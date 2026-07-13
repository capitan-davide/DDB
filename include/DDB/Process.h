#ifndef DDB_PROCESS_H
#define DDB_PROCESS_H

#include "DDB/Bit.h"
#include "DDB/BreakpointSite.h"
#include "DDB/Registers.h"
#include "DDB/StoppointCollection.h"
#include "DDB/Types.h"
#include "DDB/Watchpoint.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <sys/types.h>
#include <sys/user.h>

namespace DDB {

enum class ProcessState { Stopped, Running, Exited, Terminated };

struct StopReason {
  StopReason(int WaitStatus);

  ProcessState State;
  U8 Info;
};

class Process {
public:
  /// Launch a new process from the given executable path.
  /// @param Path Path to the executable to launch.
  /// @param Debug If true, the child process will be traced (ptraced).
  /// @param OutFD If set, the child's stdout will be redirected to this fd.
  /// @return A unique pointer to the newly created Process.
  static std::unique_ptr<Process> launch(std::filesystem::path Path,
                                         bool Debug = true,
                                         std::optional<int> OutFD = {});

  /// Attach to an already-running process by its PID.
  /// @param Pid The process ID to attach to.
  /// @return A unique pointer to the attached Process.
  static std::unique_ptr<Process> attach(pid_t Pid);

  Process() = delete;
  Process(const Process &) = delete;
  Process(Process &&) = delete;
  Process &operator=(const Process &) = delete;
  Process &operator=(Process &&) = delete;
  ~Process();

  /// Resume execution of the traced process.
  /// @throws Error if the process is not stopped (e.g., not traced, already
  ///         running, or has exited).
  void resume();

  /// Terminate the traced process.
  void terminate();

  /// Block until a process terminates. If the process is being traced, also
  /// returns when the process stops due to a signal (e.g., SIGTRAP).
  /// @return A StopReason struct describing how the process stopped or
  ///         terminated.
  /// @throws Error if the operation fails.
  StopReason waitOnSignal();

  /// Write a word to the tracee's user area at the given offset.
  /// @param Offset Byte offset into the user area (must be word-aligned).
  /// @param Data The word to write.
  /// @throws Error if the operation fails.
  void writeUserArea(std::size_t Offset, U64 Data);

  /// Write all floating-point registers to the tracee.
  /// @param FPRs The floating-point register state to write.
  /// @throws Error if the operation fails.
  void writeFPRs(const user_fpregs_struct &FPRs);

  /// Write all general-purpose registers to the tracee.
  /// @param GPRs The general-purpose register state to write.
  /// @throws Error if the operation fails.
  void writeGPRs(const user_regs_struct &GPRs);

  /// TODO: Write documentation.
  BreakpointSite &createBreakpointSite(VirtAddr Addr, bool Hardware = false,
                                       bool Internal = false);
  Watchpoint &createWatchpoint(VirtAddr Addr, StoppointMode Mode,
                               std::size_t Size);

  int setHardwareBreakpoint(BreakpointSite::IdType Id, VirtAddr Addr);
  int setWatchpoint(Watchpoint::IdType Id, VirtAddr Addr, StoppointMode Mode,
                    std::size_t Size);

  void clearHardwareStoppoint(int Idx);

  /// Step over a single machine instruction.
  /// @return A StopReason struct describing why the process stopped after
  ///         stepping.
  StopReason stepInstruction();

  std::vector<std::byte> readMemory(VirtAddr Addr, std::size_t NumBytes) const;
  std::vector<std::byte> readMemoryWithoutTraps(VirtAddr Addr,
                                                std::size_t NumBytes) const;
  void writeMemory(VirtAddr Addr, Span<const std::byte> Data) const;

  template <class T> T readMemoryAs(VirtAddr Addr) const {
    std::vector<std::byte> Data = readMemory(Addr, sizeof(T));
    return fromBytes<T>(Data.data());
  }

  pid_t pid() const { return Pid; }
  ProcessState state() const { return State; }
  Registers &getRegisters() { return *TheRegisters; }
  const Registers &getRegisters() const { return *TheRegisters; }

  VirtAddr getPC() const {
    return VirtAddr(getRegisters().readByIdAs<U64>(RegisterId::rip));
  }
  void setPC(VirtAddr Addr) {
    getRegisters().writeById(RegisterId::rip, Addr.asInt());
  }

  StoppointCollection<BreakpointSite> &breakpointSites() {
    return BreakpointSites;
  }
  const StoppointCollection<BreakpointSite> &breakpointSites() const {
    return BreakpointSites;
  }

  StoppointCollection<Watchpoint> &watchpoints() { return Watchpoints; }
  const StoppointCollection<Watchpoint> &watchpoints() const {
    return Watchpoints;
  }

private:
  Process(pid_t Pid, bool TermOnEnd, bool IsAttached)
      : Pid(Pid), TermOnEnd(TermOnEnd), IsAttached(IsAttached),
        TheRegisters(new Registers(*this)) {}

  void readAllRegisters();

  int setHardwareStoppoint(VirtAddr Addr, StoppointMode Mode, std::size_t Size);

  /// The process ID of this process.
  pid_t Pid;

  /// If true, this process will be killed on destruction.
  bool TermOnEnd;

  /// If true, this process is being traced.
  bool IsAttached;

  /// The current state of this process.
  ProcessState State = ProcessState::Stopped;

  /// The register set of this process.
  std::unique_ptr<Registers> TheRegisters;

  StoppointCollection<BreakpointSite> BreakpointSites;
  StoppointCollection<Watchpoint> Watchpoints;
};

} // namespace DDB

#endif // DDB_PROCESS_H
