#include "DDB/Process.h"
#include "DDB/Error.h"
#include "DDB/Pipe.h"

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

std::unique_ptr<DDB::Process> DDB::Process::launch(std::filesystem::path path,
                                                   bool debug) {
  Pipe pipe(/*closeOnExec=*/true);

  pid_t pid;
  if ((pid = fork()) == -1) {
    Error::sendErrno("fork");
  }

  if (pid == 0) {
    pipe.closeRead();
    if (debug && ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1) {
      exitWithPError(pipe, "ptrace(PTRACE_TRACEME)");
    }
    if (execlp(path.c_str(), path.c_str(), nullptr) == -1) {
      exitWithPError(pipe, "execlp");
    }
  }

  pipe.closeWrite();
  std::vector<std::byte> data = pipe.read();
  pipe.closeRead();

  if (data.size()) {
    waitpid(pid, nullptr, 0);
    auto msg = reinterpret_cast<char *>(data.data());
    Error::send(std::string(msg, msg + data.size()));
  }

  std::unique_ptr<Process> proc(new Process(pid, /*termOnEnd=*/true, debug));
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
  int waitStatus;
  if (waitpid(m_pid, &waitStatus, 0) == -1) {
    Error::sendErrno("waitpid");
  }

  StopReason reason(waitStatus);
  m_state = reason.state;

  return reason;
}

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
}
