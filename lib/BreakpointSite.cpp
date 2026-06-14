#include "DDB/BreakpointSite.h"
#include "DDB/Error.h"
#include "DDB/Process.h"
#include "DDB/Types.h"

#include <cerrno>
#include <cstddef>

#include <sys/ptrace.h>

namespace {
DDB::BreakpointSite::IdType getNextId() {
  static DDB::BreakpointSite::IdType id = 0;
  return ++id;
}
} // namespace

DDB::BreakpointSite::BreakpointSite(Process &proc, VirtAddr addr,
                                    bool isHardware, bool isInternal)
    : m_proc(&proc), m_addr(addr), m_isEnabled(false), m_savedData(),
      m_isHardware(isHardware), m_isInternal(isInternal) {
  m_id = m_isInternal ? -1 : getNextId();
}

void DDB::BreakpointSite::enable() {
  if (m_isEnabled)
    return;

  if (m_isHardware) {
    m_hardwareRegisterIdx = m_proc->setHardwareBreakpoint(m_id, m_addr);
  } else {
    // Read 64 bits of data from the address at which we need to set the
    // breakpoint.
    errno = 0;
    U64 data = ptrace(PTRACE_PEEKDATA, m_proc->pid(), addr(), nullptr);
    if (data == -1 && errno != 0) {
      Error::sendErrno("ptrace(PTRACE_PEEKDATA)");
    }

    // We are saving the first 8 bits here because we are gonna patch them with
    // an 'int3' instruction next.
    m_savedData = static_cast<std::byte>(data & 0xff);

    constexpr U64 int3 = 0xcc;
    U64 dataWithInt3 = ((data & ~0xff) | int3);

    if (ptrace(PTRACE_POKEDATA, m_proc->pid(), addr(), dataWithInt3) == -1) {
      Error::sendErrno("ptrace(PTRACE_POKEDATA)");
    }
  }

  m_isEnabled = true;
}

void DDB::BreakpointSite::disable() {
  if (!m_isEnabled)
    return;

  if (m_isHardware) {
    m_proc->clearHardwareStoppoint(m_hardwareRegisterIdx);
    m_hardwareRegisterIdx = -1;
  } else {
    errno = 0;
    U64 data = ptrace(PTRACE_PEEKDATA, m_proc->pid(), addr(), nullptr);
    if (data == -1 && errno != 0) {
      Error::sendErrno("ptrace(PTRACE_PEEKDATA)");
    }

    // Restore the original data, removing the 'int3' istructions that was
    // patched in when this breakpoint was enabled.
    U64 restoredData = ((data & ~0xff) | static_cast<U8>(m_savedData));

    if (ptrace(PTRACE_POKEDATA, m_proc->pid(), addr(), restoredData) == -1) {
      Error::sendErrno("ptrace(PTRACE_POKEDATA)");
    }
  }

  m_isEnabled = false;
}
