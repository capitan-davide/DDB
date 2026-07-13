#ifndef DDB_WATCHPOINT_H
#define DDB_WATCHPOINT_H

#include "DDB/Types.h"

namespace DDB {
class Process;

class Watchpoint {
public:
  using IdType = I32;

  Watchpoint() = delete;
  Watchpoint(const Watchpoint &) = delete;
  Watchpoint(Watchpoint &&) = delete;
  Watchpoint &operator=(const Watchpoint &) = delete;
  Watchpoint &operator=(Watchpoint &&) = delete;

  IdType id() const { return Id; }

  void enable();
  void disable();

  bool isEnabled() const { return IsEnabled; }

  VirtAddr addr() const { return Addr; }
  StoppointMode mode() const { return Mode; }
  std::size_t size() const { return Size; }

  bool atAddr(VirtAddr Addr) const { return this->Addr == Addr; }
  bool inRange(VirtAddr Low, VirtAddr High) const {
    return Low <= Addr && Addr < High;
  }

private:
  friend Process;
  Watchpoint(Process &Proc, VirtAddr Addr, StoppointMode Mode,
             std::size_t Size);

  IdType Id;
  Process *Proc;
  VirtAddr Addr;
  StoppointMode Mode;
  std::size_t Size;
  bool IsEnabled;
  int HardwareRegisterIdx = -1;
};
} // namespace DDB

#endif // DDB_WATCHPOINT_H
