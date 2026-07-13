#include "DDB/BreakpointSite.h"
#include "DDB/Error.h"
#include "DDB/Process.h"
#include "DDB/Types.h"

#include <cerrno>
#include <cstddef>

#include <sys/ptrace.h>

namespace {
DDB::BreakpointSite::IdType getNextId() {
  static DDB::BreakpointSite::IdType NextId = 0;
  return ++NextId;
}
} // namespace

DDB::BreakpointSite::BreakpointSite(Process &Proc, VirtAddr Addr,
                                    bool IsHardware, bool IsInternal)
    : Proc(&Proc), Addr(Addr), IsEnabled(false), SavedData(),
      IsHardware(IsHardware), IsInternal(IsInternal) {
  Id = IsInternal ? -1 : getNextId();
}

void DDB::BreakpointSite::enable() {
  if (IsEnabled)
    return;

  if (IsHardware) {
    HardwareRegisterIdx = Proc->setHardwareBreakpoint(Id, Addr);
  } else {
    // Read 64 bits of data from the address at which we need to set the
    // breakpoint.
    errno = 0;
    U64 Data = ptrace(PTRACE_PEEKDATA, Proc->pid(), addr(), nullptr);
    if (Data == -1 && errno != 0) {
      Error::sendErrno("ptrace(PTRACE_PEEKDATA)");
    }

    // We are saving the first 8 bits here because we are gonna patch them with
    // an 'int3' instruction next.
    SavedData = static_cast<std::byte>(Data & 0xff);

    constexpr U64 Int3 = 0xcc;
    U64 DataWithInt3 = ((Data & ~0xff) | Int3);

    if (ptrace(PTRACE_POKEDATA, Proc->pid(), addr(), DataWithInt3) == -1) {
      Error::sendErrno("ptrace(PTRACE_POKEDATA)");
    }
  }

  IsEnabled = true;
}

void DDB::BreakpointSite::disable() {
  if (!IsEnabled)
    return;

  if (IsHardware) {
    Proc->clearHardwareStoppoint(HardwareRegisterIdx);
    HardwareRegisterIdx = -1;
  } else {
    errno = 0;
    U64 Data = ptrace(PTRACE_PEEKDATA, Proc->pid(), addr(), nullptr);
    if (Data == -1 && errno != 0) {
      Error::sendErrno("ptrace(PTRACE_PEEKDATA)");
    }

    // Restore the original data, removing the 'int3' istructions that was
    // patched in when this breakpoint was enabled.
    U64 RestoredData = ((Data & ~0xff) | static_cast<U8>(SavedData));

    if (ptrace(PTRACE_POKEDATA, Proc->pid(), addr(), RestoredData) == -1) {
      Error::sendErrno("ptrace(PTRACE_POKEDATA)");
    }
  }

  IsEnabled = false;
}
