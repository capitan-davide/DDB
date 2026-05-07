#include "DDB/Bit.h"
#include "DDB/Error.h"
#include "DDB/Pipe.h"
#include "DDB/Process.h"
#include "DDB/RegisterInfo.h"
#include "DDB/Registers.h"
#include "DDB/Types.h"

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

TEST_CASE("Process::resume not traced", "[Process]") {
  auto proc = Process::launch("targets/run-endlessly", false);
  REQUIRE_THROWS_AS(proc->resume(), Error);
}

TEST_CASE("Process::resume already terminated", "[Process]") {
  auto proc = Process::launch("targets/end-immediately");
  proc->resume();
  proc->waitOnSignal();
  REQUIRE_THROWS_AS(proc->resume(), Error);
}

TEST_CASE("Process::writeUserArea not traced", "[Process]") {
  auto proc = Process::launch("targets/run-endlessly", false);
  REQUIRE_THROWS_AS(proc->writeUserArea(0, 0), Error);
}

TEST_CASE("Write registers works", "[Register]") {
  DDB::Pipe pipe(/*closeOnExec=*/false);

  auto proc = Process::launch("targets/reg-write", /*debug=*/true,
                              /*outFd=*/pipe.getWrite());
  pipe.closeWrite();

  proc->resume();
  proc->waitOnSignal();

  Registers &regs = proc->getRegisters();
  regs.writeById(RegisterId::rsi, 0xcafebabe);

  proc->resume();
  proc->waitOnSignal();

  auto out = pipe.read();
  REQUIRE(toStringView(out) == "0xcafebabe");

  regs.writeById(RegisterId::mm0, 0xba5eba11);

  proc->resume();
  proc->waitOnSignal();

  out = pipe.read();
  REQUIRE(toStringView(out) == "0xba5eba11");

  regs.writeById(RegisterId::xmm0, 42.24);

  proc->resume();
  proc->waitOnSignal();

  out = pipe.read();
  REQUIRE(toStringView(out) == "42.24");

  regs.writeById(RegisterId::st0, 42.24l);

  // The status word tracks the current size of the FPU stack and reports
  // errors. It's 16 bits wide and bits 11 through 13 track the top of the
  // stack. Its value starts at index 0 and goes down instead of up, wrapping
  // around 7. So, to push a value to the stack, we set bits 11 through 13 to
  // 0b111.
  regs.writeById(RegisterId::fsw, U16(0b00111000'00000000));

  // The tag register tracks which of the 'st' registers are valid, empty, or
  // special (i.e., NaNs or infinity). A tag of 0b11 means empty, 0b00 means
  // valid. We want to set the first tag to 0b00 and the rest to 0b11.
  regs.writeById(RegisterId::ftw, U16(0b00111111'11111111));

  proc->resume();
  proc->waitOnSignal();

  out = pipe.read();
  REQUIRE(toStringView(out) == "42.24");
}

TEST_CASE("Read register works", "[Register]") {
  auto proc = Process::launch("targets/reg-read");

  Registers &regs = proc->getRegisters();

  proc->resume();
  proc->waitOnSignal();

  REQUIRE(regs.readByIdAs<U64>(RegisterId::r13) == 0xcafebabe); // FIXME!!!

  proc->resume();
  proc->waitOnSignal();

  REQUIRE(regs.readByIdAs<U8>(RegisterId::r13b) == 42);

  proc->resume();
  proc->waitOnSignal();

  REQUIRE(regs.readByIdAs<Byte64>(RegisterId::mm0) == toByte64(0xba5eba11));

  proc->resume();
  proc->waitOnSignal();

  REQUIRE(regs.readByIdAs<Byte128>(RegisterId::xmm0) == toByte128(64.125));

  proc->resume();
  proc->waitOnSignal();

  REQUIRE(regs.readByIdAs<F128>(RegisterId::st0) == 64.125L);
}
