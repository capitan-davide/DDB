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

  IdType id() const { return m_id; }

  void enable();
  void disable();

  bool isEnabled() const { return m_isEnabled; }

  VirtAddr addr() const { return m_addr; }
  StoppointMode mode() const { return m_mode; }
  std::size_t size() const { return m_size; }

  bool atAddr(VirtAddr addr) const { return m_addr == addr; }
  bool inRange(VirtAddr low, VirtAddr high) const {
    return low <= m_addr && m_addr < high;
  }

private:
  friend Process;
  Watchpoint(Process &proc, VirtAddr addr, StoppointMode mode,
             std::size_t size);

  IdType m_id;
  Process *m_proc;
  VirtAddr m_addr;
  StoppointMode m_mode;
  std::size_t m_size;
  bool m_isEnabled;
  int m_hardwareRegisterIdx = -1;
};
} // namespace DDB

#endif // DDB_WATCHPOINT_H
