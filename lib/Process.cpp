#include "DDB/Process.h"
#include "DDB/BreakpointSite.h"
#include "DDB/Error.h"
#include "DDB/Pipe.h"
#include "DDB/RegisterInfo.h"
#include "DDB/Types.h"

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <signal.h>
#include <sys/personality.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
[[noreturn]] void exitWithPError(DDB::Pipe &pipe, std::string_view prefix) {
  std::string msg = std::string(prefix) + ": " + std::strerror(errno);
  pipe.write(reinterpret_cast<std::byte *>(msg.data()), msg.size());
  std::exit(EXIT_FAILURE);
}
} // namespace

DDB::StopReason::StopReason(int waitStatus) {
  // According to wait(2), a state change is considered to be:
  //   - the child terminated;
  //   - the child was stopped by a signal; or
  //   - the child was resumed by a signal.
  if (WIFEXITED(waitStatus)) {
    state = ProcessState::Exited;
    info = WEXITSTATUS(waitStatus);
  } else if (WIFSIGNALED(waitStatus)) {
    state = ProcessState::Terminated;
    info = WTERMSIG(waitStatus);
  } else if (WIFSTOPPED(waitStatus)) {
    state = ProcessState::Stopped;
    info = WSTOPSIG(waitStatus);
  } else if (WIFCONTINUED(waitStatus)) {
    state = ProcessState::Running;
    info = 0;
  } else {
    DDB_UNREACHABLE("Invalid wait status");
  }
}

std::unique_ptr<DDB::Process> DDB::Process::launch(std::filesystem::path path,
                                                   bool debug,
                                                   std::optional<int> outFd) {
  Pipe pipe(/*closeOnExec=*/true);

  pid_t pid = fork();
  if (pid == -1) {
    Error::sendErrno("fork");
  }

  // If we are the child process, execute the target program.
  if (pid == 0) {
    // Disable ASLR for processes that we launch so the address remain stable
    // between program runs.
    if (personality(ADDR_NO_RANDOMIZE) == -1) {
      exitWithPError(pipe, "personality");
    }

    // We don't need the read end of the pipe, we only write to it in case of
    // errors.
    pipe.closeRead();
    if (outFd) {
      if (dup2(*outFd, STDOUT_FILENO) == -1) {
        exitWithPError(pipe, "dup2");
      }
    }

    // PTRACE_TRACEME allows the parent process to trace this child. Also, it
    // causes the child to stop after calling 'exec*()', giving the parent a
    // chance to take control before the new program begins execution.
    if (debug && ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1) {
      exitWithPError(pipe, "ptrace(PTRACE_TRACEME)");
    }

    // 'execlp' returns only if there was an error, in which case we write the
    // error message to the pipe and terminate.
    execlp(path.c_str(), path.c_str(), nullptr);
    exitWithPError(pipe, "execlp");
  }

  // The parent process closes the write end of the pipe then waits for the
  // child to write to it in case of errors. When the child process also closes
  // the write end, the read operation will return (see closeOnExec=true above).
  pipe.closeWrite();
  std::vector<std::byte> data = pipe.read();
  pipe.closeRead();

  if (data.size() > 0) {
    waitpid(pid, nullptr, 0);
    auto msg = reinterpret_cast<char *>(data.data());
    Error::send(std::string(msg, msg + data.size()));
  }

  std::unique_ptr<Process> proc(
      new Process(pid, /*termOnEnd=*/true, /*isAttached=*/debug));
  if (debug) {
    // In 'debug' mode the child will be traced so we wait for the SIGTRAP to
    // stop the process.
    proc->waitOnSignal();
  }

  return proc;
}

std::unique_ptr<DDB::Process> DDB::Process::attach(pid_t pid) {
  if (pid == 0) {
    Error::send("Invalid PID");
  }

  // PTRACE_ATTACH will make the target process a tracee. The tracee is sent a
  // SIGSTOP automatically.
  if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) == -1) {
    Error::sendErrno("ptrace(PTRACE_ATTACH)");
  }

  std::unique_ptr<Process> proc(
      new Process(pid, /*termOnEnd=*/false, /*isAttached=*/true));

  // Here we wait for the SIGSTOP to take effect.
  proc->waitOnSignal();

  return proc;
}

DDB::Process::~Process() {
  if (m_pid != 0) {
    int waitStatus;
    if (m_isAttached) {
      if (m_state == ProcessState::Running) {
        kill(m_pid, SIGSTOP);
        waitpid(m_pid, &waitStatus, 0);
      }
      ptrace(PTRACE_DETACH, m_pid, nullptr, nullptr);
      kill(m_pid, SIGCONT);
    }

    if (m_termOnEnd) {
      kill(m_pid, SIGKILL);
      waitpid(m_pid, &waitStatus, 0);
    }
  }
}

void DDB::Process::resume() {
  // To resume the execution we need to:
  //   1. disable the breakpoint (i.e., restore the 'm_savedData')
  //   2. step over a single instruction
  //   3. re-enable the breakpoint
  //   4. continue
  VirtAddr pc = getPC();
  if (m_breakpointSites.enabledAtAddr(pc)) {
    BreakpointSite &bs = m_breakpointSites.getByAddr(pc);
    bs.disable();
    if (ptrace(PTRACE_SINGLESTEP, pid(), nullptr, nullptr) == -1) {
      Error::sendErrno("ptrace(PTRACE_SINGLESTEP)");
    }
    int waitStatus;
    if (waitpid(pid(), &waitStatus, 0) == -1) {
      Error::sendErrno("waitpid");
    }
    bs.enable();
  }

  if (ptrace(PTRACE_CONT, m_pid, nullptr, nullptr) == -1) {
    Error::sendErrno("ptrace(PTRACE_CONT)");
  }
  m_state = ProcessState::Running;
}

void DDB::Process::terminate() {
  if (m_pid == 0)
    return;

  int waitStatus;
  if (m_isAttached) {
    if (m_state == ProcessState::Running) {
      kill(m_pid, SIGSTOP);
      waitpid(m_pid, &waitStatus, 0);
    }
    ptrace(PTRACE_DETACH, m_pid, nullptr, nullptr);
    kill(m_pid, SIGCONT);
  }

  kill(m_pid, SIGKILL);
  waitpid(m_pid, &waitStatus, 0);
}

DDB::StopReason DDB::Process::waitOnSignal() {
  // By default, waitpid with 'options=0' waits only for child termination but,
  // when a process is being traced with ptrace, it also returns when the child
  // has stopped.
  int waitStatus;
  if (waitpid(m_pid, &waitStatus, 0) == -1) {
    Error::sendErrno("waitpid");
  }

  StopReason reason(waitStatus);
  m_state = reason.state;

  if (m_isAttached && m_state == ProcessState::Stopped) {
    readAllRegisters();

    // If we stopped because we hit a breakpoint, we should fix up the program
    // counter to point to the breakpoint. This is required because, to resume
    // the program later on.
    VirtAddr instrBegin = getPC() - 1;
    if (reason.info == SIGTRAP && breakpointSites().enabledAtAddr(instrBegin)) {
      setPC(instrBegin);
    }
  }

  return reason;
}

void DDB::Process::readAllRegisters() {
  if (ptrace(PTRACE_GETREGS, m_pid, nullptr, &getRegisters().m_data.regs) ==
      -1) {
    Error::sendErrno("ptrace(PTRACE_GETREGS)");
  }
  if (ptrace(PTRACE_GETFPREGS, m_pid, nullptr, &getRegisters().m_data.i387) ==
      -1) {
    Error::sendErrno("ptrace(PTRACE_GETREGS)");
  }
  for (unsigned i = 0; i < 8; ++i) {
    auto id = static_cast<int>(RegisterId::dr0) + i;
    RegisterInfo info = registerInfoById(static_cast<RegisterId>(id));

    errno = 0;
    U64 data = ptrace(PTRACE_PEEKUSER, m_pid, info.offset, nullptr);
    if (data == -1 && errno != 0)
      Error::sendErrno("ptrace(PTRACE_PEEKUSER");

    getRegisters().m_data.u_debugreg[i] = data;
  }
}

void DDB::Process::writeUserArea(std::size_t offset, U64 data) {
  // FIXME: When executing 'reg write ah 0x42' ptrace returns EIO error. Still,
  // the 'ah' portion of 'rax' register seems to be written correctly.
  // Interestingly, 'reg write al 0x42' does not generate EIO.
  // The same seems to be happening with all "high" portion of x86_64 registers
  // (e.g., 'bh, 'ch', etc.).
  if (ptrace(PTRACE_POKEUSER, m_pid, offset, data) == -1) {
    Error::sendErrno("ptrace(PTRACE_POKEUSER)");
  }
}

void DDB::Process::writeFPRs(const user_fpregs_struct &fprs) {
  if (ptrace(PTRACE_SETFPREGS, m_pid, nullptr, &fprs) == -1) {
    Error::sendErrno("ptrace(PTRACE_SETFPREGS)");
  }
}

void DDB::Process::writeGPRs(const user_regs_struct &gprs) {
  if (ptrace(PTRACE_SETREGS, m_pid, nullptr, &gprs) == -1) {
    Error::sendErrno("ptrace(PTRACE_SETREGS)");
  }
}

DDB::BreakpointSite &DDB::Process::createBreakpointSite(VirtAddr addr) {
  if (m_breakpointSites.containsAddr(addr)) {
    Error::send("Breakpoint site already created at address " +
                std::to_string(addr.asInt()));
  }
  return m_breakpointSites.push(
      std::unique_ptr<BreakpointSite>(new BreakpointSite(*this, addr)));
}
