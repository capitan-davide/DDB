#include "DDB/Bit.h"
#include "DDB/BreakpointSite.h"
#include "DDB/Error.h"
#include "DDB/Pipe.h"
#include "DDB/Process.h"
#include "DDB/RegisterInfo.h"
#include "DDB/Registers.h"
#include "DDB/Types.h"

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

#include <signal.h>
#include <sys/types.h>

#include <elf.h>

#include <catch2/catch_test_macros.hpp>

using namespace DDB;

namespace {
bool processExists(pid_t pid) {
  int ret = kill(pid, 0);
  return ret != -1 || errno != ESRCH;
}

char getProcessStatus(pid_t pid) {
  std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
  std::string line;
  std::getline(stat, line);
  std::size_t lastParenPos = line.rfind(')');
  std::size_t statusCharPos = lastParenPos + 2;
  return line[statusCharPos];
}

U64 getSectionLoadBias(std::filesystem::path path, Elf64_Addr fileAddr) {
  std::string cmd = "readelf -WS " + path.string();
  std::FILE *pipe = ::popen(cmd.c_str(), "r");

  std::regex textRefex(R"(PROGBITS\s+(\w+)\s+(\w+)\s+(\w+))");
  char *line = nullptr;
  std::size_t len = 0;
  while (::getline(&line, &len, pipe) != -1) {
    std::cmatch groups;
    if (std::regex_search(line, groups, textRefex)) {
      long addr = std::stol(groups[1], nullptr, 16);
      long offs = std::stol(groups[2], nullptr, 16);
      long size = std::stol(groups[3], nullptr, 16);
      if (addr <= fileAddr && fileAddr < (addr + size)) {
        ::free(line);
        ::pclose(pipe);
        return addr - offs;
      }
    }
    ::free(line);
    line = nullptr;
  }

  ::pclose(pipe);
  Error::send("Could not find section load bias");
}

U64 getEntryPointOffset(std::filesystem::path path) {
  std::ifstream elfFile(path);

  Elf64_Ehdr header;
  elfFile.read(reinterpret_cast<char *>(&header), sizeof(header));

  Elf64_Addr entryFileAddr = header.e_entry;
  U64 loadBias = getSectionLoadBias(path, entryFileAddr);
  return entryFileAddr - loadBias;
}

VirtAddr getLoadAddr(pid_t pid, U64 offs) {
  std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
  std::regex mapRegex(R"((\w+)-\w+ ..(.). (\w+))");

  std::string data;
  while (std::getline(maps, data)) {
    std::smatch groups;
    std::regex_search(data, groups, mapRegex);

    if (groups[2] == 'x') {
      long lowRange = std::stol(groups[1], nullptr, 16);
      long fileOffs = std::stol(groups[3], nullptr, 16);
      return VirtAddr(offs - fileOffs + lowRange);
    }
  }
  Error::send("Could not find load address");
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
  Pipe pipe(/*closeOnExec=*/false);

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

TEST_CASE("Can create breakpoint site", "[Breakpoint]") {
  auto proc = Process::launch("targets/run-endlessly");
  BreakpointSite &bs = proc->createBreakpointSite(VirtAddr(42));
  REQUIRE(bs.addr().asInt() == 42);
}

TEST_CASE("Breakpointside IDs increase", "[Breakpont]") {
  auto proc = Process::launch("targets/run-endlessly");

  auto &bs1 = proc->createBreakpointSite(VirtAddr(42));
  REQUIRE(bs1.addr().asInt() == 42);

  auto &bs2 = proc->createBreakpointSite(VirtAddr(43));
  REQUIRE(bs2.id() == bs1.id() + 1);

  auto &bs3 = proc->createBreakpointSite(VirtAddr(44));
  REQUIRE(bs3.id() == bs2.id() + 1);

  auto &bs4 = proc->createBreakpointSite(VirtAddr(45));
  REQUIRE(bs4.id() == bs3.id() + 1);
}

TEST_CASE("Can find breakpoint site", "[Breakpoint]") {
  auto proc = Process::launch("targets/run-endlessly");
  const auto &cproc = proc;

  proc->createBreakpointSite(VirtAddr(42));
  proc->createBreakpointSite(VirtAddr(43));
  proc->createBreakpointSite(VirtAddr(44));
  proc->createBreakpointSite(VirtAddr(45));

  auto &s1 = proc->breakpointSites().getByAddr(VirtAddr(44));
  REQUIRE(proc->breakpointSites().containsAddr(VirtAddr(44)));
  REQUIRE(s1.addr().asInt() == 44);

  auto &cs1 = cproc->breakpointSites().getByAddr(VirtAddr(44));
  REQUIRE(cproc->breakpointSites().containsAddr(VirtAddr(44)));
  REQUIRE(cs1.addr().asInt() == 44);

  auto &s2 = proc->breakpointSites().getById(s1.id() + 1);
  REQUIRE(proc->breakpointSites().containsId(s1.id() + 1));
  REQUIRE(s2.id() == s1.id() + 1);
  REQUIRE(s2.addr().asInt() == 45);

  auto &cs2 = cproc->breakpointSites().getById(cs1.id() + 1);
  REQUIRE(cproc->breakpointSites().containsId(cs1.id() + 1));
  REQUIRE(cs2.id() == cs1.id() + 1);
  REQUIRE(cs2.addr().asInt() == 45);
}

TEST_CASE("Cannot find breakpoint site", "[Breakpoint]") {
  auto proc = Process::launch("targets/run-endlessly");
  const auto &cproc = proc;

  REQUIRE_THROWS_AS(proc->breakpointSites().getByAddr(VirtAddr(44)), Error);
  REQUIRE_THROWS_AS(proc->breakpointSites().getById(44), Error);
  REQUIRE_THROWS_AS(cproc->breakpointSites().getByAddr(VirtAddr(44)), Error);
  REQUIRE_THROWS_AS(cproc->breakpointSites().getById(44), Error);
}

TEST_CASE("Breakpoint site list size and emptiness", "[Breakpont]") {
  auto proc = Process::launch("targets/run-endlessly");
  const auto &cproc = proc;

  REQUIRE(proc->breakpointSites().empty());
  REQUIRE(proc->breakpointSites().size() == 0);
  REQUIRE(cproc->breakpointSites().empty());
  REQUIRE(cproc->breakpointSites().size() == 0);

  proc->createBreakpointSite(VirtAddr(42));
  REQUIRE(!proc->breakpointSites().empty());
  REQUIRE(proc->breakpointSites().size() == 1);
  REQUIRE(!cproc->breakpointSites().empty());
  REQUIRE(cproc->breakpointSites().size() == 1);

  proc->createBreakpointSite(VirtAddr(43));
  REQUIRE(!proc->breakpointSites().empty());
  REQUIRE(proc->breakpointSites().size() == 2);
  REQUIRE(!cproc->breakpointSites().empty());
  REQUIRE(cproc->breakpointSites().size() == 2);
}

TEST_CASE("Can iterate breakpoint sites", "[Breakpont]") {
  auto proc = Process::launch("targets/run-endlessly");
  const auto &cproc = proc;

  proc->createBreakpointSite(VirtAddr(42));
  proc->createBreakpointSite(VirtAddr(43));
  proc->createBreakpointSite(VirtAddr(44));
  proc->createBreakpointSite(VirtAddr(45));

  proc->breakpointSites().forEach(
      [addr = 42](auto &bs) mutable { REQUIRE(bs.addr().asInt() == addr++); });

  cproc->breakpointSites().forEach(
      [addr = 42](auto &bs) mutable { REQUIRE(bs.addr().asInt() == addr++); });
}

TEST_CASE("Breakpoint on address works", "[Breakpoint]") {
  Pipe pipe(/*closeOnExec=*/false);

  auto proc = Process::launch("targets/hello-ddb", /*debug=*/true,
                              /*outFd=*/pipe.getWrite());
  pipe.closeWrite();

  U64 offs = getEntryPointOffset("targets/hello-ddb");
  VirtAddr loadAddr = getLoadAddr(proc->pid(), offs);

  proc->createBreakpointSite(loadAddr).enable();
  proc->resume();
  StopReason reason = proc->waitOnSignal();

  REQUIRE(reason.state == ProcessState::Stopped);
  REQUIRE(reason.info == SIGTRAP);
  REQUIRE(proc->getPC() == loadAddr);

  proc->resume();
  reason = proc->waitOnSignal();

  REQUIRE(reason.state == ProcessState::Exited);
  REQUIRE(reason.info == 0);

  std::vector<std::byte> data = pipe.read();
  REQUIRE(toStringView(data) == "Hello, DDB!\n");
}

TEST_CASE("Can remove breakpoint sites", "[Breakpoint]") {
  auto proc = Process::launch("targets/run-endlessly");

  BreakpointSite &bs = proc->createBreakpointSite(VirtAddr(42));
  proc->createBreakpointSite(VirtAddr(43));
  REQUIRE(proc->breakpointSites().size() == 2);

  proc->breakpointSites().removeById(bs.id());
  proc->breakpointSites().removeByAddr(VirtAddr(43));
  REQUIRE(proc->breakpointSites().empty());
}

TEST_CASE("Reading and writing memory works", "[Memory]") {
  Pipe pipe(/*closeOnExec=*/true);
  auto proc = Process::launch("targets/memory", /*debug=*/true,
                              /*outFd=*/pipe.getWrite());
  pipe.closeWrite();

  proc->resume();
  proc->waitOnSignal();

  auto aAddr = fromBytes<U64>(pipe.read().data());
  std::vector<std::byte> aVec = proc->readMemory(VirtAddr(aAddr), 8);
  auto a = fromBytes<U64>(aVec.data());
  REQUIRE(a == 0xcafebabe);

  proc->resume();
  proc->waitOnSignal();

  auto bAddr = fromBytes<U64>(pipe.read().data());
  proc->writeMemory(VirtAddr(bAddr), {asBytes("Hello, DDB!"), 12});

  proc->resume();
  proc->waitOnSignal();

  std::vector<std::byte> b = pipe.read();
  REQUIRE(toStringView(b) == "Hello, DDB!");
}
