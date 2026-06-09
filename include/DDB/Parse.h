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
std::optional<I> toIntegral(std::string_view sv, int base = 10) {
  auto begin = sv.begin();
  if (base == 16 && sv.size() > 1 && begin[0] == '0' && begin[1] == 'x') {
    begin += 2;
  }

  I ret;
  auto [ptr, ec] = std::from_chars(begin, sv.end(), ret, base);
  if (ec != std::errc()) {
    return std::nullopt;
  }
  return ret;
}

template <>
inline std::optional<std::byte> toIntegral(std::string_view sv, int base) {
  std::optional<U8> u8 = toIntegral<U8>(sv, base);
  if (u8) {
    return static_cast<std::byte>(*u8);
  }
  return std::nullopt;
}

template <class F> std::optional<F> toFloat(std::string_view sv) {
  F ret;
  auto [ptr, ec] = std::from_chars(sv.begin(), sv.end(), ret);
  if (ec != std::errc()) {
    return std::nullopt;
  }
  return ret;
}

template <std::size_t N>
std::array<std::byte, N> parseVector(std::string_view text) {
  auto sendInvalidFormat = [] { DDB::Error::send("Invalid format"); };

  std::array<std::byte, N> bytes;
  const char *c = text.data();

  if (*c++ != '[') {
    sendInvalidFormat();
  }

  for (std::size_t i = 0; i < N - 1; ++i) {
    bytes[i] = toIntegral<std::byte>({c, 4}, 16).value();
    c += 4;
    if (*c++ != ',') {
      sendInvalidFormat();
    }
  }
  bytes[N - 1] = toIntegral<std::byte>({c, 4}, 16).value();
  c += 4;

  if (*c++ != ']') {
    sendInvalidFormat();
  }
  if (c != text.end()) {
    sendInvalidFormat();
  }

  return bytes;
}

inline std::vector<std::byte> parseVector(std::string_view text) {
  auto sendInvalidFormat = [] { DDB::Error::send("Invalid format"); };

  std::vector<std::byte> bytes;
  const char *c = text.data();

  if (*c++ != '[')
    sendInvalidFormat();

  while (*c != ']') {
    auto byte = DDB::toIntegral<std::byte>({c, 4}, 16);
    bytes.push_back(byte.value());
    c += 4;

    if (*c == ',')
      ++c;
    else if (*c != ']')
      sendInvalidFormat();
  }

  if (++c != text.end())
    sendInvalidFormat();

  return bytes;
}
} // namespace DDB

#endif // DDB_PARSE_H
