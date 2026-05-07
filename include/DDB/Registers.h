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

  template <class T> T readByIdAs(RegisterId id) const {
    auto RI = registerInfoById(id);
    return std::get<T>(read(registerInfoById(id)));
  }

  void writeById(RegisterId id, Value val) { write(registerInfoById(id), val); }

private:
  friend Process;
  Registers(Process &proc) : m_proc(&proc) {}

  user m_data;
  Process *m_proc;
};
} // namespace DDB

#endif // DDB_REGISTERS_H
