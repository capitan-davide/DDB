#include "DDB/Bit.h"
#include "DDB/BreakpointSite.h"
#include "DDB/Error.h"
#include "DDB/Pipe.h"
#include "DDB/Process.h"
#include "DDB/RegisterInfo.h"
#include "DDB/Registers.h"
#include "DDB/Types.h"
#include "DDB/Watchpoint.h"

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
bool processExists(pid_t Pid) {
  int Ret = kill(Pid, 0);
  return Ret != -1 || errno != ESRCH;
}

char getProcessStatus(pid_t Pid) {
  std::ifstream Stat("/proc/" + std::to_string(Pid) + "/stat");
  std::string Line;
  std::getline(Stat, Line);
  std::size_t LastParenPos = Line.rfind(')');
  std::size_t StatusCharPos = LastParenPos + 2;
  return Line[StatusCharPos];
}

U64 getSectionLoadBias(std::filesystem::path Path, Elf64_Addr FileAddr) {
  std::string Cmd = "readelf -WS " + Path.string();
  std::FILE *CmdPipe = ::popen(Cmd.c_str(), "r");

  std::regex TextRegex(R"(PROGBITS\s+(\w+)\s+(\w+)\s+(\w+))");
  char *Line = nullptr;
  std::size_t Len = 0;
  while (::getline(&Line, &Len, CmdPipe) != -1) {
    std::cmatch Groups;
    if (std::regex_search(Line, Groups, TextRegex)) {
      long Addr = std::stol(Groups[1], nullptr, 16);
      long Offs = std::stol(Groups[2], nullptr, 16);
      long Size = std::stol(Groups[3], nullptr, 16);
      if (Addr <= FileAddr && FileAddr < (Addr + Size)) {
        ::free(Line);
        ::pclose(CmdPipe);
        return Addr - Offs;
      }
    }
    ::free(Line);
    Line = nullptr;
  }

  ::pclose(CmdPipe);
  Error::send("Could not find section load bias");
}

U64 getEntryPointOffset(std::filesystem::path Path) {
  std::ifstream ElfFile(Path);

  Elf64_Ehdr Header;
  ElfFile.read(reinterpret_cast<char *>(&Header), sizeof(Header));

  Elf64_Addr EntryFileAddr = Header.e_entry;
  U64 LoadBias = getSectionLoadBias(Path, EntryFileAddr);
  return EntryFileAddr - LoadBias;
}

VirtAddr getLoadAddr(pid_t Pid, U64 Offs) {
  std::ifstream Maps("/proc/" + std::to_string(Pid) + "/maps");
  std::regex MapRegex(R"((\w+)-\w+ ..(.). (\w+))");

  std::string Data;
  while (std::getline(Maps, Data)) {
    std::smatch Groups;
    std::regex_search(Data, Groups, MapRegex);

    if (Groups[2] == 'x') {
      long LowRange = std::stol(Groups[1], nullptr, 16);
      long FileOffs = std::stol(Groups[3], nullptr, 16);
      return VirtAddr(Offs - FileOffs + LowRange);
    }
  }
  Error::send("Could not find load address");
}
} // namespace

TEST_CASE("Process::launch success", "[Process]") {
  auto Proc = Process::launch("yes");
  REQUIRE(processExists(Proc->pid()));
}

TEST_CASE("Process::launch no such program", "[Process]") {
  REQUIRE_THROWS_AS(Process::launch("a_program_that_does_not_exists"), Error);
}

TEST_CASE("Process::attach success", "[Process]") {
  auto Target = Process::launch("targets/run-endlessly", false);
  auto Proc = Process::attach(Target->pid());
  REQUIRE(getProcessStatus(Target->pid()) == 't');
}

TEST_CASE("Process::attach invalid PID", "[Process]") {
  REQUIRE_THROWS_AS(Process::attach(0), Error);
}

TEST_CASE("Process::resume success", "[Process]") {
  {
    auto Proc = Process::launch("targets/run-endlessly");
    Proc->resume();
    char Status = getProcessStatus(Proc->pid());
    REQUIRE((Status == 'R' || Status == 'S'));
  }
  {
    auto Target = Process::launch("targets/run-endlessly", false);
    auto Proc = Process::attach(Target->pid());
    Proc->resume();
    char Status = getProcessStatus(Proc->pid());
    REQUIRE((Status == 'R' || Status == 'S'));
  }
}

TEST_CASE("Process::resume not traced", "[Process]") {
  auto Proc = Process::launch("targets/run-endlessly", false);
  REQUIRE_THROWS_AS(Proc->resume(), Error);
}

TEST_CASE("Process::resume already terminated", "[Process]") {
  auto Proc = Process::launch("targets/end-immediately");
  Proc->resume();
  Proc->waitOnSignal();
  REQUIRE_THROWS_AS(Proc->resume(), Error);
}

TEST_CASE("Process::writeUserArea not traced", "[Process]") {
  auto Proc = Process::launch("targets/run-endlessly", false);
  REQUIRE_THROWS_AS(Proc->writeUserArea(0, 0), Error);
}

TEST_CASE("Write registers works", "[Register]") {
  Pipe Pipe(/*CloseOnExec=*/false);

  auto Proc = Process::launch("targets/reg-write", /*Debug=*/true,
                              /*OutFD=*/Pipe.getWrite());
  Pipe.closeWrite();

  Proc->resume();
  Proc->waitOnSignal();

  Registers &Regs = Proc->getRegisters();
  Regs.writeById(RegisterId::rsi, 0xcafebabe);

  Proc->resume();
  Proc->waitOnSignal();

  auto Out = Pipe.read();
  REQUIRE(toStringView(Out) == "0xcafebabe");

  Regs.writeById(RegisterId::mm0, 0xba5eba11);

  Proc->resume();
  Proc->waitOnSignal();

  Out = Pipe.read();
  REQUIRE(toStringView(Out) == "0xba5eba11");

  Regs.writeById(RegisterId::xmm0, 42.24);

  Proc->resume();
  Proc->waitOnSignal();

  Out = Pipe.read();
  REQUIRE(toStringView(Out) == "42.24");

  Regs.writeById(RegisterId::st0, 42.24l);

  // The status word tracks the current size of the FPU stack and reports
  // errors. It's 16 bits wide and bits 11 through 13 track the top of the
  // stack. Its value starts at index 0 and goes down instead of up, wrapping
  // around 7. So, to push a value to the stack, we set bits 11 through 13 to
  // 0b111.
  Regs.writeById(RegisterId::fsw, U16(0b00111000'00000000));

  // The tag register tracks which of the 'st' registers are valid, empty, or
  // special (i.e., NaNs or infinity). A tag of 0b11 means empty, 0b00 means
  // valid. We want to set the first tag to 0b00 and the rest to 0b11.
  Regs.writeById(RegisterId::ftw, U16(0b00111111'11111111));

  Proc->resume();
  Proc->waitOnSignal();

  Out = Pipe.read();
  REQUIRE(toStringView(Out) == "42.24");
}

TEST_CASE("Read register works", "[Register]") {
  auto Proc = Process::launch("targets/reg-read");

  Registers &Regs = Proc->getRegisters();

  Proc->resume();
  Proc->waitOnSignal();

  REQUIRE(Regs.readByIdAs<U64>(RegisterId::r13) == 0xcafebabe);

  Proc->resume();
  Proc->waitOnSignal();

  REQUIRE(Regs.readByIdAs<U8>(RegisterId::r13b) == 42);

  Proc->resume();
  Proc->waitOnSignal();

  REQUIRE(Regs.readByIdAs<Byte64>(RegisterId::mm0) == toByte64(0xba5eba11));

  Proc->resume();
  Proc->waitOnSignal();

  REQUIRE(Regs.readByIdAs<Byte128>(RegisterId::xmm0) == toByte128(64.125));

  Proc->resume();
  Proc->waitOnSignal();

  REQUIRE(Regs.readByIdAs<F128>(RegisterId::st0) == 64.125L);
}

TEST_CASE("Can create breakpoint site", "[Breakpoint]") {
  auto Proc = Process::launch("targets/run-endlessly");
  BreakpointSite &BS = Proc->createBreakpointSite(VirtAddr(42));
  REQUIRE(BS.addr().asInt() == 42);
}

TEST_CASE("Breakpoint site IDs increase", "[Breakpoint]") {
  auto Proc = Process::launch("targets/run-endlessly");

  auto &BS1 = Proc->createBreakpointSite(VirtAddr(42));
  REQUIRE(BS1.addr().asInt() == 42);

  auto &BS2 = Proc->createBreakpointSite(VirtAddr(43));
  REQUIRE(BS2.id() == BS1.id() + 1);

  auto &BS3 = Proc->createBreakpointSite(VirtAddr(44));
  REQUIRE(BS3.id() == BS2.id() + 1);

  auto &BS4 = Proc->createBreakpointSite(VirtAddr(45));
  REQUIRE(BS4.id() == BS3.id() + 1);
}

TEST_CASE("Can find breakpoint site", "[Breakpoint]") {
  auto Proc = Process::launch("targets/run-endlessly");
  const auto &CProc = Proc;

  Proc->createBreakpointSite(VirtAddr(42));
  Proc->createBreakpointSite(VirtAddr(43));
  Proc->createBreakpointSite(VirtAddr(44));
  Proc->createBreakpointSite(VirtAddr(45));

  auto &S1 = Proc->breakpointSites().getByAddr(VirtAddr(44));
  REQUIRE(Proc->breakpointSites().containsAddr(VirtAddr(44)));
  REQUIRE(S1.addr().asInt() == 44);

  auto &CS1 = CProc->breakpointSites().getByAddr(VirtAddr(44));
  REQUIRE(CProc->breakpointSites().containsAddr(VirtAddr(44)));
  REQUIRE(CS1.addr().asInt() == 44);

  auto &S2 = Proc->breakpointSites().getById(S1.id() + 1);
  REQUIRE(Proc->breakpointSites().containsId(S1.id() + 1));
  REQUIRE(S2.id() == S1.id() + 1);
  REQUIRE(S2.addr().asInt() == 45);

  auto &CS2 = CProc->breakpointSites().getById(CS1.id() + 1);
  REQUIRE(CProc->breakpointSites().containsId(CS1.id() + 1));
  REQUIRE(CS2.id() == CS1.id() + 1);
  REQUIRE(CS2.addr().asInt() == 45);
}

TEST_CASE("Cannot find breakpoint site", "[Breakpoint]") {
  auto Proc = Process::launch("targets/run-endlessly");
  const auto &CProc = Proc;

  REQUIRE_THROWS_AS(Proc->breakpointSites().getByAddr(VirtAddr(44)), Error);
  REQUIRE_THROWS_AS(Proc->breakpointSites().getById(44), Error);
  REQUIRE_THROWS_AS(CProc->breakpointSites().getByAddr(VirtAddr(44)), Error);
  REQUIRE_THROWS_AS(CProc->breakpointSites().getById(44), Error);
}

TEST_CASE("Breakpoint site list size and emptiness", "[Breakpoint]") {
  auto Proc = Process::launch("targets/run-endlessly");
  const auto &CProc = Proc;

  REQUIRE(Proc->breakpointSites().empty());
  REQUIRE(Proc->breakpointSites().size() == 0);
  REQUIRE(CProc->breakpointSites().empty());
  REQUIRE(CProc->breakpointSites().size() == 0);

  Proc->createBreakpointSite(VirtAddr(42));
  REQUIRE(!Proc->breakpointSites().empty());
  REQUIRE(Proc->breakpointSites().size() == 1);
  REQUIRE(!CProc->breakpointSites().empty());
  REQUIRE(CProc->breakpointSites().size() == 1);

  Proc->createBreakpointSite(VirtAddr(43));
  REQUIRE(!Proc->breakpointSites().empty());
  REQUIRE(Proc->breakpointSites().size() == 2);
  REQUIRE(!CProc->breakpointSites().empty());
  REQUIRE(CProc->breakpointSites().size() == 2);
}

TEST_CASE("Can iterate breakpoint sites", "[Breakpoint]") {
  auto Proc = Process::launch("targets/run-endlessly");
  const auto &CProc = Proc;

  Proc->createBreakpointSite(VirtAddr(42));
  Proc->createBreakpointSite(VirtAddr(43));
  Proc->createBreakpointSite(VirtAddr(44));
  Proc->createBreakpointSite(VirtAddr(45));

  Proc->breakpointSites().forEach(
      [Addr = 42](auto &BS) mutable { REQUIRE(BS.addr().asInt() == Addr++); });

  CProc->breakpointSites().forEach(
      [Addr = 42](auto &BS) mutable { REQUIRE(BS.addr().asInt() == Addr++); });
}

TEST_CASE("Breakpoint on address works", "[Breakpoint]") {
  Pipe Pipe(/*CloseOnExec=*/false);

  auto Proc = Process::launch("targets/hello-ddb", /*Debug=*/true,
                              /*OutFD=*/Pipe.getWrite());
  Pipe.closeWrite();

  U64 Offs = getEntryPointOffset("targets/hello-ddb");
  VirtAddr LoadAddr = getLoadAddr(Proc->pid(), Offs);

  Proc->createBreakpointSite(LoadAddr).enable();
  Proc->resume();
  StopReason Reason = Proc->waitOnSignal();

  REQUIRE(Reason.State == ProcessState::Stopped);
  REQUIRE(Reason.Info == SIGTRAP);
  REQUIRE(Proc->getPC() == LoadAddr);

  Proc->resume();
  Reason = Proc->waitOnSignal();

  REQUIRE(Reason.State == ProcessState::Exited);
  REQUIRE(Reason.Info == 0);

  std::vector<std::byte> Data = Pipe.read();
  REQUIRE(toStringView(Data) == "Hello, DDB!\n");
}

TEST_CASE("Can remove breakpoint sites", "[Breakpoint]") {
  auto Proc = Process::launch("targets/run-endlessly");

  BreakpointSite &BS = Proc->createBreakpointSite(VirtAddr(42));
  Proc->createBreakpointSite(VirtAddr(43));
  REQUIRE(Proc->breakpointSites().size() == 2);

  Proc->breakpointSites().removeById(BS.id());
  Proc->breakpointSites().removeByAddr(VirtAddr(43));
  REQUIRE(Proc->breakpointSites().empty());
}

TEST_CASE("Reading and writing memory works", "[Memory]") {
  Pipe Pipe(/*CloseOnExec=*/true);
  auto Proc = Process::launch("targets/memory", /*Debug=*/true,
                              /*OutFD=*/Pipe.getWrite());
  Pipe.closeWrite();

  Proc->resume();
  Proc->waitOnSignal();

  auto AAddr = fromBytes<U64>(Pipe.read().data());
  std::vector<std::byte> AVec = Proc->readMemory(VirtAddr(AAddr), 8);
  auto A = fromBytes<U64>(AVec.data());
  REQUIRE(A == 0xcafebabe);

  Proc->resume();
  Proc->waitOnSignal();

  auto BAddr = fromBytes<U64>(Pipe.read().data());
  Proc->writeMemory(VirtAddr(BAddr), {asBytes("Hello, DDB!"), 12});

  Proc->resume();
  Proc->waitOnSignal();

  std::vector<std::byte> B = Pipe.read();
  REQUIRE(toStringView(B) == "Hello, DDB!");
}

TEST_CASE("Hardware breakpoint evade memory checksums", "[Breakpoint]") {
  Pipe Pipe(/*CloseOnExec=*/true);
  auto Proc = Process::launch("targets/anti-debugger", /*Debug=*/true,
                              /*OutFD=*/Pipe.getWrite());
  Pipe.closeWrite();

  Proc->resume();
  Proc->waitOnSignal();

  auto FuncAddr = VirtAddr(fromBytes<U64>(Pipe.read().data()));

  BreakpointSite &SoftBS =
      Proc->createBreakpointSite(FuncAddr, /*Hardware=*/false);
  SoftBS.enable();

  Proc->resume();
  Proc->waitOnSignal();

  REQUIRE(toStringView(Pipe.read()) == "Doing something innocent...\n");

  Proc->breakpointSites().removeById(SoftBS.id());
  BreakpointSite &HardBS =
      Proc->createBreakpointSite(FuncAddr, /*Hardware=*/true);
  HardBS.enable();

  Proc->resume();
  Proc->waitOnSignal();

  Proc->resume();
  Proc->waitOnSignal();

  REQUIRE(toStringView(Pipe.read()) == "Doing something evil...\n");
}

TEST_CASE("Watchpoint detects read", "[Watchpoint]") {
  Pipe Pipe(/*CloseOnExec=*/false);
  auto Proc = Process::launch("targets/anti-debugger", /*Debug=*/true,
                              /*OutFD=*/Pipe.getWrite());
  Pipe.closeWrite();

  Proc->resume();
  Proc->waitOnSignal();

  auto FuncAddr = VirtAddr(fromBytes<U64>(Pipe.read().data()));

  Watchpoint &WP =
      Proc->createWatchpoint(FuncAddr, StoppointMode::ReadWrite, 1);
  WP.enable();

  Proc->resume();
  Proc->waitOnSignal();

  Proc->stepInstruction();
  BreakpointSite &BP = Proc->createBreakpointSite(FuncAddr, /*Hardware=*/false);
  BP.enable();

  Proc->resume();
  StopReason Reason = Proc->waitOnSignal();

  REQUIRE(Reason.Info == SIGTRAP);

  Proc->resume();
  Proc->waitOnSignal();

  REQUIRE(toStringView(Pipe.read()) == "Doing something evil...\n");
}
