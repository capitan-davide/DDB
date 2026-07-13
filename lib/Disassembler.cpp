#include "DDB/Disassembler.h"
#include "DDB/Types.h"

#include <Zycore/Status.h>
#include <Zydis/SharedTypes.h>
#include <cstddef>
#include <optional>
#include <vector>

#include <Zydis/Zydis.h>

std::vector<DDB::Disassembler::Instruction>
DDB::Disassembler::disassemble(std::size_t NumInstr,
                               std::optional<VirtAddr> Addr) const {
  std::vector<Instruction> Ret;
  Ret.reserve(NumInstr);

  if (!Addr) {
    Addr.emplace(Proc->getPC());
  }

  // The largest x86_64 instruction is 15 byte, reading NumInstr * 15 bytes
  // guarantees to have enough memory to disassemble 'NumInstr' instructions.
  std::vector<std::byte> Code =
      Proc->readMemoryWithoutTraps(*Addr, NumInstr * 15);

  ZyanUSize Offs = 0;
  ZydisDisassembledInstruction Instr;
  while (ZYAN_SUCCESS(ZydisDisassembleATT(ZYDIS_MACHINE_MODE_LONG_64,
                                          Addr->asInt(), Code.data() + Offs,
                                          Code.size() - Offs, &Instr)) &&
         NumInstr > 0) {
    Ret.push_back(Instruction{*Addr, std::string(Instr.text)});
    Offs += Instr.info.length;
    *Addr += Instr.info.length;
    --NumInstr;
  }

  return Ret;
}
