#include "DDB/Disassembler.h"
#include "DDB/Types.h"

#include <Zycore/Status.h>
#include <Zydis/SharedTypes.h>
#include <cstddef>
#include <optional>
#include <vector>

#include <Zydis/Zydis.h>

std::vector<DDB::Disassembler::Instruction>
DDB::Disassembler::disassemble(std::size_t nInstr,
                               std::optional<VirtAddr> addr) const {
  std::vector<Instruction> ret;
  ret.reserve(nInstr);

  if (!addr) {
    addr.emplace(m_proc->getPC());
  }

  // The largest x86_64 instruction is 15 byte, reading nInstrInstr * 15 bytes
  // guarantees to have enough memory to disassemble 'nInstr' instructions.
  std::vector<std::byte> code = m_proc->readMemory(*addr, nInstr * 15);

  ZyanUSize offs = 0;
  ZydisDisassembledInstruction instr;
  while (ZYAN_SUCCESS(ZydisDisassembleATT(ZYDIS_MACHINE_MODE_LONG_64,
                                          addr->asInt(), code.data() + offs,
                                          code.size() - offs, &instr)) &&
         nInstr > 0) {
    ret.push_back(Instruction{*addr, std::string(instr.text)});
    offs += instr.info.length;
    *addr += instr.info.length;
    --nInstr;
  }

  return ret;
}
