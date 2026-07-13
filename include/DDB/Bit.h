#ifndef DDB_BIT_H
#define DDB_BIT_H

#include "DDB/Types.h"

#include <cstddef>
#include <cstring>
#include <string_view>
#include <vector>

namespace DDB {
template <class To> To fromBytes(const std::byte *Bytes) {
  To Ret;
  std::memcpy(&Ret, Bytes, sizeof(To));
  return Ret;
}

template <class From> std::byte *asBytes(From &Val) {
  return reinterpret_cast<std::byte *>(&Val);
}

template <class From> const std::byte *asBytes(const From &Val) {
  return reinterpret_cast<const std::byte *>(&Val);
}

template <class From> Byte64 toByte64(From Src) {
  Byte64 Ret{};
  std::memcpy(&Ret, &Src, sizeof(From));
  return Ret;
}

template <class From> Byte128 toByte128(From Src) {
  Byte128 Ret{};
  std::memcpy(&Ret, &Src, sizeof(From));
  return Ret;
}

inline std::string_view toStringView(const std::byte *Data, std::size_t Size) {
  return {reinterpret_cast<const char *>(Data), Size};
}

inline std::string_view toStringView(const std::vector<std::byte> &Data) {
  return toStringView(Data.data(), Data.size());
}
} // namespace DDB

#endif // DDB_BIT_H
