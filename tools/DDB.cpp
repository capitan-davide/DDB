#include "DDB/BreakpointSite.h"
#include "DDB/Disassembler.h"
#include "DDB/Error.h"
#include "DDB/Parse.h"
#include "DDB/Process.h"
#include "DDB/RegisterInfo.h"
#include "DDB/Types.h"
#include "DDB/Watchpoint.h"

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <signal.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <editline/readline.h>
#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>

using namespace std::string_view_literals;

namespace {
DDB::Process *Process;
void handleSigInt(int) { ::kill(Process->pid(), SIGSTOP); }

std::unique_ptr<DDB::Process> attach(int Argc, const char *Argv[]);
void mainLoop(std::unique_ptr<DDB::Process> &Proc);
void handleCommand(std::unique_ptr<DDB::Process> &Proc, std::string_view Line);
void handleRegisterCommand(DDB::Process &Proc,
                           const std::vector<std::string> &Args);
void handleRegisterRead(DDB::Process &Proc,
                        const std::vector<std::string> &Args);
void handleRegisterWrite(DDB::Process &Proc,
                         const std::vector<std::string> &Args);
void handleBreakpointCommand(DDB::Process &Proc,
                             const std::vector<std::string> &Args);
void handleWatchpointCommand(DDB::Process &Proc,
                             const std::vector<std::string> &Args);
void handleWatchpointList(DDB::Process &Proc,
                          const std::vector<std::string> &Args);
void handleWatchpointSet(DDB::Process &Proc,
                         const std::vector<std::string> &Args);
void handleMemoryCommand(DDB::Process &Proc,
                         const std::vector<std::string> &Args);
void handleMemoryReadCommand(DDB::Process &Proc,
                             const std::vector<std::string> &Args);
void handleMemoryWriteCommand(DDB::Process &Proc,
                              const std::vector<std::string> &Args);
void handleDisassembleCommand(DDB::Process &Proc,
                              const std::vector<std::string> &Args);
void handleStop(DDB::Process &Proc, DDB::StopReason Reason);
void handleQuitCommand(DDB::Process &Proc,
                       const std::vector<std::string> &Args);
DDB::Registers::Value parseRegisterValue(DDB::RegisterInfo Info,
                                         std::string_view Text);
void printStopReason(const DDB::Process &Proc, DDB::StopReason Reason);
void printDisassembly(const DDB::Process &Proc, DDB::VirtAddr Addr,
                      std::size_t NumInstr);
void printHelp(const std::vector<std::string> &Args);
std::vector<std::string> split(std::string_view Str, char Delim);
bool isPrefix(std::string_view Str, std::string_view Of);
void resume(pid_t Pid);
void waitOnSignal(pid_t Pid);
} // namespace

int main(int Argc, const char *Argv[]) {
  if (Argc == 1) {
    std::cerr << "No arguments given\n";
    std::exit(EXIT_FAILURE);
  }

  int Ret = EXIT_SUCCESS;
  try {
    std::unique_ptr<DDB::Process> Proc = attach(Argc, Argv);
    Process = Proc.get();
    ::signal(SIGINT, handleSigInt);
    mainLoop(Proc);
  } catch (const DDB::Error &Err) {
    std::cout << Err.what() << '\n';
    Ret = EXIT_FAILURE;
  }

  return Ret;
}

namespace {
std::unique_ptr<DDB::Process> attach(int Argc, const char *Argv[]) {
  if (Argc == 3 && Argv[1] == "-p"sv) {
    pid_t Pid = std::atoi(Argv[2]);
    return DDB::Process::attach(Pid);
  } else {
    const char *ProgPath = Argv[1];
    auto Proc = DDB::Process::launch(ProgPath);
    fmt::println("Launched process with PID {}", Proc->pid());
    return Proc;
  }
}

void mainLoop(std::unique_ptr<DDB::Process> &Proc) {
  char *Line = nullptr;
  while ((Line = readline("DDB> ")) != nullptr) {
    std::string LineStr;

    if (Line == ""sv) {
      std::free(Line);
      if (history_length > 0) {
        LineStr = history_list()[history_length - 1]->line;
      }
    } else {
      LineStr = Line;
      add_history(Line);
      std::free(Line);
    }

    if (!LineStr.empty()) {
      try {
        handleCommand(Proc, LineStr);
      } catch (const DDB::Error &Err) {
        std::cout << Err.what() << '\n';
      }
    }
  }
}

void handleCommand(std::unique_ptr<DDB::Process> &Proc, std::string_view Line) {
  std::vector<std::string> Args = split(Line, ' ');

  std::string Cmd = Args[0];
  if (isPrefix(Cmd, "continue")) {
    Proc->resume();
    DDB::StopReason Reason = Proc->waitOnSignal();
    handleStop(*Proc, Reason);
  } else if (isPrefix(Cmd, "register")) {
    handleRegisterCommand(*Proc, Args);
  } else if (isPrefix(Cmd, "breakpoint")) {
    handleBreakpointCommand(*Proc, Args);
  } else if (isPrefix(Cmd, "step")) {
    DDB::StopReason Reason = Proc->stepInstruction();
    handleStop(*Proc, Reason);
  } else if (isPrefix(Cmd, "memory")) {
    handleMemoryCommand(*Proc, Args);
  } else if (isPrefix(Cmd, "disassemble")) {
    handleDisassembleCommand(*Proc, Args);
  } else if (isPrefix(Cmd, "watchpoint")) {
    handleWatchpointCommand(*Proc, Args);
  } else if (isPrefix(Cmd, "help")) {
    printHelp(Args);
  } else if (isPrefix(Cmd, "quit")) {
    handleQuitCommand(*Proc, Args);
  } else {
    std::cerr << "Unknown command\n";
  }
}

void handleRegisterCommand(DDB::Process &Proc,
                           const std::vector<std::string> &Args) {
  if (Args.size() < 2) {
    printHelp({"help", "register"});
    return;
  }

  if (isPrefix(Args[1], "read")) {
    handleRegisterRead(Proc, Args);
  } else if (isPrefix(Args[1], "write")) {
    handleRegisterWrite(Proc, Args);
  } else {
    printHelp({"help", "register"});
  }
}

void handleRegisterRead(DDB::Process &Proc,
                        const std::vector<std::string> &Args) {
  auto Format = [](auto Val) {
    if constexpr (std::is_floating_point_v<decltype(Val)>) {
      return fmt::format("{}", Val);
    } else if constexpr (std::is_integral_v<decltype(Val)>) {
      return fmt::format("{:#0{}x}", Val, sizeof(Val) * 2 + 2);
    } else {
      return fmt::format("[{:#04x}]", fmt::join(Val, ","));
    }
  };

  if (Args.size() == 2 || (Args.size() == 3 && Args[2] == "all")) {
    for (const auto &Info : DDB::RegisterInfoTable) {
      bool ShouldPrint =
          (Args.size() == 3 || Info.Type == DDB::RegisterType::GPR) &&
          Info.Name != "orig_rax";
      if (!ShouldPrint)
        continue;
      DDB::Registers::Value Value = Proc.getRegisters().read(Info);
      fmt::println("{}:\t{}", Info.Name, std::visit(Format, Value));
    }
  } else if (Args.size() == 3) {
    try {
      const DDB::RegisterInfo &Info = DDB::registerInfoByName(Args[2]);
      DDB::Registers::Value Value = Proc.getRegisters().read(Info);
      fmt::println("{}:\t{}", Info.Name, std::visit(Format, Value));
    } catch (DDB::Error &Err) {
      std::cerr << "No such register\n";
      return;
    }
  } else {
    printHelp({"help", "register"});
  }
}

void handleRegisterWrite(DDB::Process &Proc,
                         const std::vector<std::string> &Args) {
  if (Args.size() != 4) {
    printHelp({"help", "register"});
    return;
  }

  try {
    const DDB::RegisterInfo &Info = DDB::registerInfoByName(Args[2]);
    DDB::Registers::Value Value = parseRegisterValue(Info, Args[3]);
    Proc.getRegisters().write(Info, Value);
  } catch (DDB::Error &Err) {
    std::cerr << Err.what() << '\n';
    return;
  }
}

void handleBreakpointCommand(DDB::Process &Proc,
                             const std::vector<std::string> &Args) {
  if (Args.size() < 2) {
    printHelp({"help", "breakpoint"});
    return;
  }

  std::string Cmd = Args[1];

  if (isPrefix(Cmd, "list")) {
    if (Proc.breakpointSites().empty()) {
      fmt::println("No breakpoints set");
    } else {
      fmt::println("Current breakpoints:");
      Proc.breakpointSites().forEach([](auto &BS) {
        if (BS.isInternal())
          return;
        fmt::println("{}: addr = {:#x}, {}", BS.id(), BS.addr().asInt(),
                     BS.isEnabled() ? "enabled" : "disabled");
      });
    }
    return;
  }

  if (Args.size() < 3) {
    printHelp({"help", "breakpoint"});
    return;
  }

  if (isPrefix(Cmd, "set")) {
    std::optional Addr = DDB::toIntegral<U64>(Args[2], 16);
    if (!Addr) {
      fmt::println(stderr, "Breakpoint commands expects address in "
                           "hexadecimal, prefixed with '0x'");
      return;
    }
    bool Hardware = false;
    if (Args.size() == 4) {
      if (Args[3] == "-h")
        Hardware = true;
      else
        DDB::Error::send("Invalid breakpoint command argument");
    }
    Proc.createBreakpointSite(DDB::VirtAddr(*Addr), Hardware).enable();
    return;
  }

  std::optional Id = DDB::toIntegral<DDB::BreakpointSite::IdType>(Args[2]);
  if (isPrefix(Cmd, "enable")) {
    Proc.breakpointSites().getById(*Id).enable();
  } else if (isPrefix(Cmd, "disable")) {
    Proc.breakpointSites().getById(*Id).disable();
  } else if (isPrefix(Cmd, "delete")) {
    Proc.breakpointSites().removeById(*Id);
  }
}

void handleWatchpointCommand(DDB::Process &Proc,
                             const std::vector<std::string> &Args) {
  if (Args.size() < 2) {
    printHelp({"help", "watchpoint"});
    return;
  }

  std::string Cmd = Args[1];

  if (isPrefix(Cmd, "list")) {
    handleWatchpointList(Proc, Args);
    return;
  }

  if (isPrefix(Cmd, "set")) {
    handleWatchpointSet(Proc, Args);
    return;
  }

  if (Args.size() < 3) {
    printHelp({"help", "watchpoint"});
    return;
  }

  auto Id = DDB::toIntegral<DDB::Watchpoint::IdType>(Args[2]);
  if (!Id) {
    std::cerr << "Command expects watchpoint id\n"; // FIXME: Consistent error
                                                    // handling
    return;
  }

  if (isPrefix(Cmd, "enable")) {
    Proc.watchpoints().getById(*Id).enable();
  } else if (isPrefix(Cmd, "disable")) {
    Proc.watchpoints().getById(*Id).disable();
  } else if (isPrefix(Cmd, "delete")) {
    Proc.watchpoints().removeById(*Id);
  }
}

void handleWatchpointList(DDB::Process &Proc,
                          const std::vector<std::string> &Args) {
  auto WatchpointModeToString = [](DDB::StoppointMode Mode) {
    switch (Mode) {
    case DDB::StoppointMode::Execute:
      return "x";
    case DDB::StoppointMode::Write:
      return "w";
    case DDB::StoppointMode::ReadWrite:
      return "rw";
    default:
      DDB_UNREACHABLE("Invalid stoppoint mode");
    }
  };

  if (Proc.watchpoints().empty()) {
    fmt::println("No watchpoints set");
  } else {
    Proc.watchpoints().forEach([&](DDB::Watchpoint &WP) {
      fmt::println("{}: addr = {:#x}, mode = {}, size = {}, {}", WP.id(),
                   WP.addr().asInt(), WatchpointModeToString(WP.mode()),
                   WP.size(), WP.isEnabled() ? "enabled" : "disabled");
    });
  }
}

void handleWatchpointSet(DDB::Process &Proc,
                         const std::vector<std::string> &Args) {
  if (Args.size() != 5) {
    printHelp({"help", "watchpoint"});
    return;
  }

  std::optional Addr = DDB::toIntegral<U64>(Args[2], 16);
  std::string ModeStr = Args[3];
  std::optional Size = DDB::toIntegral<std::size_t>(Args[4]);

  if (!Addr || !Size ||
      !(ModeStr == "w" || ModeStr == "rw" || ModeStr == "x")) {
    printHelp({"help", "watchpoint"});
    return;
  }

  DDB::StoppointMode Mode;
  if (ModeStr == "w")
    Mode = DDB::StoppointMode::Write;
  else if (ModeStr == "rw")
    Mode = DDB::StoppointMode::ReadWrite;
  else if (ModeStr == "x")
    Mode = DDB::StoppointMode::Execute;

  Proc.createWatchpoint(DDB::VirtAddr(*Addr), Mode, *Size).enable();
}

void handleMemoryCommand(DDB::Process &Proc,
                         const std::vector<std::string> &Args) {
  if (Args.size() < 3) {
    printHelp({"help", "memory"});
    return;
  }
  if (isPrefix(Args[1], "read")) {
    handleMemoryReadCommand(Proc, Args);
  } else if (isPrefix(Args[1], "write")) {
    handleMemoryWriteCommand(Proc, Args);
  } else {
    printHelp({"help", "memory"});
  }
}

void handleMemoryReadCommand(DDB::Process &Proc,
                             const std::vector<std::string> &Args) {
  auto ToPrintableRange = [](auto Begin, auto End) {
    std::string Str(Begin, End);
    std::for_each(Str.begin(), Str.end(), [](auto &C) {
      if (!std::isprint(C))
        C = '.';
    });
    return Str;
  };

  auto Addr = DDB::toIntegral<U64>(Args[2], 16);
  if (!Addr)
    DDB::Error::send("Invalid address format");

  int NumBytes = 32;
  if (Args.size() == 4) {
    auto NumBytesArg = DDB::toIntegral<std::size_t>(Args[3]);
    if (!NumBytesArg)
      DDB::Error::send("Invalid number of bytes");
    NumBytes = *NumBytesArg;
  }

  std::vector<std::byte> Data = Proc.readMemory(DDB::VirtAddr(*Addr), NumBytes);
  for (std::size_t I = 0; I < Data.size(); I += 16) {
    auto Begin = Data.begin() + I;
    auto End = Data.begin() + std::min(I + 16, Data.size());
    fmt::println("{:#016x}: {:02x} {}", *Addr + I, fmt::join(Begin, End, " "),
                 ToPrintableRange(Begin, End));
  }
}

void handleMemoryWriteCommand(DDB::Process &Proc,
                              const std::vector<std::string> &Args) {
  if (Args.size() != 4) {
    printHelp({"help", "memory"});
    return;
  }

  auto Addr = DDB::toIntegral<U64>(Args[2], 16);
  if (!Addr)
    DDB::Error::send("Invalid address format");

  std::vector<std::byte> Data = DDB::parseVector(Args[3]);
  Proc.writeMemory(DDB::VirtAddr(*Addr), Data);
}

void handleDisassembleCommand(DDB::Process &Proc,
                              const std::vector<std::string> &Args) {
  DDB::VirtAddr Addr = Proc.getPC();
  std::size_t NumInstr = 5;
  for (auto It = Args.begin() + 1; It != Args.end(); ++It) {
    if (*It == "-a" && It + 1 != Args.end()) {
      ++It;
      auto OptAddr = DDB::toIntegral<U64>(*It++, 16);
      if (!OptAddr)
        DDB::Error::send("Invalid address format");
      Addr = DDB::VirtAddr(*OptAddr);
    } else if (*It == "-c" && It + 1 != Args.end()) {
      ++It;
      auto OptNumInstr = DDB::toIntegral<std::size_t>(*It++);
      if (!OptNumInstr)
        DDB::Error::send("Invalid instruction count");
      NumInstr = *OptNumInstr;
    } else {
      printHelp({"help", "disassemble"});
    }
  }
  printDisassembly(Proc, Addr, NumInstr);
}

void handleStop(DDB::Process &Proc, DDB::StopReason Reason) {
  printStopReason(Proc, Reason);
  if (Reason.State == DDB::ProcessState::Stopped) {
    printDisassembly(Proc, Proc.getPC(), 5);
  }
}

void handleQuitCommand(DDB::Process &Proc,
                       const std::vector<std::string> &Args) {
  if (Args.size() > 2) {
    printHelp({"help", "quit"});
    return;
  }

  int Status = 0;
  if (Args.size() == 2) {
    try {
      Status = DDB::toIntegral<int>(Args[1]).value();
    } catch (...) {
      printHelp({"help", "quit"});
      return;
    }
  }

  Proc.terminate();
  std::exit(Status);
}

DDB::Registers::Value parseRegisterValue(DDB::RegisterInfo Info,
                                         std::string_view Text) {
  try {
    if (Info.Format == DDB::RegisterFormat::UInt) {
      switch (Info.Size) {
      case 1:
        return DDB::toIntegral<U8>(Text, 16).value();
      case 2:
        return DDB::toIntegral<U16>(Text, 16).value();
      case 4:
        return DDB::toIntegral<U32>(Text, 16).value();
      case 8:
        return DDB::toIntegral<U64>(Text, 16).value();
      }
    } else if (Info.Format == DDB::RegisterFormat::DoubleFloat) {
      return DDB::toFloat<double>(Text).value();
    } else if (Info.Format == DDB::RegisterFormat::LongDouble) {
      return DDB::toFloat<long double>(Text).value();
    } else if (Info.Format == DDB::RegisterFormat::Vector) {
      if (Info.Size == 8) {
        return DDB::parseVector<8>(Text);
      } else if (Info.Size == 16) {
        return DDB::parseVector<16>(Text);
      }
    }
  } catch (...) {
  }
  DDB::Error::send("InvalidFormat");
}

void printStopReason(const DDB::Process &Proc, DDB::StopReason Reason) {
  std::string Msg;
  switch (Reason.State) {
  case DDB::ProcessState::Exited:
    Msg = fmt::format("exited with status {}", static_cast<int>(Reason.Info));
    break;
  case DDB::ProcessState::Terminated:
    Msg = fmt::format("terminated with signal {}", sigabbrev_np(Reason.Info));
    break;
  case DDB::ProcessState::Stopped:
    Msg = fmt::format("stopped with signal {} at {:#x}",
                      sigabbrev_np(Reason.Info), Proc.getPC().asInt());
    break;
  default:
    break;
  }
  fmt::println("Process {} {}", Proc.pid(), Msg);
}

void printDisassembly(const DDB::Process &Proc, DDB::VirtAddr Addr,
                      std::size_t NumInstr) {
  DDB::Disassembler Dis(Proc);

  std::vector Instructions = Dis.disassemble(NumInstr, Addr);
  for (const auto &Instr : Instructions) {
    fmt::println("{:#018x}: {}", Instr.Addr.asInt(), Instr.Text);
  }
}

void printHelp(const std::vector<std::string> &Args) {
  if (Args.size() == 1) {
    std::cerr << R"(Debugger commands:
  breakpoint  - Commands for operating on breakpoints
  continue    - Resume the process
  disassemble - Disassemble machine code to assembly
  memory      - Commands for operating on memory
  quit        - Quit the DDB debugger
  register    - Commands for operating on registers
  step        - Step over a single instruction
  watchpoint  - Commands for operating on watchpoints
)";
  } else if (isPrefix(Args[1], "register")) {
    std::cerr << R"(Debugger commands:
  read
  read <register>
  read all
  write <register> <value>
)";
  } else if (isPrefix(Args[1], "breakpoint")) {
    std::cerr << R"(Debugger commands:
  list
  delete <id>
  disable <id>
  enable <id>
  set <addr>
  set <addr> -h
)";
  } else if (isPrefix(Args[1], "memory")) {
    std::cerr << R"(Debugger commands:
  read <addr>
  read <addr> <number-of-bytes>
  write <addr> <bytes>
)";
  } else if (isPrefix(Args[1], "watchpoint")) {
    std::cerr << R"(Debugger commands:
  list
  delete <id>
  disable <id>
  enable <id>
  set <addr> <w|rw|x> <size>
)";
  } else if (isPrefix(Args[1], "disassemble")) {
    std::cerr << R"(Debugger commands:
  disassemble
  disassemble -c <n> -a <addr>
)";
  } else if (isPrefix(Args[1], "quit")) {
    std::cerr << R"(Debubber commands:
  quit
  quit <exit-status>
)";
  } else {
    std::cerr << "No help available on that\n";
  }
}

std::vector<std::string> split(std::string_view Str, char Delim) {
  std::vector<std::string> Out;
  std::istringstream Iss{std::string(Str)};

  std::string Item;
  while (std::getline(Iss, Item, Delim)) {
    Out.push_back(Item);
  }

  return Out;
}

bool isPrefix(std::string_view Str, std::string_view Of) {
  if (Str.size() > Of.size()) {
    return false;
  }
  return std::equal(Str.begin(), Str.end(), Of.begin());
}

void resume(pid_t Pid) {
  if (ptrace(PTRACE_CONT, Pid, nullptr, nullptr) == -1) {
    std::perror("ptrace(PTRACE_CONT)");
    std::exit(EXIT_FAILURE);
  }
}

void waitOnSignal(pid_t Pid) {
  int WaitStatus;
  if (waitpid(Pid, &WaitStatus, 0) == -1) {
    std::perror("waitpid");
    std::exit(EXIT_FAILURE);
  }
}
} // namespace
