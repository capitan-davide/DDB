#ifndef DDB_TYPES_H
#define DDB_TYPES_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using U8 = std::uint8_t;
using U16 = std::uint16_t;
using U32 = std::uint32_t;
using U64 = std::uint64_t;
using I8 = std::int8_t;
using I16 = std::int16_t;
using I32 = std::int32_t;
using I64 = std::int64_t;

using F32 = float;
using F64 = double;
using F128 = long double;

namespace DDB {
using Byte64 = std::array<std::byte, 8>;
using Byte128 = std::array<std::byte, 16>;

class VirtAddr {
public:
  VirtAddr() = default;
  explicit VirtAddr(U64 Addr) : Addr(Addr) {}

  U64 asInt() const { return Addr; }

  VirtAddr operator+(U64 Offs) const { return VirtAddr(Addr + Offs); }
  VirtAddr operator-(U64 Offs) const { return VirtAddr(Addr - Offs); }

  VirtAddr operator+=(U64 Offs) {
    Addr += Offs;
    return *this;
  }
  VirtAddr operator-=(U64 Offs) {
    Addr -= Offs;
    return *this;
  }

  bool operator==(const VirtAddr &Other) const { return Addr == Other.Addr; }
  bool operator!=(const VirtAddr &Other) const { return Addr != Other.Addr; }
  bool operator<(const VirtAddr &Other) const { return Addr < Other.Addr; }
  bool operator<=(const VirtAddr &Other) const { return Addr <= Other.Addr; }
  bool operator>(const VirtAddr &Other) const { return Addr > Other.Addr; }
  bool operator>=(const VirtAddr &Other) const { return Addr >= Other.Addr; }

private:
  U64 Addr = 0;
};

template <class T> class Span {
public:
  Span() = default;
  Span(T *Data, std::size_t Size) : Data(Data), Size(Size) {}
  Span(T *Begin, T *End) : Data(Begin), Size(End - Begin) {}

  template <class U>
  Span(const std::vector<U> &V) : Data(V.data()), Size(V.size()) {}

  T *begin() const { return Data; }
  T *end() const { return Data + Size; }
  std::size_t size() const { return Size; }
  T &operator[](std::size_t N) { return *(Data + N); }

private:
  T *Data = nullptr;
  std::size_t Size = 0;
};

enum class StoppointMode { Write, ReadWrite, Execute };
} // namespace DDB

#endif // DDB_TYPES_H
