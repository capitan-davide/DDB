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
    VirtAddr addr;
    std::string text;
  };

public:
  Disassembler(const Process &proc) : m_proc(&proc) {}

  /// Disassemble a number of instructions.
  /// @param nInstr The number of instructions to disassemble.
  /// @param addr If set, start disassembly from this address rather than the
  ///             current program counter.
  /// @return The disassembled instructions.
  std::vector<Instruction> disassemble(std::size_t nInstr,
                                       std::optional<VirtAddr> addr = {}) const;

private:
  const Process *m_proc;
};
} // namespace DDB

#endif // DDB_DISASSEMBLER_H
