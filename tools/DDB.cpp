#include "DDB/BreakpointSite.h"
#include "DDB/Error.h"
#include "DDB/Parse.h"
#include "DDB/Process.h"
#include "DDB/RegisterInfo.h"
#include "DDB/Types.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <editline/readline.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

using namespace std::string_view_literals;

namespace {
std::unique_ptr<DDB::Process> attach(int argc, const char *argv[]);
void mainLoop(std::unique_ptr<DDB::Process> &proc);
void handleCommand(std::unique_ptr<DDB::Process> &proc, std::string_view line);
void handleRegisterCommand(DDB::Process &proc,
                           const std::vector<std::string> &args);
void handleBreakpointCommand(DDB::Process &proc,
                             const std::vector<std::string> &args);
void handleRegisterRead(DDB::Process &proc,
                        const std::vector<std::string> &args);
void handleRegisterWrite(DDB::Process &proc,
                         const std::vector<std::string> &args);
void handleQuitCommand(DDB::Process &proc,
                       const std::vector<std::string> &args);
DDB::Registers::Value parseRegisterValue(DDB::RegisterInfo info,
                                         std::string_view text);
void printStopReason(const DDB::Process &proc, DDB::StopReason reason);
void printHelp(const std::vector<std::string> &args);
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
    auto proc = DDB::Process::launch(progPath);
    fmt::println("Launched process with PID {}", proc->pid());
    return proc;
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
  } else if (isPrefix(cmd, "register")) {
    handleRegisterCommand(*proc, args);
  } else if (isPrefix(cmd, "breakpoint")) {
    handleBreakpointCommand(*proc, args);
  } else if (isPrefix(cmd, "help")) {
    printHelp(args);
  } else if (isPrefix(cmd, "quit")) {
    handleQuitCommand(*proc, args);
  } else {
    std::cerr << "Unknown command\n";
  }
}

void handleRegisterCommand(DDB::Process &proc,
                           const std::vector<std::string> &args) {
  if (args.size() < 2) {
    printHelp({"help", "register"});
    return;
  }

  if (isPrefix(args[1], "read")) {
    handleRegisterRead(proc, args);
  } else if (isPrefix(args[1], "write")) {
    handleRegisterWrite(proc, args);
  } else {
    printHelp({"help", "register"});
  }
}

void handleBreakpointCommand(DDB::Process &proc,
                             const std::vector<std::string> &args) {
  if (args.size() < 2) {
    printHelp({"help", "breakpoint"});
    return;
  }

  std::string cmd = args[1];

  if (isPrefix(cmd, "list")) {
    if (proc.breakpointSites().empty()) {
      fmt::println("No breakpoints set");
    } else {
      fmt::println("Current breakpoints:");
      proc.breakpointSites().forEach([](auto &bs) {
        fmt::println("{}: addr = {:#x}, {}", bs.id(), bs.addr().asInt(),
                     bs.isEnabled() ? "enabled" : "disabled");
      });
    }
    return;
  }

  if (args.size() < 3) {
    printHelp({"help", "breakpoint"});
    return;
  }

  if (isPrefix(cmd, "set")) {
    std::optional addr = DDB::toIntegral<U64>(args[2], 16);
    if (!addr) {
      fmt::println(stderr, "Breakpoint commands expects address in "
                           "hexadecimal, prefixed with '0x'");
      return;
    }
    proc.createBreakpointSite(DDB::VirtAddr(*addr)).enable();
    return;
  }

  std::optional id = DDB::toIntegral<DDB::BreakpointSite::IdType>(args[2]);
  if (isPrefix(cmd, "enable")) {
    proc.breakpointSites().getById(*id).enable();
  } else if (isPrefix(cmd, "disable")) {
    proc.breakpointSites().getById(*id).disable();
  } else if (isPrefix(cmd, "delete")) {
    proc.breakpointSites().removeById(*id);
  }
}

void handleRegisterRead(DDB::Process &proc,
                        const std::vector<std::string> &args) {
  auto format = [](auto t) {
    if constexpr (std::is_floating_point_v<decltype(t)>) {
      return fmt::format("{}", t);
    } else if constexpr (std::is_integral_v<decltype(t)>) {
      return fmt::format("{:#0{}x}", t, sizeof(t) * 2 + 2);
    } else {
      return fmt::format("[{:#04x}]", fmt::join(t, ","));
    }
  };

  if (args.size() == 2 || (args.size() == 3 && args[2] == "all")) {
    for (const auto &info : DDB::g_registerInfo) {
      bool shouldPrint =
          (args.size() == 3 || info.type == DDB::RegisterType::GPR) &&
          info.name != "orig_rax";
      if (!shouldPrint)
        continue;
      DDB::Registers::Value value = proc.getRegisters().read(info);
      fmt::println("{}:\t{}", info.name, std::visit(format, value));
    }
  } else if (args.size() == 3) {
    try {
      const DDB::RegisterInfo &info = DDB::registerInfoByName(args[2]);
      DDB::Registers::Value value = proc.getRegisters().read(info);
      fmt::println("{}:\t{}", info.name, std::visit(format, value));
    } catch (DDB::Error &err) {
      std::cerr << "No such register\n";
      return;
    }
  } else {
    printHelp({"help", "register"});
  }
}

void handleRegisterWrite(DDB::Process &proc,
                         const std::vector<std::string> &args) {
  if (args.size() != 4) {
    printHelp({"help", "register"});
    return;
  }

  try {
    const DDB::RegisterInfo &info = DDB::registerInfoByName(args[2]);
    DDB::Registers::Value value = parseRegisterValue(info, args[3]);
    proc.getRegisters().write(info, value);
  } catch (DDB::Error &err) {
    std::cerr << err.what() << '\n';
    return;
  }
}

void handleQuitCommand(DDB::Process &proc,
                       const std::vector<std::string> &args) {
  if (args.size() > 2) {
    printHelp({"help", "quit"});
    return;
  }

  int status = 0;
  if (args.size() == 2) {
    try {
      status = DDB::toIntegral<int>(args[1]).value();
    } catch (...) {
      printHelp({"help", "quit"});
      return;
    }
  }

  proc.terminate();
  std::exit(status);
}

DDB::Registers::Value parseRegisterValue(DDB::RegisterInfo info,
                                         std::string_view text) {
  try {
    if (info.format == DDB::RegisterFormat::UInt) {
      switch (info.size) {
      case 1:
        return DDB::toIntegral<U8>(text, 16).value();
      case 2:
        return DDB::toIntegral<U16>(text, 16).value();
      case 4:
        return DDB::toIntegral<U32>(text, 16).value();
      case 8:
        return DDB::toIntegral<U64>(text, 16).value();
      }
    } else if (info.format == DDB::RegisterFormat::DoubleFloat) {
      return DDB::toFloat<double>(text).value();
    } else if (info.format == DDB::RegisterFormat::LongDouble) {
      return DDB::toFloat<long double>(text).value();
    } else if (info.format == DDB::RegisterFormat::Vector) {
      if (info.size == 8) {
        return DDB::parseVector<8>(text);
      } else if (info.size == 16) {
        return DDB::parseVector<16>(text);
      }
    }
  } catch (...) {
  }
  DDB::Error::send("InvalidFormat");
}

void printStopReason(const DDB::Process &proc, DDB::StopReason reason) {
  std::string msg;
  switch (reason.state) {
  case DDB::ProcessState::Exited:
    msg = fmt::format("exited with status {}", static_cast<int>(reason.info));
    break;
  case DDB::ProcessState::Terminated:
    msg = fmt::format("terminated with signal {}", sigabbrev_np(reason.info));
    break;
  case DDB::ProcessState::Stopped:
    msg = fmt::format("stopped with signal {} at {:#x}",
                      sigabbrev_np(reason.info), proc.getPC().asInt());
    break;
  default:
    break;
  }
  fmt::println("Process {} {}", proc.pid(), msg);
}

void printHelp(const std::vector<std::string> &args) {
  if (args.size() == 1) {
    std::cerr << R"(Debugger commands:
  breakpoint  - Commands for operating on breakpoints
  continue    - Resume the process
  quit        - Quit the DDB debugger
  register    - Commands for operating on registers
)";
  } else if (isPrefix(args[1], "register")) {
    std::cerr << R"(Debugger commands:
  read
  read <register>
  read all
  write <register> <value>
)";
  } else if (isPrefix(args[1], "breakpoint")) {
    std::cerr << R"(Debugger commands:
  list
  delete <id>
  disable <id>
  enable <id>
  set <addr>
)";
  } else if (isPrefix(args[1], "quit")) {
    std::cerr << R"(Debubber commands:
  quit
  quit <exit-status>
)";
  } else {
    std::cerr << "No help available on that\n";
  }
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
