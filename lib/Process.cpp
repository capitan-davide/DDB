#include "DDB/Process.h"
#include "DDB/Error.h"
#include "DDB/Pipe.h"
#include "DDB/RegisterInfo.h"
#include "DDB/Types.h"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <signal.h>
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
  if (WIFEXITED(waitStatus)) {
    state = ProcessState::Exited;
    info = WEXITSTATUS(waitStatus);
  } else if (WIFSIGNALED(waitStatus)) {
    state = ProcessState::Terminated;
    info = WTERMSIG(waitStatus);
  } else if (WIFSTOPPED(waitStatus)) {
    state = ProcessState::Stopped;
    info = WSTOPSIG(waitStatus);
  }
  // TODO: Explicit WIFCONTINUED?
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

    // We don't need the read end of the pipe, we only write to it in case of
    // errors.
    pipe.closeRead();
    if (outFd) {
      if (dup2(*outFd, STDOUT_FILENO) == -1) {
        exitWithPError(pipe, "dup2");
      }
    }

    // PTRACE_TRACEME allows the parent process to trace this child. Also, it
    // causes the child to stop after calling exec*, giving the parent a chance
    // to take control before the new program begins execution.
    if (debug && ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1) {
      exitWithPError(pipe, "ptrace(PTRACE_TRACEME)");
    }

    // execlp returns only if there was an error, in which case we write the
    // error message to the pipe and terminate.
    execlp(path.c_str(), path.c_str(), nullptr);
    exitWithPError(pipe, "execlp");
  }

  // The parent process closes the write end of the pipe and waits for the child
  // to write to it in case of errors. When the child process closes the write
  // end (see closeOnExec=true above), the read operation will return.
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
    proc->waitOnSignal();
  }

  return proc;
}

std::unique_ptr<DDB::Process> DDB::Process::attach(pid_t pid) {
  if (pid == 0) {
    Error::send("Invalid PID");
  }

  if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) == -1) {
    Error::sendErrno("ptrace(PTRACE_ATTACH)");
  }

  std::unique_ptr<Process> proc(
      new Process(pid, /*termOnEnd=*/false, /*isAttached=*/true));
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
  if (ptrace(PTRACE_CONT, m_pid, nullptr, nullptr) == -1) {
    Error::sendErrno("ptrace(PTRACE_CONT)");
  }
  m_state = ProcessState::Running;
}

DDB::StopReason DDB::Process::waitOnSignal() {
  // By default, waitpid with options set to 0 waits only for a terminated
  // child but, when a process is being traced with ptrace, it also returns when
  // the child has stopped.
  int waitStatus;
  if (waitpid(m_pid, &waitStatus, 0) == -1) {
    Error::sendErrno("waitpid");
  }

  StopReason reason(waitStatus);
  m_state = reason.state;

  if (m_isAttached && m_state == ProcessState::Stopped) {
    readAllRegisters();
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
