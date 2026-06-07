#include "DDB/BreakpointSite.h"
#include "DDB/Process.h"

namespace {
DDB::BreakpointSite::IdType getNextId() {
  static DDB::BreakpointSite::IdType id = 0;
  return ++id;
}
} // namespace

DDB::BreakpointSite::BreakpointSite(Process &proc, VirtAddr addr) :
  m_proc(&proc), m_addr(addr), m_isEnabled(false), m_savedData() {
  m_id = getNextId();
}
