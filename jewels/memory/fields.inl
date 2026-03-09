// IWYU pragma: private, include "jewels/memory/fields.hh"

#pragma once

#include "jewels/memory/fields.hh"

#include "jewels/memory/bits.hh"
#include "jewels/meta/types.hh" // IWYU pragma: keep
#include "jewels/std/functional.hh"

#include <endian.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <type_traits>

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
  requires(!IsArrayField<FieldType>)
[[nodiscard]] FieldValueType<FieldType> read_field(std::span<const std::byte, span_size> data)
{
  return FieldType::read_transform(
    jewels::memory::bit_cast_to<typename FieldType::DestinationType>(
      data.template subspan<FieldType::offset_bytes, FieldType::size_bytes>()));
}

template <IsArrayField FieldType, auto span_size>
[[nodiscard]] auto read_field(std::span<const std::byte, span_size> data)
{
  if constexpr (std::derived_from<
                  typename FieldType::ElementFieldType,
                  Field<std::byte, 0U, sizeof(std::byte), Identity{}, Identity{}>>)
  {
    // std::byte is the only element we can provide a span for
    // otherwise we have to reinterpret_cast which is UB given valid
    // objects of the element type aren't constructed in the byte
    // span.
    return data.template subspan<FieldType::offset_bytes, FieldType::count>();
  }
  else
  {
    const auto array_field_bytes = data.template subspan<FieldType::offset_bytes, FieldType::size_bytes>();

    return std::ranges::views::transform(
      std::ranges::views::iota(0U, FieldType::count),
      [array_field_bytes](auto index)
      {
        constexpr auto element_size = FieldType::ElementFieldType::size_bytes;
        const auto bytes = array_field_bytes.subspan(index * element_size).template first<element_size>();
        return read_field<typename FieldType::ElementFieldType>(bytes);
      });
  }
}

template <typename FieldType, auto span_size>
  requires(!IsArrayField<FieldType>)
void write_field(const FieldValueType<FieldType>& value, std::span<std::byte, span_size> data)
{
  jewels::memory::write_as_bytes(
    FieldType::write_transform(value), data.template subspan<FieldType::offset_bytes, FieldType::size_bytes>());
}

template <typename FieldType, auto span_size>
  requires(IsArrayField<FieldType>)
void write_field(
  std::span<const FieldValueType<typename FieldType::ElementFieldType>, FieldType::count> values,
  std::span<std::byte, span_size> data)
{
  const auto array_field_bytes = data.template subspan<FieldType::offset_bytes, FieldType::size_bytes>();
  constexpr auto element_size = FieldType::ElementFieldType::size_bytes;
  for (auto index{0U}; index < FieldType::count; ++index)
  {
    const auto bytes = array_field_bytes.subspan(index * element_size).template first<element_size>();
    write_field<typename FieldType::ElementFieldType>(values[index], bytes);
  }
}

} // namespace jewels::memory
