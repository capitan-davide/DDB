#ifndef DDB_REGISTER_INFO_H
#define DDB_REGISTER_INFO_H

#include "DDB/Error.h"
#include "DDB/Types.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string_view>

#include <sys/user.h>

namespace DDB {
enum class RegisterId {
#define REG(name, dwarfId, size, offset, type, format) name,
#include "DDB/Registers.inc"
#undef REG
};

inline std::string_view toString(RegisterId Id) {
#define REG(name, dwarfId, size, offset, type, format)                         \
  if (Id == RegisterId::name)                                                  \
    return "RegisterId::" #name;
#include "DDB/Registers.inc"
#undef REG
  return "";
}

enum class RegisterType { GPR, SubGPR, FPR, DR };

enum class RegisterFormat { UInt, DoubleFloat, LongDouble, Vector };

struct RegisterInfo {
  RegisterId Id;
  std::string_view Name;
  I32 DwarfId;
  std::size_t Size;
  std::size_t Offset;
  RegisterType Type;
  RegisterFormat Format;
};

inline constexpr const RegisterInfo RegisterInfoTable[] = {
#define REG(name, dwarfId, size, offset, type, format)                         \
  {RegisterId::name, #name, dwarfId, size, offset, type, format},
#include "DDB/Registers.inc"
#undef REG
};

template <class F> const RegisterInfo &registerInfoBy(F Fn) {
  auto It = std::find_if(std::begin(RegisterInfoTable),
                         std::end(RegisterInfoTable), Fn);
  if (It == std::end(RegisterInfoTable))
    Error::send("Can't find register info");

  return *It;
}

inline const RegisterInfo &registerInfoById(RegisterId Id) {
  return registerInfoBy([Id](auto &RI) { return RI.Id == Id; });
}

inline const RegisterInfo &registerInfoByName(std::string_view Name) {
  return registerInfoBy([Name](auto &RI) { return RI.Name == Name; });
}

inline const RegisterInfo &registerInfoByDwarfId(I32 DwarfId) {
  return registerInfoBy([DwarfId](auto &RI) { return RI.DwarfId == DwarfId; });
}

} // namespace DDB

#endif // DDB_REGISTER_INFO_H
