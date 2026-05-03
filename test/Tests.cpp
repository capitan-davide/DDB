#include "DDB/Error.h"
#include "DDB/Process.h"

#include <cerrno>
#include <fstream>
#include <string>

#include <signal.h>
#include <sys/types.h>

#include <catch2/catch_test_macros.hpp>

using namespace DDB;

namespace {
bool processExists(pid_t pid) {
  int ret = kill(pid, 0);
  return ret != -1 && errno != ESRCH;
}

char getProcessStatus(pid_t pid) {
  std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
  std::string line;
  std::getline(stat, line);
  std::size_t lastParenPos = line.rfind(')');
  std::size_t statusCharPos = lastParenPos + 2;
  return line[statusCharPos];
}
} // namespace

TEST_CASE("Process::launch success", "[Process]") {
  auto proc = Process::launch("yes");
  REQUIRE(processExists(proc->pid()));
}

TEST_CASE("Process::launch no such program", "[Process]") {
  REQUIRE_THROWS_AS(Process::launch("a_program_that_does_not_exists"), Error);
}

TEST_CASE("Process::attach success", "[Process]") {
  auto target = Process::launch("targets/run-endlessly", false);
  auto proc = Process::attach(target->pid());
  REQUIRE(getProcessStatus(target->pid()) == 't');
}

TEST_CASE("Process::attach invalid PID", "[Process]") {
  REQUIRE_THROWS_AS(Process::attach(0), Error);
}

TEST_CASE("Process::resume success", "[Process]") {
  {
    auto proc = Process::launch("targets/run-endlessly");
    proc->resume();
    char status = getProcessStatus(proc->pid());
    REQUIRE((status == 'R' || status == 'S'));
  }
  {
    auto target = Process::launch("targets/run-endlessly", false);
    auto proc = Process::attach(target->pid());
    proc->resume();
    char status = getProcessStatus(proc->pid());
    REQUIRE((status == 'R' || status == 'S'));
  }
}

TEST_CASE("Process::result already terminated", "[Process]") {
  auto proc = Process::launch("targets/end-immediately");
  proc->resume();
  proc->waitOnSignal();
  REQUIRE_THROWS_AS(proc->resume(), Error);
}
