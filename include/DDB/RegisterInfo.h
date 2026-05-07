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

inline std::string_view toString(RegisterId id) {
#define REG(name, dwarfId, size, offset, type, format)                         \
  if (id == RegisterId::name)                                                  \
    return "RegisterId::" #name;
#include "DDB/Registers.inc"
#undef REG
  return "";
}

enum class RegisterType { GPR, SubGPR, FPR, DR };

enum class RegisterFormat { UInt, DoubleFloat, LongDouble, Vector };

struct RegisterInfo {
  RegisterId id;
  std::string_view name;
  I32 dwarfId;
  std::size_t size;
  std::size_t offset;
  RegisterType type;
  RegisterFormat format;
};

inline constexpr const RegisterInfo g_registerInfo[] = {
#define REG(name, dwarfId, size, offset, type, format)                         \
  {RegisterId::name, #name, dwarfId, size, offset, type, format},
#include "DDB/Registers.inc"
#undef REG
};

template <class F> const RegisterInfo &registerInfoBy(F f) {
  auto it =
      std::find_if(std::begin(g_registerInfo), std::end(g_registerInfo), f);
  if (it == std::end(g_registerInfo))
    Error::send("Can't find register info");

  return *it;
}

inline const RegisterInfo &registerInfoById(RegisterId id) {
  return registerInfoBy([id](auto &i) { return i.id == id; });
}

inline const RegisterInfo &registerInfoByName(std::string_view name) {
  return registerInfoBy([name](auto &i) { return i.name == name; });
}

inline const RegisterInfo &registerInfoByDwarfId(I32 dwarfId) {
  return registerInfoBy([dwarfId](auto &i) { return i.dwarfId == dwarfId; });
}

} // namespace DDB

#endif // DDB_REGISTER_INFO_H
