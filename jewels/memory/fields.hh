// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "jewels/meta/concepts.hh"
#include "jewels/std/functional.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <concepts>
#include <cstddef>
#include <span>
#include <type_traits>

namespace jewels::memory
{

template <typename T>
concept ByteSwappable = std::is_integral_v<T> && (sizeof(T) > 1);

// Functor for decoding big endian values.
struct FromBigEndian
{
  template <ByteSwappable T>
  constexpr T operator()(T value) const;
};

// Functor for decoding little endian values.
struct FromLittleEndian
{
  template <ByteSwappable T>
  constexpr T operator()(T value) const;
};

// Functor for encoding big endian values.
struct ToBigEndian
{
  template <ByteSwappable T>
  constexpr T operator()(T value) const;
};

// Functor for encoding little endian values.
struct ToLittleEndian
{
  template <ByteSwappable T>
  constexpr T operator()(T value) const;
};

/// Checks the transforms are valid for the underlying type.
template <class UnderlyingType, class TransformR, class TransformW>
concept ValidFieldTransform = requires(UnderlyingType value, TransformR transform_r, TransformW transform_w) {
  { transform_r(value) };
  { transform_w(transform_r(value)) } -> jewels::meta::DecaysTo<UnderlyingType>;
};

/// Representation of a set of raw bytes from a data payload. Used to easily read/write components of bitfields.
/// @tparam UnderlyingType The type of the data
/// @tparam offset The offset from the start of the payload to start accessing the data
/// @tparam size The size of the data to access
/// @tparam transform A callable used to apply transformations to the raw field value when reading.
template <
  typename UnderlyingType,
  auto offset,
  auto size,
  auto transform_r = jewels::Identity{},
  auto transform_w = jewels::Identity{}>
  requires ValidFieldTransform<UnderlyingType, decltype(transform_r), decltype(transform_w)>
struct Field
{
  using DestinationType = UnderlyingType;
  static constexpr auto offset_bytes = offset;
  static constexpr auto size_bytes = size;
  static constexpr auto read_transform = transform_r;
  static constexpr auto write_transform = transform_w;
};

/// Field where the size is equal to sizeof(UnderlyingType)
template <typename UnderlyingType, auto offset>
struct BasicField : Field<UnderlyingType, offset, sizeof(UnderlyingType)>
{
};

/// Field with bytes stored big endian.
template <typename UnderlyingType, auto offset, auto size>
struct BigEndianField : Field<UnderlyingType, offset, size, FromBigEndian{}, ToBigEndian{}>
{
};

/// Field with bytes stored little endian.
template <typename UnderlyingType, auto offset, auto size>
struct LittleEndianField : Field<UnderlyingType, offset, size, FromLittleEndian{}, ToLittleEndian{}>
{
};

/// Get underlying value type of a field
template <typename FieldType>
using FieldValueType =
  std::decay_t<std::invoke_result_t<decltype(FieldType::read_transform), typename FieldType::DestinationType>>;

/// A field type that is an array of elements.
template <class ElementFieldTypeIn, auto offset, auto count_in>
struct ArrayField
{
  static_assert(count_in > 0UL);
  static_assert(ElementFieldTypeIn::offset_bytes == 0U, "Sub element field types must not have an offset");

  using ElementFieldType = ElementFieldTypeIn;
  static constexpr auto offset_bytes = offset;
  static constexpr auto count = count_in;
  static constexpr auto size_bytes = ElementFieldTypeIn::size_bytes * count;
};

// Check if a field type is an array field type.
template <class Type>
concept IsArrayField = std::same_as<Type, ArrayField<typename Type::ElementFieldType, Type::offset_bytes, Type::count>>;

/// Calculate the offset from the end of another Field
/// @tparam FieldType The Field to calcluate the offset from
/// @return The offset of the field + the size of the field, used to calculate the offset for the next field in a set
template <typename FieldType>
[[nodiscard]] constexpr auto offset_from();

/// Read a field from a span of bytes
/// @tparam FieldType The type of field to read
/// @tparam (deduced) The size of the span of bytes
/// @param data The span of bytes to read the field from
/// @return The result of bitcasting the data in the region specified by @c FieldType from @c data
template <typename FieldType, auto span_size>
  requires(!IsArrayField<FieldType>)
[[nodiscard]] FieldValueType<FieldType> read_field(std::span<const std::byte, span_size> data);

/// Read an array field from a span of bytes
/// @note If the element type is a const std::byte with no transform, the
/// result is a std::span.  Otherwise it is a transform view over the
/// input bytes.
/// @tparam FieldType The array field type of field to read
/// @tparam (deduced) The size of the span of bytes
/// @param data The span of bytes to read the field from
/// @return A lazy range that reads each field of the array from the byte span.
template <IsArrayField FieldType, auto span_size>
[[nodiscard]] auto read_field(std::span<const std::byte, span_size> data);

/// Write a field into a span of bytes
/// @tparam FieldType The type of field to read
/// @tparam (deduced) The size of the span of bytes
/// @param value The value to write
/// @param data The buffer to write the value into
template <typename FieldType, auto span_size>
  requires(!IsArrayField<FieldType>)
void write_field(const FieldValueType<FieldType>& value, std::span<std::byte, span_size> data);

/// Write an array field into a span of bytes
/// @note This does not support nested arrays.
/// @tparam FieldType The type of field to read
/// @tparam (deduced) The size of the span of bytes
/// @param values The values to write
/// @param data The buffer to write the value into
template <typename FieldType, auto span_size>
  requires(IsArrayField<FieldType>)
void write_field(
  std::span<const FieldValueType<typename FieldType::ElementFieldType>, FieldType::count> values,
  std::span<std::byte, span_size> data);

} // namespace jewels::memory

#include "jewels/memory/fields.inl"
