#include "DDB/Error.h"
#include "DDB/Process.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <editline/readline.h>

using namespace std::string_view_literals;

namespace {
std::unique_ptr<DDB::Process> attach(int argc, const char *argv[]);
void mainLoop(std::unique_ptr<DDB::Process> &proc);
void handleCommand(std::unique_ptr<DDB::Process> &proc, std::string_view line);
void printStopReason(const DDB::Process &proc, DDB::StopReason reason);
std::vector<std::string> split(std::string_view str, char delim);
bool isPrefix(std::string_view str, std::string_view of);
void resume(pid_t pid);
void waitOnSignal(pid_t pid);
} // namespace

int main(int argc, const char *argv[]) {
  if (argc == 1) {
    std::cerr << "No arguments given\n";
    std::exit(EXIT_FAILURE);
  }

  int ret = EXIT_SUCCESS;
  try {
    std::unique_ptr<DDB::Process> proc = attach(argc, argv);
    mainLoop(proc);
  } catch (const DDB::Error &err) {
    std::cout << err.what() << '\n';
    ret = EXIT_FAILURE;
  }

  return ret;
}

namespace {
std::unique_ptr<DDB::Process> attach(int argc, const char *argv[]) {
  if (argc == 3 && argv[1] == "-p"sv) {
    pid_t pid = std::atoi(argv[2]);
    return DDB::Process::attach(pid);
  } else {
    const char *progPath = argv[1];
    return DDB::Process::launch(progPath);
  }
}

void mainLoop(std::unique_ptr<DDB::Process> &proc) {
  char *line = nullptr;
  while ((line = readline("DDB> ")) != nullptr) {
    std::string lineStr;

    if (line == ""sv) {
      std::free(line);
      if (history_length > 0) {
        lineStr = history_list()[history_length - 1]->line;
      }
    } else {
      lineStr = line;
      add_history(line);
      std::free(line);
    }

    if (!lineStr.empty()) {
      try {
        handleCommand(proc, lineStr);
      } catch (const DDB::Error &err) {
        std::cout << err.what() << '\n';
      }
    }
  }
}

void handleCommand(std::unique_ptr<DDB::Process> &proc, std::string_view line) {
  std::vector<std::string> args = split(line, ' ');

  std::string cmd = args[0];
  if (isPrefix(cmd, "continue")) {
    proc->resume();
    DDB::StopReason reason = proc->waitOnSignal();
    printStopReason(*proc, reason);
  } else {
    std::cerr << "Unknown command\n";
  }
}

void printStopReason(const DDB::Process &proc, DDB::StopReason reason) {
  std::cout << "Process " << proc.pid() << ' ';
  switch (reason.state) {
  case DDB::ProcessState::Exited:
    std::cout << "exited with status " << static_cast<int>(reason.info);
    break;
  case DDB::ProcessState::Terminated:
    std::cout << "terminated with signal " << sigabbrev_np(reason.info);
    break;
  case DDB::ProcessState::Stopped:
    std::cout << "stopped with signal " << sigabbrev_np(reason.info);
    break;
  default:
    break;
  }
  std::cout << std::endl;
}

std::vector<std::string> split(std::string_view str, char delim) {
  std::vector<std::string> out;
  std::istringstream iss{std::string(str)};

  std::string item;
  while (std::getline(iss, item, delim)) {
    out.push_back(item);
  }

  return out;
}

bool isPrefix(std::string_view str, std::string_view of) {
  if (str.size() > of.size()) {
    return false;
  }
  return std::equal(str.begin(), str.end(), of.begin());
}

void resume(pid_t pid) {
  if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) == -1) {
    std::perror("ptrace(PTRACE_CONT)");
    std::exit(EXIT_FAILURE);
  }
}

void waitOnSignal(pid_t pid) {
  int waitStatus;
  if (waitpid(pid, &waitStatus, 0) == -1) {
    std::perror("waitpid");
    std::exit(EXIT_FAILURE);
  }
}
} // namespace
