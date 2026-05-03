#ifndef DDB_PROCESS_H
#define DDB_PROCESS_H

#include "DDB/Types.h"

#include <filesystem>
#include <memory>

#include <sys/types.h>

namespace DDB {

enum class ProcessState { Stopped, Running, Exited, Terminated };

struct StopReason {
  StopReason(int waitStatus);

  ProcessState state;
  U8 info;
};

class Process {
public:
  static std::unique_ptr<Process> launch(std::filesystem::path path, bool debug = true);
  static std::unique_ptr<Process> attach(pid_t pid);

  Process() = delete;
  Process(const Process &) = delete;
  Process(Process &&) = delete;
  Process &operator=(const Process &) = delete;
  Process &operator=(Process &&) = delete;
  ~Process();

  void resume();
  StopReason waitOnSignal();

  pid_t pid() const { return m_pid; }
  ProcessState state() const { return m_state; }

private:
  Process(pid_t pid, bool termOnEnd, bool isAttached)
      : m_pid(pid), m_termOnEnd(termOnEnd), m_isAttached(isAttached) {}

  pid_t m_pid;
  bool m_termOnEnd;
  bool m_isAttached;
  ProcessState m_state = ProcessState::Stopped;
};

} // namespace DDB

#endif // DDB_PROCESS_H
