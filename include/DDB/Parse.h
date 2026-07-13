#ifndef DDB_PARSE_H
#define DDB_PARSE_H

#include "DDB/Error.h"
#include "DDB/Types.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace DDB {
template <class I>
std::optional<I> toIntegral(std::string_view Str, int Base = 10) {
  auto Begin = Str.begin();
  if (Base == 16 && Str.size() > 1 && Begin[0] == '0' && Begin[1] == 'x') {
    Begin += 2;
  }

  I Ret;
  auto [Ptr, EC] = std::from_chars(Begin, Str.end(), Ret, Base);
  if (EC != std::errc()) {
    return std::nullopt;
  }
  return Ret;
}

template <>
inline std::optional<std::byte> toIntegral(std::string_view Str, int Base) {
  std::optional<U8> Val = toIntegral<U8>(Str, Base);
  if (Val) {
    return static_cast<std::byte>(*Val);
  }
  return std::nullopt;
}

template <class F> std::optional<F> toFloat(std::string_view Str) {
  F Ret;
  auto [Ptr, EC] = std::from_chars(Str.begin(), Str.end(), Ret);
  if (EC != std::errc()) {
    return std::nullopt;
  }
  return Ret;
}

template <std::size_t N>
std::array<std::byte, N> parseVector(std::string_view Text) {
  auto SendInvalidFormat = [] { DDB::Error::send("Invalid format"); };

  std::array<std::byte, N> Bytes;
  const char *C = Text.data();

  if (*C++ != '[') {
    SendInvalidFormat();
  }

  for (std::size_t I = 0; I < N - 1; ++I) {
    Bytes[I] = toIntegral<std::byte>({C, 4}, 16).value();
    C += 4;
    if (*C++ != ',') {
      SendInvalidFormat();
    }
  }
  Bytes[N - 1] = toIntegral<std::byte>({C, 4}, 16).value();
  C += 4;

  if (*C++ != ']') {
    SendInvalidFormat();
  }
  if (C != Text.end()) {
    SendInvalidFormat();
  }

  return Bytes;
}

inline std::vector<std::byte> parseVector(std::string_view Text) {
  auto SendInvalidFormat = [] { DDB::Error::send("Invalid format"); };

  std::vector<std::byte> Bytes;
  const char *C = Text.data();

  if (*C++ != '[')
    SendInvalidFormat();

  while (*C != ']') {
    auto Byte = DDB::toIntegral<std::byte>({C, 4}, 16);
    Bytes.push_back(Byte.value());
    C += 4;

    if (*C == ',')
      ++C;
    else if (*C != ']')
      SendInvalidFormat();
  }

  if (++C != Text.end())
    SendInvalidFormat();

  return Bytes;
}
} // namespace DDB

#endif // DDB_PARSE_H
