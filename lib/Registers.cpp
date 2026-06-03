#include "DDB/Registers.h"
#include "DDB/Bit.h"
#include "DDB/Error.h"
#include "DDB/Process.h"
#include "DDB/RegisterInfo.h"
#include "DDB/Types.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <iostream>
#include <type_traits>
#include <variant>

namespace {
template <class T> DDB::Byte128 widen(const DDB::RegisterInfo &info, T t) {
  using namespace DDB;
  if constexpr (std::is_floating_point_v<T>) {
    if (info.format == RegisterFormat::DoubleFloat)
      return toByte128(static_cast<F64>(t));
    if (info.format == RegisterFormat::LongDouble)
      return toByte128(static_cast<F128>(t));
  } else if constexpr (std::is_signed_v<T>) {
    if (info.format == RegisterFormat::UInt) {
      switch (info.size) {
      case 2:
        return toByte128(static_cast<I16>(t));
      case 4:
        return toByte128(static_cast<I32>(t));
      case 8:
        return toByte128(static_cast<I64>(t));
      }
    }
  }
  return toByte128(t);
}
} // namespace

DDB::Registers::Value DDB::Registers::read(const RegisterInfo &info) const {
  const std::byte *bytes = asBytes(m_data);
  if (info.format == RegisterFormat::UInt) {
    switch (info.size) {
    case 1:
      return fromBytes<U8>(bytes + info.offset);
    case 2:
      return fromBytes<U16>(bytes + info.offset);
    case 4:
      return fromBytes<U32>(bytes + info.offset);
    case 8:
      return fromBytes<U64>(bytes + info.offset);
    default:
      Error::send("Unexpected register size");
    }
  } else if (info.format == RegisterFormat::DoubleFloat) {
    return fromBytes<F64>(bytes + info.offset);
  } else if (info.format == RegisterFormat::LongDouble) {
    return fromBytes<F128>(bytes + info.offset);
  } else if (info.format == RegisterFormat::Vector && info.size == 8) {
    return fromBytes<Byte64>(bytes + info.offset);
  } else {
    return fromBytes<Byte128>(bytes + info.offset);
  }
}

void DDB::Registers::write(const RegisterInfo &info, Value value) {
  std::byte *bytes = asBytes(m_data);
  std::visit(
      [&](auto &v) {
        if (sizeof(v) <= info.size) {
          Byte128 wide = widen(info, v);
          auto valBytes = asBytes(wide);
          std::copy(valBytes, valBytes + sizeof(v), bytes + info.offset);
        } else {
          // FIXME: Make this an assertion?
          std::cerr << "DDB::Registers::write called with mismatched register "
                       "and value sizes";
          std::terminate();
        }
      },
      value);

  // TODO: Write a comment about why it's done like so!
  if (info.type == RegisterType::FPR) {
    m_proc->writeFPRs(m_data.i387);
  } else {
    std::size_t alignedOffs = info.offset & ~0b111;
    m_proc->writeUserArea(info.offset, fromBytes<U64>(bytes + alignedOffs));
  }
}
