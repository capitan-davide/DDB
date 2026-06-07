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

  bool isEnabled() const { return m_isEnabled; }

  IdType id() const { return m_id; }
  VirtAddr addr() const { return m_addr; }

  bool atAddr(VirtAddr addr) const { return m_addr == addr; }
  bool inRange(VirtAddr low, VirtAddr high) const {
    return low <= m_addr && m_addr < high;
  }

private:
  BreakpointSite(Process &proc, VirtAddr addr);
  friend Process;

  IdType m_id;
  Process *m_proc;
  VirtAddr m_addr;
  bool m_isEnabled;
  std::byte m_savedData;
};
} // namespace DDB

#endif // DDB_BREAKPOINT_SITE_H
