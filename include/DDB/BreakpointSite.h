#ifndef DDB_BREAKPOINT_SITE_H
#define DDB_BREAKPOINT_SITE_H

#include "DDB/Types.h"

#include <cstddef>

namespace DDB {
class Process;

class BreakpointSite {
public:
  using IdType = I32;

  BreakpointSite() = delete;
  BreakpointSite(const BreakpointSite &) = delete;
  BreakpointSite(BreakpointSite &&) = delete;
  BreakpointSite &operator=(const BreakpointSite &) = delete;
  BreakpointSite &operator=(BreakpointSite &&) = delete;

  void enable();
  void disable();

  bool isEnabled() const { return IsEnabled; }
  bool isHardware() const { return IsHardware; }
  bool isInternal() const { return IsInternal; }

  IdType id() const { return Id; }
  VirtAddr addr() const { return Addr; }

  bool atAddr(VirtAddr Addr) const { return this->Addr == Addr; }
  bool inRange(VirtAddr Low, VirtAddr High) const {
    return Low <= Addr && Addr < High;
  }

private:
  BreakpointSite(Process &Proc, VirtAddr Addr, bool IsHardware = false,
                 bool IsInternal = false);
  friend Process;

  IdType Id;
  Process *Proc;
  VirtAddr Addr;
  bool IsEnabled;
  std::byte SavedData;
  bool IsHardware;
  bool IsInternal;
  int HardwareRegisterIdx = -1;
};
} // namespace DDB

#endif // DDB_BREAKPOINT_SITE_H
