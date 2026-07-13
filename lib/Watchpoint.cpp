#include "DDB/Watchpoint.h"
#include "DDB/Error.h"
#include "DDB/Process.h"
#include "DDB/Types.h"

#include <cstddef>

namespace {
DDB::Watchpoint::IdType getNextId() {
  static DDB::Watchpoint::IdType NextId = 0;
  return ++NextId;
}
} // namespace

DDB::Watchpoint::Watchpoint(Process &Proc, VirtAddr Addr, StoppointMode Mode,
                            std::size_t Size)
    : Proc(&Proc), Addr(Addr), Mode(Mode), Size(Size), IsEnabled(false) {
  Id = getNextId();
}

void DDB::Watchpoint::enable() {
  if (IsEnabled)
    return;
  HardwareRegisterIdx = Proc->setWatchpoint(Id, Addr, Mode, Size);
  IsEnabled = true;
}

void DDB::Watchpoint::disable() {
  if (!IsEnabled)
    return;
  Proc->clearHardwareStoppoint(HardwareRegisterIdx);
  IsEnabled = false;
}
