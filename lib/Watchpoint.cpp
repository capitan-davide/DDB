#include "DDB/Watchpoint.h"
#include "DDB/Error.h"
#include "DDB/Process.h"
#include "DDB/Types.h"

#include <cstddef>

namespace {
DDB::Watchpoint::IdType getNextId() {
  static DDB::Watchpoint::IdType s_id = 0;
  return ++s_id;
}
} // namespace

DDB::Watchpoint::Watchpoint(Process &proc, VirtAddr addr, StoppointMode mode,
                            std::size_t size)
    : m_proc(&proc), m_addr(addr), m_mode(mode), m_size(size) {
  m_id = getNextId();
}

void DDB::Watchpoint::enable() {
  if (m_isEnabled)
    return;
  // TODO: m_proc->setWatchpoint(...)
  m_isEnabled = true;
}

void DDB::Watchpoint::disable() {
  if (!m_isEnabled)
    return;
  m_proc->clearHardwareStoppoint(m_hardwareRegisterIdx);
  m_isEnabled = false;
}
