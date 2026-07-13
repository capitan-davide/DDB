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
template <class T> DDB::Byte128 widen(const DDB::RegisterInfo &Info, T Val) {
  using namespace DDB;
  if constexpr (std::is_floating_point_v<T>) {
    if (Info.Format == RegisterFormat::DoubleFloat)
      return toByte128(static_cast<F64>(Val));
    if (Info.Format == RegisterFormat::LongDouble)
      return toByte128(static_cast<F128>(Val));
  } else if constexpr (std::is_signed_v<T>) {
    if (Info.Format == RegisterFormat::UInt) {
      switch (Info.Size) {
      case 2:
        return toByte128(static_cast<I16>(Val));
      case 4:
        return toByte128(static_cast<I32>(Val));
      case 8:
        return toByte128(static_cast<I64>(Val));
      }
    }
  }
  return toByte128(Val);
}
} // namespace

DDB::Registers::Value DDB::Registers::read(const RegisterInfo &Info) const {
  const std::byte *Bytes = asBytes(Data);
  if (Info.Format == RegisterFormat::UInt) {
    switch (Info.Size) {
    case 1:
      return fromBytes<U8>(Bytes + Info.Offset);
    case 2:
      return fromBytes<U16>(Bytes + Info.Offset);
    case 4:
      return fromBytes<U32>(Bytes + Info.Offset);
    case 8:
      return fromBytes<U64>(Bytes + Info.Offset);
    default:
      Error::send("Unexpected register size");
    }
  } else if (Info.Format == RegisterFormat::DoubleFloat) {
    return fromBytes<F64>(Bytes + Info.Offset);
  } else if (Info.Format == RegisterFormat::LongDouble) {
    return fromBytes<F128>(Bytes + Info.Offset);
  } else if (Info.Format == RegisterFormat::Vector && Info.Size == 8) {
    return fromBytes<Byte64>(Bytes + Info.Offset);
  } else {
    return fromBytes<Byte128>(Bytes + Info.Offset);
  }
}

void DDB::Registers::write(const RegisterInfo &Info, Value Val) {
  std::byte *Bytes = asBytes(Data);
  std::visit(
      [&](auto &V) {
        if (sizeof(V) <= Info.Size) {
          Byte128 Wide = widen(Info, V);
          auto ValBytes = asBytes(Wide);
          std::copy(ValBytes, ValBytes + sizeof(V), Bytes + Info.Offset);
        } else {
          // FIXME: Make this an assertion?
          std::cerr << "DDB::Registers::write called with mismatched register "
                       "and value sizes";
          std::terminate();
        }
      },
      Val);

  // TODO: Write a comment about why it's done like so!
  if (Info.Type == RegisterType::FPR) {
    Proc->writeFPRs(Data.i387);
  } else {
    std::size_t AlignedOffs = Info.Offset & ~0b111;
    Proc->writeUserArea(Info.Offset, fromBytes<U64>(Bytes + AlignedOffs));
  }
}
