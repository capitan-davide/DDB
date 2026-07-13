#ifndef DDB_DISASSEMBLER_H
#define DDB_DISASSEMBLER_H

#include "DDB/Process.h"
#include "DDB/Types.h"

#include <optional>
#include <string>
#include <vector>

namespace DDB {
class Disassembler {
  struct Instruction {
    VirtAddr Addr;
    std::string Text;
  };

public:
  Disassembler(const Process &Proc) : Proc(&Proc) {}

  /// Disassemble a number of instructions.
  /// @param NumInstr The number of instructions to disassemble.
  /// @param Addr If set, start disassembly from this address rather than the
  ///             current program counter.
  /// @return The disassembled instructions.
  std::vector<Instruction> disassemble(std::size_t NumInstr,
                                       std::optional<VirtAddr> Addr = {}) const;

private:
  const Process *Proc;
};
} // namespace DDB

#endif // DDB_DISASSEMBLER_H
