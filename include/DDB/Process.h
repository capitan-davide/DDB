#ifndef DDB_PROCESS_H
#define DDB_PROCESS_H

#include "DDB/BreakpointSite.h"
#include "DDB/Registers.h"
#include "DDB/StoppointCollection.h"
#include "DDB/Types.h"

#include <filesystem>
#include <memory>
#include <optional>

#include <sys/types.h>
#include <sys/user.h>

namespace DDB {

enum class ProcessState { Stopped, Running, Exited, Terminated };

struct StopReason {
  StopReason(int waitStatus);

  ProcessState state;
  U8 info;
};

class Process {
public:
  /// Launch a new process from the given executable path.
  /// @param path Path to the executable to launch.
  /// @param debug If true, the child process will be traced (ptraced).
  /// @param outFd If set, the child's stdout will be redirected to this fd.
  /// @return A unique pointer to the newly created Process.
  static std::unique_ptr<Process> launch(std::filesystem::path path,
                                         bool debug = true,
                                         std::optional<int> outFd = {});

  /// Attach to an already-running process by its PID.
  /// @param pid The process ID to attach to.
  /// @return A unique pointer to the attached Process.
  static std::unique_ptr<Process> attach(pid_t pid);

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
  /// @param offset Byte offset into the user area (must be word-aligned).
  /// @param data The word to write.
  /// @throws Error if the operation fails.
  void writeUserArea(std::size_t offset, U64 data);

  /// Write all floating-point registers to the tracee.
  /// @param fprs The floating-point register state to write.
  /// @throws Error if the operation fails.
  void writeFPRs(const user_fpregs_struct &fprs);

  /// Write all general-purpose registers to the tracee.
  /// @param gprs The general-purpose register state to write.
  /// @throws Error if the operation fails.
  void writeGPRs(const user_regs_struct &gprs);

  /// TODO: Write documentation.
  BreakpointSite &createBreakpointSite(VirtAddr addr);

  /// Step over a single machine instruction.
  /// @return A StopReason struct describing why the process stopped after
  ///         stepping.
  StopReason stepInstruction();

  pid_t pid() const { return m_pid; }
  ProcessState state() const { return m_state; }
  Registers &getRegisters() { return *m_registers; }
  const Registers &getRegisters() const { return *m_registers; }
  VirtAddr getPC() const {
    return VirtAddr(getRegisters().readByIdAs<U64>(RegisterId::rip));
  }
  void setPC(VirtAddr addr) {
    getRegisters().writeById(RegisterId::rip, addr.asInt());
  }

  StoppointCollection<BreakpointSite> &breakpointSites() {
    return m_breakpointSites;
  }
  const StoppointCollection<BreakpointSite> &breakpointSites() const {
    return m_breakpointSites;
  }

private:
  Process(pid_t pid, bool termOnEnd, bool isAttached)
      : m_pid(pid), m_termOnEnd(termOnEnd), m_isAttached(isAttached),
        m_registers(new Registers(*this)) {}

  void readAllRegisters();

  /// The process ID of this process.
  pid_t m_pid;

  /// If true, this process will be killed on destruction.
  bool m_termOnEnd;

  /// If true, this process is being traced.
  bool m_isAttached;

  /// The current state of this process.
  ProcessState m_state = ProcessState::Stopped;

  /// The register set of this process.
  std::unique_ptr<Registers> m_registers;

  StoppointCollection<BreakpointSite> m_breakpointSites;
};

} // namespace DDB

#endif // DDB_PROCESS_H
