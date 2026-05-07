#ifndef DDB_BIT_H
#define DDB_BIT_H

#include "DDB/Types.h"

#include <cstddef>
#include <cstring>
#include <string_view>
#include <vector>

namespace DDB {
template <class To> To fromBytes(const std::byte *bytes) {
  To ret;
  std::memcpy(&ret, bytes, sizeof(To));
  return ret;
}

template <class From> std::byte *asBytes(From &from) {
  return reinterpret_cast<std::byte *>(&from);
}

template <class From> const std::byte *asBytes(const From &from) {
  return reinterpret_cast<const std::byte *>(&from);
}

template <class From> Byte64 toByte64(From src) {
  Byte64 ret{};
  std::memcpy(&ret, &src, sizeof(From));
  return ret;
}

template <class From> Byte128 toByte128(From src) {
  Byte128 ret{};
  std::memcpy(&ret, &src, sizeof(From));
  return ret;
}

inline std::string_view toStringView(const std::byte *data, std::size_t size) {
  return {reinterpret_cast<const char *>(data), size};
}

inline std::string_view toStringView(const std::vector<std::byte> &data) {
  return toStringView(data.data(), data.size());
}
} // namespace DDB

#endif // DDB_BIT_H
