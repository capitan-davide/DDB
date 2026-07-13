#ifndef DDB_REGISTERS_H
#define DDB_REGISTERS_H

#include "DDB/RegisterInfo.h"
#include "DDB/Types.h"

#include <variant>

#include <sys/user.h>

namespace DDB {
class Process;
class Registers {
public:
  using Value = std::variant<U8, U16, U32, U64, I8, I16, I32, I64, F32, F64,
                             F128, Byte64, Byte128>;

  Registers() = delete;
  Registers(const Registers &) = delete;
  Registers(Registers &&) = delete;
  Registers &operator=(const Registers &) = delete;
  Registers &operator=(Registers &&) = delete;
  ~Registers() = default;

  Value read(const RegisterInfo &info) const;
  void write(const RegisterInfo &info, Value val);

  template <class T> T readByIdAs(RegisterId Id) const {
    auto RI = registerInfoById(Id);
    return std::get<T>(read(registerInfoById(Id)));
  }

  void writeById(RegisterId Id, Value Val) { write(registerInfoById(Id), Val); }

private:
  friend Process;
  Registers(Process &Proc) : Proc(&Proc) {}

  user Data;
  Process *Proc;
};
} // namespace DDB

#endif // DDB_REGISTERS_H
