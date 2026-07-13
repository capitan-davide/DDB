#include "DDB/Process.h"
#include "DDB/Bit.h"
#include "DDB/BreakpointSite.h"
#include "DDB/Error.h"
#include "DDB/Pipe.h"
#include "DDB/RegisterInfo.h"
#include "DDB/Types.h"
#include "DDB/Watchpoint.h"

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <signal.h>
#include <sys/personality.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
[[noreturn]] void exitWithPError(DDB::Pipe &Pipe, std::string_view Prefix) {
  std::string Msg = std::string(Prefix) + ": " + std::strerror(errno);
  Pipe.write(reinterpret_cast<std::byte *>(Msg.data()), Msg.size());
  std::exit(EXIT_FAILURE);
}

int findFreeStoppointRegister(U64 CtlReg) {
  for (unsigned I = 0; I < 4; ++I) {
    if ((CtlReg & (0b11 << (I * 2))) == 0) {
      return I;
    }
  }
  DDB::Error::send("No remaining hardware debug registers");
}

U64 encodeHardwareStoppointMode(DDB::StoppointMode Mode) {
  switch (Mode) {
  case DDB::StoppointMode::Write:
    return 0b01;
  case DDB::StoppointMode::ReadWrite:
    return 0b11;
  case DDB::StoppointMode::Execute:
    return 0b00;
  default:
    DDB_UNREACHABLE("Invalid stoppoint mode");
  }
}

U64 encodeHardwareStoppointSize(std::size_t Size) {
  switch (Size) {
  case 1:
    return 0b00;
  case 2:
    return 0b01;
  case 4:
    return 0b11;
  case 8:
    return 0b10;
  default:
    DDB_UNREACHABLE("Invalid stoppoint size");
  }
}
} // namespace

DDB::StopReason::StopReason(int WaitStatus) {
  // According to wait(2), a state change is considered to be:
  //   - the child terminated;
  //   - the child was stopped by a signal; or
  //   - the child was resumed by a signal.
  if (WIFEXITED(WaitStatus)) {
    State = ProcessState::Exited;
    Info = WEXITSTATUS(WaitStatus);
  } else if (WIFSIGNALED(WaitStatus)) {
    State = ProcessState::Terminated;
    Info = WTERMSIG(WaitStatus);
  } else if (WIFSTOPPED(WaitStatus)) {
    State = ProcessState::Stopped;
    Info = WSTOPSIG(WaitStatus);
  } else if (WIFCONTINUED(WaitStatus)) {
    State = ProcessState::Running;
    Info = 0;
  } else {
    DDB_UNREACHABLE("Invalid wait status");
  }
}

std::unique_ptr<DDB::Process> DDB::Process::launch(std::filesystem::path Path,
                                                   bool Debug,
                                                   std::optional<int> OutFD) {
  Pipe Pipe(/*CloseOnExec=*/true);

  pid_t Pid = fork();
  if (Pid == -1) {
    Error::sendErrno("fork");
  }

  // If we are the child process, execute the target program.
  if (Pid == 0) {
    // Change the child process group ID. This way we ensure that, when the
    // user sends a signal to the debugger, this will not be sent to the
    // child process as well. For example, when the user presses Ctrl-C,
    // a SIGINT is delivered to *all* processes the the debugger's process
    // group.
    if (::setpgid(0, 0) == -1) {
      exitWithPError(Pipe, "setpgid");
    }

    // Disable ASLR for processes that we launch so the address remain stable
    // between program runs.
    if (personality(ADDR_NO_RANDOMIZE) == -1) {
      exitWithPError(Pipe, "personality");
    }

    // We don't need the read end of the pipe, we only write to it in case of
    // errors.
    Pipe.closeRead();
    if (OutFD) {
      if (dup2(*OutFD, STDOUT_FILENO) == -1) {
        exitWithPError(Pipe, "dup2");
      }
    }

    // PTRACE_TRACEME allows the parent process to trace this child. Also, it
    // causes the child to stop after calling 'exec*()', giving the parent a
    // chance to take control before the new program begins execution.
    if (Debug && ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1) {
      exitWithPError(Pipe, "ptrace(PTRACE_TRACEME)");
    }

    // 'execlp' returns only if there was an error, in which case we write the
    // error message to the pipe and terminate.
    execlp(Path.c_str(), Path.c_str(), nullptr);
    exitWithPError(Pipe, "execlp");
  }

  // The parent process closes the write end of the pipe then waits for the
  // child to write to it in case of errors. When the child process also closes
  // the write end, the read operation will return (see CloseOnExec=true above).
  Pipe.closeWrite();
  std::vector<std::byte> Data = Pipe.read();
  Pipe.closeRead();

  if (Data.size() > 0) {
    waitpid(Pid, nullptr, 0);
    auto Msg = reinterpret_cast<char *>(Data.data());
    Error::send(std::string(Msg, Msg + Data.size()));
  }

  std::unique_ptr<Process> Proc(
      new Process(Pid, /*TermOnEnd=*/true, /*IsAttached=*/Debug));
  if (Debug) {
    // In 'debug' mode the child will be traced so we wait for the SIGTRAP to
    // stop the process.
    Proc->waitOnSignal();
  }

  return Proc;
}

std::unique_ptr<DDB::Process> DDB::Process::attach(pid_t Pid) {
  if (Pid == 0) {
    Error::send("Invalid PID");
  }

  // PTRACE_ATTACH will make the target process a tracee. The tracee is sent a
  // SIGSTOP automatically.
  if (ptrace(PTRACE_ATTACH, Pid, nullptr, nullptr) == -1) {
    Error::sendErrno("ptrace(PTRACE_ATTACH)");
  }

  std::unique_ptr<Process> Proc(
      new Process(Pid, /*TermOnEnd=*/false, /*IsAttached=*/true));

  // Here we wait for the SIGSTOP to take effect.
  Proc->waitOnSignal();

  return Proc;
}

DDB::Process::~Process() {
  if (Pid != 0) {
    int WaitStatus;
    if (IsAttached) {
      if (State == ProcessState::Running) {
        kill(Pid, SIGSTOP);
        waitpid(Pid, &WaitStatus, 0);
      }
      ptrace(PTRACE_DETACH, Pid, nullptr, nullptr);
      kill(Pid, SIGCONT);
    }

    if (TermOnEnd) {
      kill(Pid, SIGKILL);
      waitpid(Pid, &WaitStatus, 0);
    }
  }
}

void DDB::Process::resume() {
  // To resume the execution we need to:
  //   1. disable the breakpoint (i.e., restore the 'SavedData')
  //   2. step over a single instruction
  //   3. re-enable the breakpoint
  //   4. continue
  VirtAddr PC = getPC();
  if (BreakpointSites.enabledAtAddr(PC)) {
    BreakpointSite &BS = BreakpointSites.getByAddr(PC);
    BS.disable();
    if (ptrace(PTRACE_SINGLESTEP, pid(), nullptr, nullptr) == -1) {
      Error::sendErrno("ptrace(PTRACE_SINGLESTEP)");
    }
    int WaitStatus;
    if (waitpid(pid(), &WaitStatus, 0) == -1) {
      Error::sendErrno("waitpid");
    }
    BS.enable();
  }

  if (ptrace(PTRACE_CONT, Pid, nullptr, nullptr) == -1) {
    Error::sendErrno("ptrace(PTRACE_CONT)");
  }
  State = ProcessState::Running;
}

void DDB::Process::terminate() {
  if (Pid == 0)
    return;

  int WaitStatus;
  if (IsAttached) {
    if (State == ProcessState::Running) {
      kill(Pid, SIGSTOP);
      waitpid(Pid, &WaitStatus, 0);
    }
    ptrace(PTRACE_DETACH, Pid, nullptr, nullptr);
    kill(Pid, SIGCONT);
  }

  kill(Pid, SIGKILL);
  waitpid(Pid, &WaitStatus, 0);
}

DDB::StopReason DDB::Process::waitOnSignal() {
  // By default, waitpid with 'options=0' waits only for child termination but,
  // when a process is being traced with ptrace, it also returns when the child
  // has stopped.
  int WaitStatus;
  if (waitpid(Pid, &WaitStatus, 0) == -1) {
    Error::sendErrno("waitpid");
  }

  StopReason SR(WaitStatus);
  State = SR.State;

  if (IsAttached && State == ProcessState::Stopped) {
    readAllRegisters();
    augmentStopReason(SR);

    // If we stopped because we hit a breakpoint, we should fix up the program
    // counter to point to the breakpoint. This is required because, to resume
    // the program later on.
    VirtAddr InstrBegin = getPC() - 1;
    if (SR.Info == SIGTRAP && breakpointSites().enabledAtAddr(InstrBegin)) {
      setPC(InstrBegin);
    }
  }

  return SR;
}

void DDB::Process::writeUserArea(std::size_t Offset, U64 Data) {
  // FIXME: When executing 'reg write ah 0x42' ptrace returns EIO error. Still,
  // the 'ah' portion of 'rax' register seems to be written correctly.
  // Interestingly, 'reg write al 0x42' does not generate EIO.
  // The same seems to be happening with all "high" portion of x86_64 registers
  // (e.g., 'bh, 'ch', etc.).
  if (ptrace(PTRACE_POKEUSER, Pid, Offset, Data) == -1) {
    Error::sendErrno("ptrace(PTRACE_POKEUSER)");
  }
}

void DDB::Process::writeFPRs(const user_fpregs_struct &FPRs) {
  if (ptrace(PTRACE_SETFPREGS, Pid, nullptr, &FPRs) == -1) {
    Error::sendErrno("ptrace(PTRACE_SETFPREGS)");
  }
}

void DDB::Process::writeGPRs(const user_regs_struct &GPRs) {
  if (ptrace(PTRACE_SETREGS, Pid, nullptr, &GPRs) == -1) {
    Error::sendErrno("ptrace(PTRACE_SETREGS)");
  }
}

void DDB::Process::augmentStopReason(StopReason &SR) {
  siginfo_t Info;
  if (::ptrace(PTRACE_GETSIGINFO, Pid, nullptr, &Info) == -1) {
    Error::sendErrno("ptrace(PTRACE_GETSIGINFO");
  }

  // Apparently, on x86 the Linux kernel reports the wrong value: SI_KERNEL for
  // a software breakpoint and TRAP_BRKPT for a single-step over a syscall. Too
  // many important tools rely on this bug's behavior that it's not worth to
  // fix it anymore.
  SR.TrapReason = TrapType::Unknown;
  if (SR.Info == SIGTRAP) {
    switch (Info.si_code) {
    case TRAP_TRACE:
      SR.TrapReason = TrapType::SingleStep;
      break;
    case SI_KERNEL:
      SR.TrapReason = TrapType::SoftwareBreak;
      break;
    case TRAP_HWBKPT:
      SR.TrapReason = TrapType::HardwareBreak;
      break;
    }
  }
}

DDB::BreakpointSite &DDB::Process::createBreakpointSite(VirtAddr Addr,
                                                        bool Hardware,
                                                        bool Internal) {
  if (BreakpointSites.containsAddr(Addr)) {
    Error::send("Breakpoint site already created at address " +
                std::to_string(Addr.asInt()));
  }
  return BreakpointSites.push(std::unique_ptr<BreakpointSite>(
      new BreakpointSite(*this, Addr, Hardware, Internal)));
}

DDB::Watchpoint &DDB::Process::createWatchpoint(VirtAddr Addr,
                                                StoppointMode Mode,
                                                std::size_t Size) {
  if (Watchpoints.containsAddr(Addr)) {
    Error::send("Watchpoint already created at address " +
                std::to_string(Addr.asInt()));
  }
  return Watchpoints.push(
      std::unique_ptr<Watchpoint>(new Watchpoint(*this, Addr, Mode, Size)));
}

int DDB::Process::setHardwareBreakpoint(BreakpointSite::IdType Id,
                                        VirtAddr Addr) {
  return setHardwareStoppoint(Addr, StoppointMode::Execute, 1);
}

int DDB::Process::setWatchpoint(Watchpoint::IdType Id, VirtAddr Addr,
                                StoppointMode Mode, std::size_t Size) {
  return setHardwareStoppoint(Addr, Mode, Size);
}

std::variant<DDB::BreakpointSite::IdType, DDB::Watchpoint::IdType>
DDB::Process::getCurrentHardwareStoppoint() const {
  // TODO: Not implemented.
  return {};
}

void DDB::Process::clearHardwareStoppoint(int Idx) {
  Registers &Regs = getRegisters();

  auto Id = static_cast<int>(RegisterId::dr0) + Idx;
  Regs.writeById(static_cast<RegisterId>(Id), 0);

  auto CtlReg = Regs.readByIdAs<U64>(RegisterId::dr7);

  U64 ClearMask = (0b11 << (Idx * 2)) | (0b1111 << (Idx * 4 + 16));
  U64 Masked = CtlReg & ~ClearMask;

  Regs.writeById(RegisterId::dr7, Masked);
}

DDB::StopReason DDB::Process::stepInstruction() {
  std::optional<BreakpointSite *> ToReenable;
  VirtAddr PC = getPC();
  if (BreakpointSites.enabledAtAddr(PC)) {
    BreakpointSite &BS = BreakpointSites.getByAddr(PC);
    BS.disable();
    ToReenable = &BS;
  }

  if (ptrace(PTRACE_SINGLESTEP, Pid, nullptr, nullptr)) {
    Error::sendErrno("ptrace(PTRACE_SINGLESTEP)");
  }
  StopReason Reason = waitOnSignal();

  if (ToReenable) {
    (*ToReenable)->enable();
  }
  return Reason;
}

std::vector<std::byte> DDB::Process::readMemory(VirtAddr Addr,
                                                std::size_t NumBytes) const {
  std::vector<std::byte> Ret(NumBytes);

  const struct iovec LocalDesc = {.iov_base = Ret.data(),
                                  .iov_len = Ret.size()};

  std::vector<struct iovec> RemoteDescs;
  while (NumBytes > 0) {
    U64 UpToNextPage = 0x1000 - (Addr.asInt() & 0xfff); // FIXME: Page size?
    U64 ChunkSize = std::min(NumBytes, UpToNextPage);
    RemoteDescs.push_back({.iov_base = reinterpret_cast<void *>(Addr.asInt()),
                           .iov_len = ChunkSize});
    NumBytes -= ChunkSize;
    Addr += ChunkSize;
  }

  if (process_vm_readv(Pid, &LocalDesc, /*liovcnt=*/1, RemoteDescs.data(),
                       RemoteDescs.size(), /*flags=*/0) == -1) {
    Error::sendErrno("process_vm_readv");
  }
  return Ret;
}

std::vector<std::byte>
DDB::Process::readMemoryWithoutTraps(VirtAddr Addr,
                                     std::size_t NumBytes) const {
  std::vector<std::byte> Memory = readMemory(Addr, NumBytes);
  std::vector<BreakpointSite *> BPSites =
      BreakpointSites.getInRegion(Addr, Addr + NumBytes);
  for (BreakpointSite *BP : BPSites) {
    if (!BP->isEnabled() || BP->isHardware())
      continue;
    VirtAddr Offs = BP->addr() - Addr.asInt();
    Memory[Offs.asInt()] = BP->SavedData;
  }
  return Memory;
}

void DDB::Process::writeMemory(VirtAddr Addr,
                               Span<const std::byte> Data) const {
  // We cannot use 'process_vm_writev' here because this function doesn't
  // support writing to protected aread of memory like code segments.
  std::size_t NumWritten = 0;
  while (NumWritten < Data.size()) {
    std::size_t Rem = Data.size() - NumWritten;
    U64 Word;
    if (Rem >= 8) {
      Word = fromBytes<U64>(Data.begin() + NumWritten);
    } else {
      // If we have less than 8 bytes left, we need to handle a partial memory
      // write. This is because ptrace can only write exactly 8 bytes at a time.
      std::vector<std::byte> Read = readMemory(Addr + NumWritten, 8);
      auto WordData = reinterpret_cast<char *>(&Word);
      std::memcpy(WordData, Data.begin() + NumWritten, Rem);
      std::memcpy(WordData + Rem, Read.data() + Rem, 8 - Rem);
    }
    if (ptrace(PTRACE_POKEDATA, Pid, Addr + NumWritten, Word) == -1) {
      Error::sendErrno("Failed to write memory");
    }
    NumWritten += 8;
  }
}

void DDB::Process::readAllRegisters() {
  if (ptrace(PTRACE_GETREGS, Pid, nullptr, &getRegisters().Data.regs) == -1) {
    Error::sendErrno("ptrace(PTRACE_GETREGS)");
  }
  if (ptrace(PTRACE_GETFPREGS, Pid, nullptr, &getRegisters().Data.i387) == -1) {
    Error::sendErrno("ptrace(PTRACE_GETREGS)");
  }
  for (unsigned I = 0; I < 8; ++I) {
    auto Id = static_cast<int>(RegisterId::dr0) + I;
    RegisterInfo Info = registerInfoById(static_cast<RegisterId>(Id));

    errno = 0;
    U64 Data = ptrace(PTRACE_PEEKUSER, Pid, Info.Offset, nullptr);
    if (Data == -1 && errno != 0)
      Error::sendErrno("ptrace(PTRACE_PEEKUSER");

    getRegisters().Data.u_debugreg[I] = Data;
  }
}

int DDB::Process::setHardwareStoppoint(VirtAddr Addr, StoppointMode Mode,
                                       std::size_t Size) {
  Registers &Regs = getRegisters();

  auto CtlReg = Regs.readByIdAs<U64>(RegisterId::dr7);
  int FreeIdx = findFreeStoppointRegister(CtlReg);

  // The debug registers' IDs are sequential; the ID of DR1 is the one directly
  // following DR0, and so on.
  auto Id = static_cast<int>(RegisterId::dr0) + FreeIdx;
  Regs.writeById(static_cast<RegisterId>(Id), Addr.asInt());

  U64 ModeFlag = encodeHardwareStoppointMode(Mode);
  U64 SizeFlag = encodeHardwareStoppointSize(Size);

  U64 EnableBit = (1 << (FreeIdx * 2));
  U64 ModeBits = (ModeFlag << (FreeIdx * 4 + 16));
  U64 SizeBits = (SizeFlag << (FreeIdx * 4 + 18));

  U64 ClearMask = (0b11 << (FreeIdx * 2)) | (0b1111 << (FreeIdx * 4 + 16));
  U64 Masked = CtlReg & ~ClearMask;

  Masked |= EnableBit | ModeBits | SizeBits;
  Regs.writeById(RegisterId::dr7, Masked);

  return FreeIdx;
}
