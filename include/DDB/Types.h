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
  explicit VirtAddr(U64 addr) : m_addr(addr) {}

  U64 asInt() const { return m_addr; }

  VirtAddr operator+(U64 offs) const { return VirtAddr(m_addr + offs); }
  VirtAddr operator-(U64 offs) const { return VirtAddr(m_addr - offs); }

  VirtAddr operator+=(U64 offs) {
    m_addr += offs;
    return *this;
  }
  VirtAddr operator-=(U64 offs) {
    m_addr -= offs;
    return *this;
  }

  bool operator==(const VirtAddr &other) const {
    return m_addr == other.m_addr;
  }
  bool operator!=(const VirtAddr &other) const {
    return m_addr != other.m_addr;
  }
  bool operator<(const VirtAddr &other) const { return m_addr < other.m_addr; }
  bool operator<=(const VirtAddr &other) const {
    return m_addr <= other.m_addr;
  }
  bool operator>(const VirtAddr &other) const { return m_addr > other.m_addr; }
  bool operator>=(const VirtAddr &other) const {
    return m_addr >= other.m_addr;
  }

private:
  U64 m_addr = 0;
};

template <class T> class Span {
public:
  Span() = default;
  Span(T *data, std::size_t size) : m_data(data), m_size(size) {}
  Span(T *begin, T *end) : m_data(begin), m_size(end - begin) {}

  template <class U>
  Span(const std::vector<U> &v) : m_data(v.data()), m_size(v.size()) {}

  T *begin() const { return m_data; }
  T *end() const { return m_data + m_size; }
  std::size_t size() const { return m_size; }
  T &operator[](std::size_t n) { return *(m_data + n); }

private:
  T *m_data = nullptr;
  std::size_t m_size = 0;
};
} // namespace DDB

#endif // DDB_TYPES_H
