// IWYU pragma: private, include "jewels/memory/fields.hh"

#pragma once

#include "jewels/memory/fields.hh"

#include "jewels/memory/bits.hh"
#include "jewels/meta/types.hh" // IWYU pragma: keep

#include <endian.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace jewels::memory
{

template <ByteSwappable T>
constexpr T FromBigEndian::operator()(T value) const
{
  if constexpr (sizeof(T) == sizeof(uint64_t))
  {
    return static_cast<T>(be64toh(value));
  }
  else if constexpr (sizeof(T) == sizeof(uint32_t))
  {
    return static_cast<T>(be32toh(value));
  }
  else if constexpr (sizeof(T) == sizeof(uint16_t))
  {
    return static_cast<T>(be16toh(value));
  }
  else
  {
    static_assert(jewels::meta::always_false_v<T>, "Unsupported type");
  }
}

template <ByteSwappable T>
constexpr T FromLittleEndian::operator()(T value) const
{
  if constexpr (sizeof(T) == sizeof(uint64_t))
  {
    return static_cast<T>(le64toh(value));
  }
  else if constexpr (sizeof(T) == sizeof(uint32_t))
  {
    return static_cast<T>(le32toh(value));
  }
  else if constexpr (sizeof(T) == sizeof(uint16_t))
  {
    return static_cast<T>(le16toh(value));
  }
  else
  {
    static_assert(jewels::meta::always_false_v<T>, "Unsupported type");
  }
}

template <ByteSwappable T>
constexpr T ToBigEndian::operator()(T value) const
{
  if constexpr (sizeof(T) == sizeof(uint64_t))
  {
    return static_cast<T>(htobe64(value));
  }
  else if constexpr (sizeof(T) == sizeof(uint32_t))
  {
    return static_cast<T>(htobe32(value));
  }
  else if constexpr (sizeof(T) == sizeof(uint16_t))
  {
    return static_cast<T>(htobe16(value));
  }
  else
  {
    static_assert(jewels::meta::always_false_v<T>, "Unsupported type");
  }
}

template <ByteSwappable T>
constexpr T ToLittleEndian::operator()(T value) const
{
  if constexpr (sizeof(T) == sizeof(uint64_t))
  {
    return static_cast<T>(htole64(value));
  }
  else if constexpr (sizeof(T) == sizeof(uint32_t))
  {
    return static_cast<T>(htole32(value));
  }
  else if constexpr (sizeof(T) == sizeof(uint16_t))
  {
    return static_cast<T>(htole16(value));
  }
  else
  {
    static_assert(jewels::meta::always_false_v<T>, "Unsupported type");
  }
}

template <typename FieldType>
constexpr auto offset_from()
{
  return FieldType::offset_bytes + FieldType::size_bytes;
}

template <typename FieldType, auto span_size>
[[nodiscard]] FieldValueType<FieldType> read_field(std::span<const std::byte, span_size> data)
{
  return FieldType::read_transform(jewels::memory::bit_cast_to<typename FieldType::DestinationType>(
    data.template subspan<FieldType::offset_bytes, FieldType::size_bytes>()));
}

template <typename FieldType, auto span_size>
void write_field(const FieldValueType<FieldType>& value, std::span<std::byte, span_size> data)
{
  jewels::memory::write_as_bytes(
    FieldType::write_transform(value), data.template subspan<FieldType::offset_bytes, FieldType::size_bytes>());
}

} // namespace jewels::memory
