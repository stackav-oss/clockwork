// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/serialization/cpp/clk_builtin_type.hh"

#include "clockwork/serialization/cpp/clk_type.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "jewels/memory/bits.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/uuid/uuid.hh"

#include <fmt10/format.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace clockwork::serialization
{

namespace
{

/// Initializer for map from built in type fully qualified name to type ID
constexpr std::initializer_list<std::pair<const std::string_view, ClkTypeId>> built_int_fqn_to_type_id_initializer = {
  {".Bool", ClkTypeId::boolean},       {".UInt8", ClkTypeId::uint8},
  {".UInt16", ClkTypeId::uint16},      {".UInt32", ClkTypeId::uint32},
  {".UInt64", ClkTypeId::uint64},      {".Int8", ClkTypeId::int8},
  {".Int16", ClkTypeId::int16},        {".Int32", ClkTypeId::int32},
  {".Int64", ClkTypeId::int64},        {".Float32", ClkTypeId::float32},
  {".Float64", ClkTypeId::float64},    {".SyncTime", ClkTypeId::synctime},
  {".Duration", ClkTypeId::duration},  {".Byte", ClkTypeId::byte},
  {".Uuid", ClkTypeId::uuid},          {".FixedArray", ClkTypeId::fixed_array},
  {".VarArray", ClkTypeId::var_array}, {".VarString", ClkTypeId::var_string},
  {".Optional", ClkTypeId::optional},
};

/// Convert the fully qualified name of a built in type to a type ID
/// @param[in] fqn Fully qualified name of a built in type
/// @return Built in type ID
/// @throws runtime_error if the fqn is invalid
[[nodiscard]] ClkTypeId built_in_fqn_to_type_id(std::string_view fqn)
{
  static const std::unordered_map<std::string_view, ClkTypeId> fqn_to_type_id_map{built_int_fqn_to_type_id_initializer};
  const auto id_iter = fqn_to_type_id_map.find(fqn);
  if (id_iter == fqn_to_type_id_map.end())
  {
    throw ClkTypeUpgradeError(fmt::format("Invalid built in type: {}", fqn));
  }
  return id_iter->second;
}

/// Get a float value from a string
/// @param[in] str String to parse
/// @return Floating point value
/// @throws runtime_error if the string is invalid
[[nodiscard]] float string_to_float(const std::string& str)
{
  std::size_t end_pos = 0U;
  float value{};
  try
  {
    value = std::stof(str, &end_pos);
  }
  catch (const std::exception& exc)
  {
    throw ClkTypeUpgradeError(fmt::format("Invalid floating point initializer ('{}')", str));
  }
  if (end_pos != str.size())
  {
    throw ClkTypeUpgradeError(fmt::format("Invalid floating point initializer ('{}')", str));
  }
  return value;
}

/// Get a double value from a string
/// @param[in] str String to parse
/// @return Floating point value
/// @throws runtime_error if the string is invalid
[[nodiscard]] double string_to_double(const std::string& str)
{
  std::size_t end_pos = 0U;
  double value{};
  try
  {
    value = std::stod(str, &end_pos);
  }
  catch (const std::exception& exc)
  {
    throw ClkTypeUpgradeError(fmt::format("Invalid floating point initializer ('{}')", str));
  }
  if (end_pos != str.size())
  {
    throw ClkTypeUpgradeError(fmt::format("Invalid floating point initializer ('{}')", str));
  }
  return value;
}

/// Parse a size parameter from a string
/// @param[in] schema_fqn Fully qualified schema name
/// @param[in] str String to parse
/// @return Size value
/// @throws runtime_error if the string is invalid
[[nodiscard]] size_t parse_size_parameter(std::string_view schema_fqn, const std::string& str)
{
  std::size_t end_pos = 0U;
  size_t value{};
  try
  {
    value = std::stoull(str, &end_pos);
  }
  catch (const std::exception& exc)
  {
    throw ClkTypeUpgradeError(fmt::format("Invalid size parameter ('{}') for {}", str, schema_fqn));
  }
  if (end_pos != str.size())
  {
    throw ClkTypeUpgradeError(fmt::format("Invalid size parameter ('{}') for {}", str, schema_fqn));
  }
  return value;
}

/// Upgrade an integer value from SrcValueType to DestValueType
/// @tparam SrcValueType Source value type
/// @tparam DestValueType Destination value type
/// @param[in] src_span Source value span
/// @param[in] dest_span Destination value span
template <typename SrcValueType, typename DestValueType>
void upgrade_integer(std::span<const std::byte> src_span, std::span<std::byte> dest_span)
{
  const auto src_value = jewels::memory::bit_cast_to<SrcValueType>(
    std::span<const std::byte, sizeof(SrcValueType)>{src_span.first(sizeof(SrcValueType))});
  jewels::memory::write_as_bytes(
    static_cast<DestValueType>(src_value),
    std::span<std::byte, sizeof(DestValueType)>{dest_span.first(sizeof(DestValueType))});
}

/// Upgrade a floating point value from SrcValueType to DestValueType
/// @tparam SrcValueType Source value type
/// @tparam DestValueType Destination value type
/// @param[in] src_span Source value span
/// @param[in] dest_span Destination value span
template <typename SrcValueType, typename DestValueType>
void upgrade_floating_point(std::span<const std::byte> src_span, std::span<std::byte> dest_span)
{
  const auto src_value = jewels::memory::bit_cast_to<SrcValueType>(
    std::span<const std::byte, sizeof(SrcValueType)>{src_span.first(sizeof(SrcValueType))});
  jewels::memory::write_as_bytes(
    static_cast<DestValueType>(src_value),
    std::span<std::byte, sizeof(DestValueType)>{dest_span.first(sizeof(DestValueType))});
}

/// Clockwork upgrader for primitive types
/// @tparam ValueType Value type
template <typename ValueType>
class ClkPrimitiveUpgrader : public ClkTypeUpgrader
{
public:
  ClkPrimitiveUpgrader() noexcept = default;

  ~ClkPrimitiveUpgrader() noexcept override = default;

  ClkPrimitiveUpgrader(const ClkPrimitiveUpgrader&) = delete;
  ClkPrimitiveUpgrader& operator=(const ClkPrimitiveUpgrader&) = delete;
  ClkPrimitiveUpgrader(ClkPrimitiveUpgrader&&) = delete;
  ClkPrimitiveUpgrader& operator=(ClkPrimitiveUpgrader&&) = delete;

  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;
};

template <typename ValueType>
void ClkPrimitiveUpgrader<ValueType>::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  if constexpr (sizeof(ValueType) == 1U)
  {
    dest_span[0U] = src_span[0U];
  }
  else
  {
    std::memcpy(dest_span.data(), src_span.data(), sizeof(ValueType));
  }
}

/// Clockwork upgrader for integer types
/// @tparam SrcValueType Source value type
/// @tparam DestValueType Destination value type
template <typename SrcValueType, typename DestValueType>
class ClkIntegerUpgrader : public ClkTypeUpgrader
{
public:
  ClkIntegerUpgrader() noexcept = default;

  ~ClkIntegerUpgrader() noexcept override = default;

  ClkIntegerUpgrader(const ClkIntegerUpgrader&) = delete;
  ClkIntegerUpgrader& operator=(const ClkIntegerUpgrader&) = delete;
  ClkIntegerUpgrader(ClkIntegerUpgrader&&) = delete;
  ClkIntegerUpgrader& operator=(ClkIntegerUpgrader&&) = delete;

  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;
};

template <typename SrcValueType, typename DestValueType>
void ClkIntegerUpgrader<SrcValueType, DestValueType>::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  upgrade_integer<SrcValueType, DestValueType>(src_span, dest_span);
}

/// Clockwork upgrader for floating point types
/// @tparam SrcValueType Source value type
/// @tparam DestValueType Destination value type
template <typename SrcValueType, typename DestValueType>
class ClkFloatingPointUpgrader : public ClkTypeUpgrader
{
public:
  ClkFloatingPointUpgrader() noexcept = default;

  ~ClkFloatingPointUpgrader() noexcept override = default;

  ClkFloatingPointUpgrader(const ClkFloatingPointUpgrader&) = delete;
  ClkFloatingPointUpgrader& operator=(const ClkFloatingPointUpgrader&) = delete;
  ClkFloatingPointUpgrader(ClkFloatingPointUpgrader&&) = delete;
  ClkFloatingPointUpgrader& operator=(ClkFloatingPointUpgrader&&) = delete;

  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;
};

template <typename SrcValueType, typename DestValueType>
void ClkFloatingPointUpgrader<SrcValueType, DestValueType>::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  upgrade_floating_point<SrcValueType, DestValueType>(src_span, dest_span);
}

/// Clockwork upgrader for fixed array types
class ClkFixedArrayFromFixedArrayUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] src_array_size Source array size
  /// @param[in] array_upgrader Array upgrader
  ClkFixedArrayFromFixedArrayUpgrader(size_t src_array_size, std::unique_ptr<ClkArrayUpgrader> array_upgrader);

  ~ClkFixedArrayFromFixedArrayUpgrader() noexcept override = default;

  ClkFixedArrayFromFixedArrayUpgrader(const ClkFixedArrayFromFixedArrayUpgrader&) = delete;
  ClkFixedArrayFromFixedArrayUpgrader& operator=(const ClkFixedArrayFromFixedArrayUpgrader&) = delete;
  ClkFixedArrayFromFixedArrayUpgrader(ClkFixedArrayFromFixedArrayUpgrader&&) = delete;
  ClkFixedArrayFromFixedArrayUpgrader& operator=(ClkFixedArrayFromFixedArrayUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source array size
  size_t src_array_size_;

  /// Array upgrader
  std::unique_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkFixedArrayFromFixedArrayUpgrader::ClkFixedArrayFromFixedArrayUpgrader(
  size_t src_array_size, std::unique_ptr<ClkArrayUpgrader> array_upgrader)
  : src_array_size_(src_array_size), array_upgrader_(std::move(array_upgrader))
{
}

void ClkFixedArrayFromFixedArrayUpgrader::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  array_upgrader_->upgrade(src_array_size_, src_span, dest_span);
}

/// Clockwork uprader to a fixed array from a variable array
class ClkFixedArrayFromVarArrayUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] src_size_offset Source size offset
  /// @param[in] array_upgrader Array upgrader
  ClkFixedArrayFromVarArrayUpgrader(size_t src_size_offset, std::unique_ptr<ClkArrayUpgrader> array_upgrader);

  ~ClkFixedArrayFromVarArrayUpgrader() noexcept override = default;

  ClkFixedArrayFromVarArrayUpgrader(const ClkFixedArrayFromVarArrayUpgrader&) = delete;
  ClkFixedArrayFromVarArrayUpgrader& operator=(const ClkFixedArrayFromVarArrayUpgrader&) = delete;
  ClkFixedArrayFromVarArrayUpgrader(ClkFixedArrayFromVarArrayUpgrader&&) = delete;
  ClkFixedArrayFromVarArrayUpgrader& operator=(ClkFixedArrayFromVarArrayUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source size offset
  size_t src_size_offset_;

  /// Array upgrader
  std::unique_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkFixedArrayFromVarArrayUpgrader::ClkFixedArrayFromVarArrayUpgrader(
  size_t src_size_offset, std::unique_ptr<ClkArrayUpgrader> array_upgrader)
  : src_size_offset_(src_size_offset), array_upgrader_(std::move(array_upgrader))
{
}

void ClkFixedArrayFromVarArrayUpgrader::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const auto src_array_size = jewels::memory::bit_cast_to<uint64_t>(
    std::span<const std::byte, sizeof(uint64_t)>{src_span.subspan(src_size_offset_, sizeof(uint64_t))});
  array_upgrader_->upgrade(src_array_size, src_span, dest_span);
}

/// Clockwork uprader to a fixed array from a variable array
class ClkFixedArrayFromVarStringUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] array_size Fixed array size
  /// @param[in] src_size_offset Source size offset
  ClkFixedArrayFromVarStringUpgrader(size_t array_size, size_t src_size_offset);

  ~ClkFixedArrayFromVarStringUpgrader() noexcept override = default;

  ClkFixedArrayFromVarStringUpgrader(const ClkFixedArrayFromVarStringUpgrader&) = delete;
  ClkFixedArrayFromVarStringUpgrader& operator=(const ClkFixedArrayFromVarStringUpgrader&) = delete;
  ClkFixedArrayFromVarStringUpgrader(ClkFixedArrayFromVarStringUpgrader&&) = delete;
  ClkFixedArrayFromVarStringUpgrader& operator=(ClkFixedArrayFromVarStringUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Fixed array size
  size_t array_size_;

  /// Source size offset
  size_t src_size_offset_;
};

ClkFixedArrayFromVarStringUpgrader::ClkFixedArrayFromVarStringUpgrader(size_t array_size, size_t src_size_offset)
  : array_size_(array_size), src_size_offset_(src_size_offset)
{
}

void ClkFixedArrayFromVarStringUpgrader::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const auto src_string_size = jewels::memory::bit_cast_to<uint64_t>(
    std::span<const std::byte, sizeof(uint64_t)>{src_span.subspan(src_size_offset_, sizeof(uint64_t))});
  if (src_string_size != array_size_)
  {
    throw ClkTypeUpgradeError(
      fmt::format("String size {} does not match fixed array size {}", src_string_size, array_size_));
  }
  std::memcpy(dest_span.data(), src_span.data(), array_size_);
}

/// Clockwork uprader to a fixed array from an optional
class ClkFixedArrayFromOptionalUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] src_has_value_offset Source has value flag offset
  /// @param[in] array_upgrader Array upgrader
  ClkFixedArrayFromOptionalUpgrader(size_t src_has_value_offset, std::unique_ptr<ClkArrayUpgrader> array_upgrader);

  ~ClkFixedArrayFromOptionalUpgrader() noexcept override = default;

  ClkFixedArrayFromOptionalUpgrader(const ClkFixedArrayFromOptionalUpgrader&) = delete;
  ClkFixedArrayFromOptionalUpgrader& operator=(const ClkFixedArrayFromOptionalUpgrader&) = delete;
  ClkFixedArrayFromOptionalUpgrader(ClkFixedArrayFromOptionalUpgrader&&) = delete;
  ClkFixedArrayFromOptionalUpgrader& operator=(ClkFixedArrayFromOptionalUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source has value flag offset
  size_t src_has_value_offset_;

  /// Array upgrader
  std::unique_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkFixedArrayFromOptionalUpgrader::ClkFixedArrayFromOptionalUpgrader(
  size_t src_has_value_offset, std::unique_ptr<ClkArrayUpgrader> array_upgrader)
  : src_has_value_offset_(src_has_value_offset), array_upgrader_(std::move(array_upgrader))
{
}

void ClkFixedArrayFromOptionalUpgrader::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const auto src_has_value = src_span[src_has_value_offset_];
  array_upgrader_->upgrade(src_has_value == std::byte{0} ? 0U : 1U, src_span, dest_span);
}

/// Clockwork uprader to a fixed array from an element type
class ClkFixedArrayFromElementUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] array_upgrader Array upgrader
  explicit ClkFixedArrayFromElementUpgrader(std::unique_ptr<ClkArrayUpgrader> array_upgrader);

  ~ClkFixedArrayFromElementUpgrader() noexcept override = default;

  ClkFixedArrayFromElementUpgrader(const ClkFixedArrayFromElementUpgrader&) = delete;
  ClkFixedArrayFromElementUpgrader& operator=(const ClkFixedArrayFromElementUpgrader&) = delete;
  ClkFixedArrayFromElementUpgrader(ClkFixedArrayFromElementUpgrader&&) = delete;
  ClkFixedArrayFromElementUpgrader& operator=(ClkFixedArrayFromElementUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Array upgrader
  std::unique_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkFixedArrayFromElementUpgrader::ClkFixedArrayFromElementUpgrader(std::unique_ptr<ClkArrayUpgrader> array_upgrader)
  : array_upgrader_(std::move(array_upgrader))
{
}

void ClkFixedArrayFromElementUpgrader::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  array_upgrader_->upgrade(1U, src_span, dest_span);
}

/// Clockwork upgrader for variable array types
class ClkVarArrayFromVarArrayUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] src_size_offset Source array size offset
  /// @param[in] dest_size_offset Destination array size offset
  /// @param[in] array_upgrader Array upgrader
  ClkVarArrayFromVarArrayUpgrader(
    size_t src_size_offset, size_t dest_size_offset, std::unique_ptr<ClkArrayUpgrader> array_upgrader);

  ~ClkVarArrayFromVarArrayUpgrader() noexcept override = default;

  ClkVarArrayFromVarArrayUpgrader(const ClkVarArrayFromVarArrayUpgrader&) = delete;
  ClkVarArrayFromVarArrayUpgrader& operator=(const ClkVarArrayFromVarArrayUpgrader&) = delete;
  ClkVarArrayFromVarArrayUpgrader(ClkVarArrayFromVarArrayUpgrader&&) = delete;
  ClkVarArrayFromVarArrayUpgrader& operator=(ClkVarArrayFromVarArrayUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source size offset
  size_t src_size_offset_;

  /// Destination size offset
  size_t dest_size_offset_;

  /// Array upgrader
  std::unique_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkVarArrayFromVarArrayUpgrader::ClkVarArrayFromVarArrayUpgrader(
  size_t src_size_offset, size_t dest_size_offset, std::unique_ptr<ClkArrayUpgrader> array_upgrader)
  : src_size_offset_(src_size_offset), dest_size_offset_(dest_size_offset), array_upgrader_(std::move(array_upgrader))
{
}

void ClkVarArrayFromVarArrayUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const auto src_array_size = jewels::memory::bit_cast_to<uint64_t>(
    std::span<const std::byte, sizeof(uint64_t)>{src_span.subspan(src_size_offset_, sizeof(uint64_t))});
  array_upgrader_->upgrade(src_array_size, src_span, dest_span);
  jewels::memory::write_as_bytes(
    src_array_size, std::span<std::byte, sizeof(uint64_t)>{dest_span.subspan(dest_size_offset_, sizeof(uint64_t))});
}

/// Clockwork upgrader to a variable array from a fixed array type
class ClkVarArrayFromFixedArrayUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] src_array_size Source array size
  /// @param[in] dest_size_offset Destination array size offset
  /// @param[in] array_upgrader Array upgrader
  ClkVarArrayFromFixedArrayUpgrader(
    size_t src_array_size, size_t dest_size_offset, std::unique_ptr<ClkArrayUpgrader> array_upgrader);

  ~ClkVarArrayFromFixedArrayUpgrader() noexcept override = default;

  ClkVarArrayFromFixedArrayUpgrader(const ClkVarArrayFromFixedArrayUpgrader&) = delete;
  ClkVarArrayFromFixedArrayUpgrader& operator=(const ClkVarArrayFromFixedArrayUpgrader&) = delete;
  ClkVarArrayFromFixedArrayUpgrader(ClkVarArrayFromFixedArrayUpgrader&&) = delete;
  ClkVarArrayFromFixedArrayUpgrader& operator=(ClkVarArrayFromFixedArrayUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source array size
  uint64_t src_array_size_;

  /// Destination size offset
  size_t dest_size_offset_;

  /// Array upgrader
  std::unique_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkVarArrayFromFixedArrayUpgrader::ClkVarArrayFromFixedArrayUpgrader(
  size_t src_array_size, size_t dest_size_offset, std::unique_ptr<ClkArrayUpgrader> array_upgrader)
  : src_array_size_(src_array_size), dest_size_offset_(dest_size_offset), array_upgrader_(std::move(array_upgrader))
{
}

void ClkVarArrayFromFixedArrayUpgrader::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  array_upgrader_->upgrade(src_array_size_, src_span, dest_span);
  jewels::memory::write_as_bytes(
    src_array_size_, std::span<std::byte, sizeof(uint64_t)>{dest_span.subspan(dest_size_offset_, sizeof(uint64_t))});
}

/// Clockwork uprader to a fixed array from a variable string
class ClkVarArrayFromVarStringUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] max_array_size Maximum array size
  /// @param[in] src_size_offset Source size offset
  /// @param[in] dest_size_offset Source size offset
  ClkVarArrayFromVarStringUpgrader(size_t max_array_size, size_t src_size_offset, size_t dest_size_offset);

  ~ClkVarArrayFromVarStringUpgrader() noexcept override = default;

  ClkVarArrayFromVarStringUpgrader(const ClkVarArrayFromVarStringUpgrader&) = delete;
  ClkVarArrayFromVarStringUpgrader& operator=(const ClkVarArrayFromVarStringUpgrader&) = delete;
  ClkVarArrayFromVarStringUpgrader(ClkVarArrayFromVarStringUpgrader&&) = delete;
  ClkVarArrayFromVarStringUpgrader& operator=(ClkVarArrayFromVarStringUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Maximum array size
  size_t max_array_size_;

  /// Source size offset
  size_t src_size_offset_;

  /// Destination size offset
  size_t dest_size_offset_;
};

ClkVarArrayFromVarStringUpgrader::ClkVarArrayFromVarStringUpgrader(
  size_t max_array_size, size_t src_size_offset, size_t dest_size_offset)
  : max_array_size_(max_array_size), src_size_offset_(src_size_offset), dest_size_offset_(dest_size_offset)
{
}

void ClkVarArrayFromVarStringUpgrader::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const auto src_string_size = jewels::memory::bit_cast_to<uint64_t>(
    std::span<const std::byte, sizeof(uint64_t)>{src_span.subspan(src_size_offset_, sizeof(uint64_t))});
  if (src_string_size > max_array_size_)
  {
    throw ClkTypeUpgradeError(
      fmt::format("String size {} greater than max array size {}", src_string_size, max_array_size_));
  }
  std::memcpy(dest_span.data(), src_span.data(), src_string_size);
  jewels::memory::write_as_bytes(
    src_string_size, std::span<std::byte, sizeof(uint64_t)>{dest_span.subspan(dest_size_offset_, sizeof(uint64_t))});
}

/// Clockwork uprader to a variable array from an optional
class ClkVarArrayFromOptionalUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] src_has_value_offset Source has value flag offset
  /// @param[in] dest_size_offset Destination array size offset
  /// @param[in] array_upgrader Array upgrader
  ClkVarArrayFromOptionalUpgrader(
    size_t src_has_value_offset, size_t dest_size_offset, std::unique_ptr<ClkArrayUpgrader> array_upgrader);

  ~ClkVarArrayFromOptionalUpgrader() noexcept override = default;

  ClkVarArrayFromOptionalUpgrader(const ClkVarArrayFromOptionalUpgrader&) = delete;
  ClkVarArrayFromOptionalUpgrader& operator=(const ClkVarArrayFromOptionalUpgrader&) = delete;
  ClkVarArrayFromOptionalUpgrader(ClkVarArrayFromOptionalUpgrader&&) = delete;
  ClkVarArrayFromOptionalUpgrader& operator=(ClkVarArrayFromOptionalUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source has value flag offset
  size_t src_has_value_offset_;

  /// Destination size offset
  size_t dest_size_offset_;

  /// Array upgrader
  std::unique_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkVarArrayFromOptionalUpgrader::ClkVarArrayFromOptionalUpgrader(
  size_t src_has_value_offset, size_t dest_size_offset, std::unique_ptr<ClkArrayUpgrader> array_upgrader)
  : src_has_value_offset_(src_has_value_offset),
    dest_size_offset_(dest_size_offset),
    array_upgrader_(std::move(array_upgrader))
{
}

void ClkVarArrayFromOptionalUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const uint64_t src_array_size = src_span[src_has_value_offset_] == std::byte{0} ? 0U : 1U;
  array_upgrader_->upgrade(src_array_size, src_span, dest_span);
  jewels::memory::write_as_bytes(
    src_array_size, std::span<std::byte, sizeof(uint64_t)>{dest_span.subspan(dest_size_offset_, sizeof(uint64_t))});
}

/// Clockwork uprader to a variable array from an element type
class ClkVarArrayFromElementUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] dest_size_offset Destination array size offset
  /// @param[in] array_upgrader Array upgrader
  ClkVarArrayFromElementUpgrader(size_t dest_size_offset, std::unique_ptr<ClkArrayUpgrader> array_upgrader);

  ~ClkVarArrayFromElementUpgrader() noexcept override = default;

  ClkVarArrayFromElementUpgrader(const ClkVarArrayFromElementUpgrader&) = delete;
  ClkVarArrayFromElementUpgrader& operator=(const ClkVarArrayFromElementUpgrader&) = delete;
  ClkVarArrayFromElementUpgrader(ClkVarArrayFromElementUpgrader&&) = delete;
  ClkVarArrayFromElementUpgrader& operator=(ClkVarArrayFromElementUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Destination size offset
  size_t dest_size_offset_;

  /// Array upgrader
  std::unique_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkVarArrayFromElementUpgrader::ClkVarArrayFromElementUpgrader(
  size_t dest_size_offset, std::unique_ptr<ClkArrayUpgrader> array_upgrader)
  : dest_size_offset_(dest_size_offset), array_upgrader_(std::move(array_upgrader))
{
}

void ClkVarArrayFromElementUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const uint64_t src_array_size = 1U;
  array_upgrader_->upgrade(src_array_size, src_span, dest_span);
  jewels::memory::write_as_bytes(
    src_array_size, std::span<std::byte, sizeof(uint64_t)>{dest_span.subspan(dest_size_offset_, sizeof(uint64_t))});
}

/// Upgrade a variable string type from another variable string type
class ClkVarStringFromVarStringUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] max_string_size Maximum string size
  /// @param[in] src_size_offset Source string size offset
  /// @param[in] dest_size_offset Destination string size offset
  ClkVarStringFromVarStringUpgrader(size_t max_string_size, size_t src_size_offset, size_t dest_size_offset);

  ~ClkVarStringFromVarStringUpgrader() noexcept override = default;

  ClkVarStringFromVarStringUpgrader(const ClkVarStringFromVarStringUpgrader&) = delete;
  ClkVarStringFromVarStringUpgrader& operator=(const ClkVarStringFromVarStringUpgrader&) = delete;
  ClkVarStringFromVarStringUpgrader(ClkVarStringFromVarStringUpgrader&&) = delete;
  ClkVarStringFromVarStringUpgrader& operator=(ClkVarStringFromVarStringUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Maximum string size
  size_t max_string_size_;

  /// Source size offset
  size_t src_size_offset_;

  /// Destination size offset
  size_t dest_size_offset_;
};

ClkVarStringFromVarStringUpgrader::ClkVarStringFromVarStringUpgrader(
  size_t max_string_size, size_t src_size_offset, size_t dest_size_offset)
  : max_string_size_(max_string_size), src_size_offset_(src_size_offset), dest_size_offset_(dest_size_offset)
{
}

void ClkVarStringFromVarStringUpgrader::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const auto src_size = jewels::memory::bit_cast_to<uint64_t>(
    std::span<const std::byte, sizeof(uint64_t)>{src_span.subspan(src_size_offset_, sizeof(uint64_t))});
  if (src_size > max_string_size_)
  {
    throw ClkTypeUpgradeError(
      fmt::format("Source string size ({}) exceeds max destination size ({})", src_size, max_string_size_));
  }
  std::memcpy(dest_span.data(), src_span.data(), src_size);
  jewels::memory::write_as_bytes(
    src_size, std::span<std::byte, sizeof(uint64_t)>{dest_span.subspan(dest_size_offset_, sizeof(uint64_t))});
}

/// Upgrade a variable string type from a fixed array type
class ClkVarStringFromFixedArrayUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] max_string_size Maximum string size
  /// @param[in] src_array_size Source array size
  /// @param[in] dest_size_offset Destination string size offset
  ClkVarStringFromFixedArrayUpgrader(size_t max_string_size, size_t src_array_size, size_t dest_size_offset);

  ~ClkVarStringFromFixedArrayUpgrader() noexcept override = default;

  ClkVarStringFromFixedArrayUpgrader(const ClkVarStringFromFixedArrayUpgrader&) = delete;
  ClkVarStringFromFixedArrayUpgrader& operator=(const ClkVarStringFromFixedArrayUpgrader&) = delete;
  ClkVarStringFromFixedArrayUpgrader(ClkVarStringFromFixedArrayUpgrader&&) = delete;
  ClkVarStringFromFixedArrayUpgrader& operator=(ClkVarStringFromFixedArrayUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Maximum string size
  size_t max_string_size_;

  /// Source array size
  uint64_t src_array_size_;

  /// Destination size offset
  size_t dest_size_offset_;
};

ClkVarStringFromFixedArrayUpgrader::ClkVarStringFromFixedArrayUpgrader(
  size_t max_string_size, size_t src_array_size, size_t dest_size_offset)
  : max_string_size_(max_string_size), src_array_size_(src_array_size), dest_size_offset_(dest_size_offset)
{
}

void ClkVarStringFromFixedArrayUpgrader::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  if (src_array_size_ > max_string_size_)
  {
    throw ClkTypeUpgradeError(
      fmt::format("Source string size ({}) exceeds max destination size ({})", src_array_size_, max_string_size_));
  }
  std::memcpy(dest_span.data(), src_span.data(), src_array_size_);
  jewels::memory::write_as_bytes(
    src_array_size_, std::span<std::byte, sizeof(uint64_t)>{dest_span.subspan(dest_size_offset_, sizeof(uint64_t))});
}

/// Upgrade a variable string type from an optional type
class ClkVarStringFromOptionalUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] src_has_value_offset Source has value flag offset
  /// @param[in] dest_size_offset Destination string size offset
  ClkVarStringFromOptionalUpgrader(size_t src_has_value_offset, size_t dest_size_offset);

  ~ClkVarStringFromOptionalUpgrader() noexcept override = default;

  ClkVarStringFromOptionalUpgrader(const ClkVarStringFromOptionalUpgrader&) = delete;
  ClkVarStringFromOptionalUpgrader& operator=(const ClkVarStringFromOptionalUpgrader&) = delete;
  ClkVarStringFromOptionalUpgrader(ClkVarStringFromOptionalUpgrader&&) = delete;
  ClkVarStringFromOptionalUpgrader& operator=(ClkVarStringFromOptionalUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source has value flag offset
  size_t src_has_value_offset_;

  /// Destination size offset
  size_t dest_size_offset_;
};

ClkVarStringFromOptionalUpgrader::ClkVarStringFromOptionalUpgrader(size_t src_has_value_offset, size_t dest_size_offset)
  : src_has_value_offset_(src_has_value_offset), dest_size_offset_(dest_size_offset)
{
}

void ClkVarStringFromOptionalUpgrader::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  if (src_span[src_has_value_offset_] != std::byte{0})
  {
    dest_span[0] = src_span[0];
    const uint64_t string_size{1U};
    jewels::memory::write_as_bytes(
      string_size, std::span<std::byte, sizeof(uint64_t)>{dest_span.subspan(dest_size_offset_, sizeof(uint64_t))});
  }
}

/// Clockwork upgrader to upgrade optional types from optional types
class ClkOptionalFromOptionalUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] src_has_value_offset Source has value offset
  /// @param[in] dest_has_value_offset Destination has value offset
  /// @param[in] element_ugprader Element upgrader
  ClkOptionalFromOptionalUpgrader(
    size_t src_has_value_offset,
    size_t dest_has_value_offset,
    jewels::memory::ObjectPtr<const ClkTypeUpgrader> element_upgrader);

  ~ClkOptionalFromOptionalUpgrader() noexcept override = default;

  ClkOptionalFromOptionalUpgrader(const ClkOptionalFromOptionalUpgrader&) = delete;
  ClkOptionalFromOptionalUpgrader& operator=(const ClkOptionalFromOptionalUpgrader&) = delete;
  ClkOptionalFromOptionalUpgrader(ClkOptionalFromOptionalUpgrader&&) = delete;
  ClkOptionalFromOptionalUpgrader& operator=(ClkOptionalFromOptionalUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source has_value offset
  size_t src_has_value_offset_;

  /// Destination has value offset
  size_t dest_has_value_offset_;

  /// Element upgrader
  jewels::memory::ObjectPtr<const ClkTypeUpgrader> element_upgrader_;
};

ClkOptionalFromOptionalUpgrader::ClkOptionalFromOptionalUpgrader(
  size_t src_has_value_offset,
  size_t dest_has_value_offset,
  jewels::memory::ObjectPtr<const ClkTypeUpgrader> element_upgrader)
  : src_has_value_offset_(src_has_value_offset),
    dest_has_value_offset_(dest_has_value_offset),
    element_upgrader_(element_upgrader)
{
}

void ClkOptionalFromOptionalUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const auto src_has_value = src_span[src_has_value_offset_];
  if (src_has_value != std::byte{0})
  {
    element_upgrader_->upgrade(src_span, dest_span);
    dest_span[dest_has_value_offset_] = std::byte{1};
  }
}

/// Clockwork upgrader to upgrade optional types from fixed array types
class ClkOptionalFromFixedArrayUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] src_array_size Source array size
  /// @param[in] dest_has_value_offset Destination has value offset
  /// @param[in] element_ugprader Element upgrader
  ClkOptionalFromFixedArrayUpgrader(
    size_t src_array_size,
    size_t dest_has_value_offset,
    jewels::memory::ObjectPtr<const ClkTypeUpgrader> element_upgrader);

  ~ClkOptionalFromFixedArrayUpgrader() noexcept override = default;

  ClkOptionalFromFixedArrayUpgrader(const ClkOptionalFromFixedArrayUpgrader&) = delete;
  ClkOptionalFromFixedArrayUpgrader& operator=(const ClkOptionalFromFixedArrayUpgrader&) = delete;
  ClkOptionalFromFixedArrayUpgrader(ClkOptionalFromFixedArrayUpgrader&&) = delete;
  ClkOptionalFromFixedArrayUpgrader& operator=(ClkOptionalFromFixedArrayUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source has_value offset
  size_t src_array_size_;

  /// Destination has value offset
  size_t dest_has_value_offset_;

  /// Element upgrader
  jewels::memory::ObjectPtr<const ClkTypeUpgrader> element_upgrader_;
};

ClkOptionalFromFixedArrayUpgrader::ClkOptionalFromFixedArrayUpgrader(
  size_t src_array_size,
  size_t dest_has_value_offset,
  jewels::memory::ObjectPtr<const ClkTypeUpgrader> element_upgrader)
  : src_array_size_(src_array_size), dest_has_value_offset_(dest_has_value_offset), element_upgrader_(element_upgrader)
{
}

void ClkOptionalFromFixedArrayUpgrader::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  if (src_array_size_ != 1U)
  {
    throw ClkTypeUpgradeError(fmt::format("Source array size ({}) is greater than one", src_array_size_));
  }
  element_upgrader_->upgrade(src_span, dest_span);
  dest_span[dest_has_value_offset_] = std::byte{1};
}

/// Clockwork upgrader to upgrade optional types from variable array types
class ClkOptionalFromVarArrayUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] src_size_offset Source array size offset
  /// @param[in] dest_has_value_offset Destination has value offset
  /// @param[in] element_ugprader Element upgrader
  ClkOptionalFromVarArrayUpgrader(
    size_t src_size_offset,
    size_t dest_has_value_offset,
    jewels::memory::ObjectPtr<const ClkTypeUpgrader> element_upgrader);

  ~ClkOptionalFromVarArrayUpgrader() noexcept override = default;

  ClkOptionalFromVarArrayUpgrader(const ClkOptionalFromVarArrayUpgrader&) = delete;
  ClkOptionalFromVarArrayUpgrader& operator=(const ClkOptionalFromVarArrayUpgrader&) = delete;
  ClkOptionalFromVarArrayUpgrader(ClkOptionalFromVarArrayUpgrader&&) = delete;
  ClkOptionalFromVarArrayUpgrader& operator=(ClkOptionalFromVarArrayUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source array size offset
  size_t src_size_offset_;

  /// Destination has value offset
  size_t dest_has_value_offset_;

  /// Element upgrader
  jewels::memory::ObjectPtr<const ClkTypeUpgrader> element_upgrader_;
};

ClkOptionalFromVarArrayUpgrader::ClkOptionalFromVarArrayUpgrader(
  size_t src_size_offset,
  size_t dest_has_value_offset,
  jewels::memory::ObjectPtr<const ClkTypeUpgrader> element_upgrader)
  : src_size_offset_(src_size_offset),
    dest_has_value_offset_(dest_has_value_offset),
    element_upgrader_(element_upgrader)
{
}

void ClkOptionalFromVarArrayUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const auto src_size = jewels::memory::bit_cast_to<uint64_t>(
    std::span<const std::byte, sizeof(uint64_t)>{src_span.subspan(src_size_offset_, sizeof(uint64_t))});
  if (src_size > 1U)
  {
    throw ClkTypeUpgradeError(fmt::format("Source size ({}) is greater than one", src_size));
  }
  if (src_size != 0U)
  {
    element_upgrader_->upgrade(src_span, dest_span);
    dest_span[dest_has_value_offset_] = std::byte{1};
  }
}

/// Clockwork upgrader to upgrade optional types from variable string types
class ClkOptionalFromVarStringUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] src_size_offset Source array size offset
  /// @param[in] dest_has_value_offset Destination has value offset
  ClkOptionalFromVarStringUpgrader(size_t src_size_offset, size_t dest_has_value_offset);

  ~ClkOptionalFromVarStringUpgrader() noexcept override = default;

  ClkOptionalFromVarStringUpgrader(const ClkOptionalFromVarStringUpgrader&) = delete;
  ClkOptionalFromVarStringUpgrader& operator=(const ClkOptionalFromVarStringUpgrader&) = delete;
  ClkOptionalFromVarStringUpgrader(ClkOptionalFromVarStringUpgrader&&) = delete;
  ClkOptionalFromVarStringUpgrader& operator=(ClkOptionalFromVarStringUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source string size offset
  size_t src_size_offset_;

  /// Destination has value offset
  size_t dest_has_value_offset_;
};

ClkOptionalFromVarStringUpgrader::ClkOptionalFromVarStringUpgrader(size_t src_size_offset, size_t dest_has_value_offset)
  : src_size_offset_(src_size_offset), dest_has_value_offset_(dest_has_value_offset)
{
}

void ClkOptionalFromVarStringUpgrader::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const auto src_size = jewels::memory::bit_cast_to<uint64_t>(
    std::span<const std::byte, sizeof(uint64_t)>{src_span.subspan(src_size_offset_, sizeof(uint64_t))});
  if (src_size > 1U)
  {
    throw ClkTypeUpgradeError(fmt::format("Source size ({}) is greater than one", src_size));
  }
  if (src_size != 0U)
  {
    dest_span[0U] = src_span[0U];
    dest_span[dest_has_value_offset_] = std::byte{1};
  }
}

/// Clockwork tag type (tag types are never upgraded)
class ClkTagType : public ClkType
{
public:
  using ClkType::ClkType;

  /// @return Type size in bytes (always returns 1)
  [[nodiscard]] size_t get_size() const noexcept override;

  /// @return Type alignment in bytes (always returns 1)
  [[nodiscard]] size_t get_alignment() const noexcept override;

  /// Always throws an exception, tag types are never upgraded
  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::is_legacy_wire_compatible
  [[nodiscard]] bool is_legacy_wire_compatible(const ClkType& src_type) const override;

  /// @see ClkType::check_for_unexpected_schema_changes
  void check_for_unexpected_schema_changes(const ClkType& src_type, std::string_view name) override;
};

[[nodiscard]] size_t ClkTagType::get_size() const noexcept
{
  return 1U;
}

[[nodiscard]] size_t ClkTagType::get_alignment() const noexcept
{
  return 1U;
}

[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> ClkTagType::make_upgrader(const ClkType& /*src_type*/)
{
  throw ClkTypeUpgradeError("Internal error: Tag types cannot be upgraded");
}

[[nodiscard]] bool ClkTagType::is_legacy_wire_compatible(const ClkType& src_type) const
{
  return src_type.get_type_id() == ClkTypeId::tag;
}

void ClkTagType::check_for_unexpected_schema_changes(const ClkType& src_type, std::string_view name)
{
  if (src_type.get_type_id() != get_type_id())
  {
    throw ClkTypeUpgradeError(
      fmt::format("Unexpected type change from {} to {} for {}", src_type.get_type_id(), get_type_id(), name));
  }
}

/// Virtual clockwork built in type representation
class ClkBuiltInType : public ClkType
{
public:
  /// Constructor
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  ClkBuiltInType(std::string_view fqn, ClkTypeId type_id, size_t type_index, size_t size, size_t alignment);

  ~ClkBuiltInType() override = default;

  ClkBuiltInType(const ClkBuiltInType&) = delete;
  ClkBuiltInType& operator=(const ClkBuiltInType&) = delete;
  ClkBuiltInType(ClkBuiltInType&&) = delete;
  ClkBuiltInType& operator=(ClkBuiltInType&&) = delete;

  /// @return Type size in bytes
  [[nodiscard]] size_t get_size() const noexcept override;

  /// @return Type alignment in bytes
  [[nodiscard]] size_t get_alignment() const noexcept override;

  /// @see ClkType::is_legacy_wire_compatible
  [[nodiscard]] bool is_legacy_wire_compatible(const ClkType& src_type) const override;

  /// @see ClkType::check_for_unexpected_schema_changes
  void check_for_unexpected_schema_changes(const ClkType& src_type, std::string_view name) override;

private:
  /// Type size in bytes
  size_t size_;

  /// Type alignment in bytes
  size_t alignment_;
};

ClkBuiltInType::ClkBuiltInType(
  std::string_view fqn, ClkTypeId type_id, size_t type_index, size_t size, size_t alignment)
  : ClkType(fqn, type_id, type_index), size_(size), alignment_(alignment)
{
}

[[nodiscard]] size_t ClkBuiltInType::get_size() const noexcept
{
  return size_;
}

[[nodiscard]] size_t ClkBuiltInType::get_alignment() const noexcept
{
  return alignment_;
}

[[nodiscard]] bool ClkBuiltInType::is_legacy_wire_compatible(const ClkType& src_type) const
{
  return src_type.get_type_id() == get_type_id() && src_type.get_size() == get_size() &&
         src_type.get_alignment() == get_alignment();
}

void ClkBuiltInType::check_for_unexpected_schema_changes(const ClkType& src_type, std::string_view name)
{
  if (src_type.get_type_id() != get_type_id())
  {
    throw ClkTypeUpgradeError(
      fmt::format("Unexpected type change from {} to {} for {}", src_type.get_type_id(), get_type_id(), name));
  }
}

/// Initializer for a clockwork primitive value
/// @tparam ValueType Value type
template <typename ValueType>
class ClkPrimitiveInitializer : public ClkValueInitializer
{
public:
  /// Constructor
  /// @param[in] value Initial value
  explicit ClkPrimitiveInitializer(ValueType value);

  ~ClkPrimitiveInitializer() override = default;

  ClkPrimitiveInitializer(const ClkPrimitiveInitializer&) = delete;
  ClkPrimitiveInitializer& operator=(const ClkPrimitiveInitializer&) = delete;
  ClkPrimitiveInitializer(ClkPrimitiveInitializer&&) = delete;
  ClkPrimitiveInitializer& operator=(ClkPrimitiveInitializer&&) = delete;

  /// @see ClkValueInitializer::initialize
  void initialize(std::span<std::byte> dest_span) const override;

private:
  /// Initial value
  ValueType value_;
};

template <typename ValueType>
ClkPrimitiveInitializer<ValueType>::ClkPrimitiveInitializer(ValueType value)
  : value_(value)
{
}

template <typename ValueType>
void ClkPrimitiveInitializer<ValueType>::initialize(std::span<std::byte> dest_span) const
{
  std::memcpy(dest_span.data(), &value_, sizeof(ValueType));
}

/// Clockwork primitive type that doesn't need special handling for type conversion in upgrade
/// @tparam ValueType Value type
template <typename ValueType>
class ClkPrimitiveType : public ClkBuiltInType
{
public:
  /// Constructor
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  ClkPrimitiveType(std::string_view fqn, ClkTypeId type_id, size_t type_index, size_t size, size_t alignment);

  ~ClkPrimitiveType() override = default;

  ClkPrimitiveType(const ClkPrimitiveType&) = delete;
  ClkPrimitiveType& operator=(const ClkPrimitiveType&) = delete;
  ClkPrimitiveType(ClkPrimitiveType&&) = delete;
  ClkPrimitiveType& operator=(ClkPrimitiveType&&) = delete;

  /// @see ClkType::make_value_initializer
  [[nodiscard]] std::unique_ptr<ClkValueInitializer>
  make_value_initializer(const metadata::InitialValue& value) const override;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;
};

template <typename ValueType>
ClkPrimitiveType<ValueType>::ClkPrimitiveType(
  std::string_view fqn, ClkTypeId type_id, size_t type_index, size_t size, size_t alignment)
  : ClkBuiltInType(fqn, type_id, type_index, size, alignment)
{
  if (size != sizeof(ValueType))
  {
    throw ClkTypeUpgradeError(fmt::format("Invalid size for {}, got {} expected {}", type_id, size, sizeof(ValueType)));
  }
}

template <typename ValueType>
[[nodiscard]] std::unique_ptr<ClkValueInitializer>
ClkPrimitiveType<ValueType>::make_value_initializer(const metadata::InitialValue& value) const
{
  if constexpr (std::is_same_v<ValueType, bool>)
  {
    return std::make_unique<ClkPrimitiveInitializer<ValueType>>(value.bool_value());
  }
  return ClkType::make_value_initializer(value);
}

template <typename ValueType>
[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader>
ClkPrimitiveType<ValueType>::make_upgrader(const ClkType& src_type)
{
  if (src_type.get_type_id() != get_type_id())
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Cannot upgrade from ({}) to {} ({})", src_type.get_fqn(), src_type.get_type_id(), get_fqn(), get_type_id()));
  }
  auto& upgrader_cache = get_upgrader_cache();
  if (const auto cache_iter = upgrader_cache.find(src_type.get_type_index()); cache_iter != upgrader_cache.end())
  {
    return jewels::memory::make_non_null_from_ref(*cache_iter->second);
  }
  return jewels::memory::make_non_null_from_ref(
    *upgrader_cache.emplace(src_type.get_type_index(), jewels::memory::make_shared<ClkPrimitiveUpgrader<ValueType>>())
       .first->second);
}

template <typename ValueType>
[[nodiscard]] bool ClkPrimitiveType<ValueType>::use_memcpy_for_array_upgrade(const ClkType& src_type)
{
  return src_type.get_type_id() == get_type_id();
}

/// Clockwork integer type (constructor checks the field size and alignment)
/// @tparam ValueType Value type
template <typename ValueType>
class ClkIntegerType : public ClkBuiltInType
{
public:
  /// Constructor
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  ClkIntegerType(std::string_view fqn, ClkTypeId type_id, size_t type_index, size_t size, size_t alignment);

  ~ClkIntegerType() override = default;

  ClkIntegerType(const ClkIntegerType&) = delete;
  ClkIntegerType& operator=(const ClkIntegerType&) = delete;
  ClkIntegerType(ClkIntegerType&&) = delete;
  ClkIntegerType& operator=(ClkIntegerType&&) = delete;

  /// @see ClkType::make_value_initializer
  [[nodiscard]] std::unique_ptr<ClkValueInitializer>
  make_value_initializer(const metadata::InitialValue& value) const override;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;
};

template <typename ValueType>
ClkIntegerType<ValueType>::ClkIntegerType(
  std::string_view fqn, ClkTypeId type_id, size_t type_index, size_t size, size_t alignment)
  : ClkBuiltInType(fqn, type_id, type_index, size, alignment)
{
  if (size != sizeof(ValueType))
  {
    throw ClkTypeUpgradeError(fmt::format("Invalid size for {}, got {} expected {}", type_id, size, sizeof(ValueType)));
  }
  if (alignment != sizeof(ValueType))
  {
    throw ClkTypeUpgradeError(
      fmt::format("Invalid alignment for {}, got {} expected {}", type_id, alignment, sizeof(ValueType)));
  }
}

template <typename ValueType>
[[nodiscard]] std::unique_ptr<ClkValueInitializer>
ClkIntegerType<ValueType>::make_value_initializer(const metadata::InitialValue& value) const
{
  if constexpr (std::is_unsigned_v<ValueType>)
  {
    return std::make_unique<ClkPrimitiveInitializer<ValueType>>(value.unsigned_value());
  }
  return std::make_unique<ClkPrimitiveInitializer<ValueType>>(value.signed_value());
}

template <typename ValueType>
[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader>
ClkIntegerType<ValueType>::make_upgrader(const ClkType& src_type)
{
  auto& upgrader_cache = get_upgrader_cache();
  if (const auto cache_iter = upgrader_cache.find(src_type.get_type_index()); cache_iter != upgrader_cache.end())
  {
    return jewels::memory::make_non_null_from_ref(*cache_iter->second);
  }
  switch (src_type.get_type_id())
  {
  case ClkTypeId::uint8:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(src_type.get_type_index(), jewels::memory::make_shared<ClkIntegerUpgrader<uint8_t, ValueType>>())
         .first->second);
  case ClkTypeId::uint16:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(src_type.get_type_index(), jewels::memory::make_shared<ClkIntegerUpgrader<uint16_t, ValueType>>())
         .first->second);
  case ClkTypeId::uint32:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(src_type.get_type_index(), jewels::memory::make_shared<ClkIntegerUpgrader<uint32_t, ValueType>>())
         .first->second);
  case ClkTypeId::uint64:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(src_type.get_type_index(), jewels::memory::make_shared<ClkIntegerUpgrader<uint64_t, ValueType>>())
         .first->second);
  case ClkTypeId::int8:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(src_type.get_type_index(), jewels::memory::make_shared<ClkIntegerUpgrader<int8_t, ValueType>>())
         .first->second);
  case ClkTypeId::int16:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(src_type.get_type_index(), jewels::memory::make_shared<ClkIntegerUpgrader<int16_t, ValueType>>())
         .first->second);
  case ClkTypeId::int32:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(src_type.get_type_index(), jewels::memory::make_shared<ClkIntegerUpgrader<int32_t, ValueType>>())
         .first->second);
  case ClkTypeId::int64:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(src_type.get_type_index(), jewels::memory::make_shared<ClkIntegerUpgrader<int64_t, ValueType>>())
         .first->second);
  default:
    throw ClkTypeUpgradeError(
      fmt::format(
        "Cannot upgrade from {} ({}) to {} ({})",
        src_type.get_fqn(),
        src_type.get_type_id(),
        get_fqn(),
        get_type_id()));
  }
}

template <typename ValueType>
[[nodiscard]] bool ClkIntegerType<ValueType>::use_memcpy_for_array_upgrade(const ClkType& src_type)
{
  return src_type.get_type_id() == get_type_id();
}

/// Clockwork time type
class ClkTimeType : public ClkIntegerType<int64_t>
{
public:
  /// Constructor
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  ClkTimeType(std::string_view fqn, ClkTypeId type_id, size_t type_index, size_t size, size_t alignment);

  ~ClkTimeType() override = default;

  ClkTimeType(const ClkTimeType&) = delete;
  ClkTimeType& operator=(const ClkTimeType&) = delete;
  ClkTimeType(ClkTimeType&&) = delete;
  ClkTimeType& operator=(ClkTimeType&&) = delete;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;
};

ClkTimeType::ClkTimeType(std::string_view fqn, ClkTypeId type_id, size_t type_index, size_t size, size_t alignment)
  : ClkIntegerType<int64_t>(fqn, type_id, type_index, size, alignment)
{
}

[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> ClkTimeType::make_upgrader(const ClkType& src_type)
{
  if (src_type.get_type_id() == get_type_id())
  {
    return jewels::memory::make_non_null_from_ref(
      *get_upgrader_cache()
         .emplace(src_type.get_type_index(), jewels::memory::make_shared<ClkIntegerUpgrader<int64_t, int64_t>>())
         .first->second);
  }
  return ClkIntegerType<int64_t>::make_upgrader(src_type);
}

[[nodiscard]] bool ClkTimeType::use_memcpy_for_array_upgrade(const ClkType& src_type)
{
  return src_type.get_type_id() == get_type_id() || src_type.get_type_id() == ClkTypeId::int64;
}

/// Clockwork floating point type (constructor checks the field size and alignment)
/// @tparam ValueType Value type
template <typename ValueType>
class ClkFloatingPointType : public ClkBuiltInType
{
public:
  /// Constructor
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  ClkFloatingPointType(std::string_view fqn, ClkTypeId type_id, size_t type_index, size_t size, size_t alignment);

  ~ClkFloatingPointType() override = default;

  ClkFloatingPointType(const ClkFloatingPointType&) = delete;
  ClkFloatingPointType& operator=(const ClkFloatingPointType&) = delete;
  ClkFloatingPointType(ClkFloatingPointType&&) = delete;
  ClkFloatingPointType& operator=(ClkFloatingPointType&&) = delete;

  /// @see ClkType::make_value_initializer
  [[nodiscard]] std::unique_ptr<ClkValueInitializer>
  make_value_initializer(const metadata::InitialValue& value) const override;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;
};

template <typename ValueType>
ClkFloatingPointType<ValueType>::ClkFloatingPointType(
  std::string_view fqn, ClkTypeId type_id, size_t type_index, size_t size, size_t alignment)
  : ClkBuiltInType(fqn, type_id, type_index, size, alignment)
{
  if (size != sizeof(ValueType))
  {
    throw ClkTypeUpgradeError(fmt::format("Invalid size for {}, got {} expected {}", type_id, size, sizeof(ValueType)));
  }
  if (alignment != sizeof(ValueType))
  {
    throw ClkTypeUpgradeError(
      fmt::format("Invalid alignment for {}, got {} expected {}", type_id, alignment, sizeof(ValueType)));
  }
}

template <typename ValueType>
[[nodiscard]] std::unique_ptr<ClkValueInitializer>
ClkFloatingPointType<ValueType>::make_value_initializer(const metadata::InitialValue& value) const
{
  if constexpr (std::is_same_v<ValueType, float>)
  {
    return std::make_unique<ClkPrimitiveInitializer<ValueType>>(string_to_float(value.float_value()));
  }
  return std::make_unique<ClkPrimitiveInitializer<ValueType>>(string_to_double(value.float_value()));
}

template <typename ValueType>
[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader>
ClkFloatingPointType<ValueType>::make_upgrader(const ClkType& src_type)
{
  auto& upgrader_cache = get_upgrader_cache();
  if (const auto cache_iter = upgrader_cache.find(src_type.get_type_index()); cache_iter != upgrader_cache.end())
  {
    return jewels::memory::make_non_null_from_ref(*cache_iter->second);
  }
  switch (src_type.get_type_id())
  {
  case ClkTypeId::float32:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(src_type.get_type_index(), jewels::memory::make_shared<ClkFloatingPointUpgrader<float, ValueType>>())
         .first->second);
  case ClkTypeId::float64:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(src_type.get_type_index(), jewels::memory::make_shared<ClkFloatingPointUpgrader<double, ValueType>>())
         .first->second);
  default:
    throw ClkTypeUpgradeError(
      fmt::format(
        "Cannot upgrade from {} ({}) to {} ({})",
        src_type.get_fqn(),
        src_type.get_type_id(),
        get_fqn(),
        get_type_id()));
  }
}

template <typename ValueType>
[[nodiscard]] bool ClkFloatingPointType<ValueType>::use_memcpy_for_array_upgrade(const ClkType& src_type)
{
  return src_type.get_type_id() == get_type_id();
}

/// Initializer for a clockwork fixed array type
class ClkFixedArrayInitializer : public ClkValueInitializer
{
public:
  /// Constructor
  /// @param[in] array_size Number of array elements
  /// @param[in] element_size Element size in bytes
  /// @param[in] element_initializer Element initializer
  ClkFixedArrayInitializer(
    size_t array_size, size_t element_size, jewels::memory::ObjectPtr<const ClkValueInitializer> element_initializer);

  ~ClkFixedArrayInitializer() override = default;

  ClkFixedArrayInitializer(const ClkFixedArrayInitializer&) = delete;
  ClkFixedArrayInitializer& operator=(const ClkFixedArrayInitializer&) = delete;
  ClkFixedArrayInitializer(ClkFixedArrayInitializer&&) = delete;
  ClkFixedArrayInitializer& operator=(ClkFixedArrayInitializer&&) = delete;

  /// @see ClkValueInitializer::initialize
  void initialize(std::span<std::byte> dest_span) const override;

private:
  /// Array size
  size_t array_size_;

  /// Element size
  size_t element_size_;

  /// Element initializer
  jewels::memory::ObjectPtr<const ClkValueInitializer> element_initializer_;
};

ClkFixedArrayInitializer::ClkFixedArrayInitializer(
  size_t array_size, size_t element_size, jewels::memory::ObjectPtr<const ClkValueInitializer> element_initializer)
  : array_size_(array_size), element_size_(element_size), element_initializer_(element_initializer)
{
}

void ClkFixedArrayInitializer::initialize(std::span<std::byte> dest_span) const
{
  size_t element_offset = 0U;
  for (size_t i = 0U; i < array_size_; ++i, element_offset += element_size_)
  {
    element_initializer_->initialize(dest_span.subspan(element_offset, element_size_));
  }
}

/// Clockwork fixed array type
class ClkFixedArrayType : public ClkBuiltInType
{
public:
  /// Constructor, use from_proto to create an instance
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  /// @param[in] element_type_index Array element type index
  /// @param[in] array_size Number of elements in the array
  /// @param[in] factory Clockwork type factory
  ClkFixedArrayType(
    std::string_view fqn,
    ClkTypeId type_id,
    size_t type_index,
    size_t size,
    size_t alignment,
    size_t element_type_index,
    size_t array_size,
    jewels::memory::ObjectPtr<ClkTypeFactory> factory);

  ~ClkFixedArrayType() override = default;

  ClkFixedArrayType(const ClkFixedArrayType&) = delete;
  ClkFixedArrayType& operator=(const ClkFixedArrayType&) = delete;
  ClkFixedArrayType(ClkFixedArrayType&&) = delete;
  ClkFixedArrayType& operator=(ClkFixedArrayType&&) = delete;

  /// Create an instance from a tachyon built in fixed array type protobuf
  /// @param[in] factory Clockwork type factory
  /// @param[in] builtin_proto Tachyon built in type protobuf
  /// @param[in] type_index Type index
  /// @param[in] maybe_strong_type_fqn Optional fully qualified name of a strong type wrapping this schema
  /// @returns Clockwork type instance
  /// @throws runtime_error on failure.
  [[nodiscard]] static std::unique_ptr<ClkFixedArrayType> from_proto(
    jewels::memory::ObjectPtr<ClkTypeFactory> factory,
    const metadata::BuiltInType& builtin_proto,
    size_t type_index,
    std::optional<std::string_view> maybe_strong_type_fqn);

  /// @return Element type
  [[nodiscard]] const ClkType& get_element_type() const;

  /// @return Element type
  [[nodiscard]] ClkType& get_element_type();

  /// @return Number of elements in the array
  [[nodiscard]] size_t get_array_size() const noexcept;

  /// @see ClkType::make_initializer
  [[nodiscard]] std::optional<jewels::memory::ObjectPtr<const ClkValueInitializer>> make_initializer() override;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;

  /// @see ClkType::is_legacy_wire_compatible
  [[nodiscard]] bool is_legacy_wire_compatible(const ClkType& src_type) const override;

  /// @see ClkType::check_for_unexpected_schema_changes
  void check_for_unexpected_schema_changes(const ClkType& src_type, std::string_view name) override;

private:
  /// Element type index
  size_t element_type_index_;

  /// Array size
  size_t array_size_;

  /// Clockwork type factory
  jewels::memory::ObjectPtr<ClkTypeFactory> factory_;

  /// Cached array initializer
  std::optional<ClkFixedArrayInitializer> maybe_initializer_;

  /// Flag set when the cached array initializer is valid
  bool cached_initializer_valid_{false};
};

/// Clockwork variable array type
class ClkVarArrayType : public ClkBuiltInType
{
public:
  /// Maximum total array size that can be upgraded with memcpy
  /// This is a hueristic to allow upgrading with memcpy when the
  /// size of the entire type is reasonably small.
  static constexpr size_t max_memcpy_size = 128U;

  /// Constructor, use from_proto to create an instance
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  /// @param[in] element_type_index Array element type index
  /// @param[in] array_size Number of elements in the array
  /// @param[in] factory Clockwork type factory
  ClkVarArrayType(
    std::string_view fqn,
    ClkTypeId type_id,
    size_t type_index,
    size_t size,
    size_t alignment,
    size_t element_type_index,
    size_t array_size,
    jewels::memory::ObjectPtr<ClkTypeFactory> factory);

  ~ClkVarArrayType() override = default;

  ClkVarArrayType(const ClkVarArrayType&) = delete;
  ClkVarArrayType& operator=(const ClkVarArrayType&) = delete;
  ClkVarArrayType(ClkVarArrayType&&) = delete;
  ClkVarArrayType& operator=(ClkVarArrayType&&) = delete;

  /// Create an instance from a tachyon built in variable array type protobuf
  /// @param[in] factory Clockwork type factory
  /// @param[in] builtin_proto Tachyon built in type protobuf
  /// @param[in] type_index Type index
  /// @param[in] maybe_strong_type_fqn Optional fully qualified name of a strong type wrapping this schema
  /// @returns Clockwork type instance
  /// @throws runtime_error on failure.
  [[nodiscard]] static std::unique_ptr<ClkVarArrayType> from_proto(
    jewels::memory::ObjectPtr<ClkTypeFactory> factory,
    const metadata::BuiltInType& builtin_proto,
    size_t type_index,
    std::optional<std::string_view> maybe_strong_type_fqn);

  /// @return Element type
  [[nodiscard]] const ClkType& get_element_type() const;

  /// @return Element type
  [[nodiscard]] ClkType& get_element_type();

  /// @return Number of elements in the array
  [[nodiscard]] size_t get_array_size() const noexcept;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;

  /// @see ClkType::is_legacy_wire_compatible
  [[nodiscard]] bool is_legacy_wire_compatible(const ClkType& src_type) const override;

  /// @see ClkType::check_for_unexpected_schema_changes
  void check_for_unexpected_schema_changes(const ClkType& src_type, std::string_view name) override;

private:
  /// Element type index
  size_t element_type_index_;

  /// Array size
  size_t array_size_;

  /// Clockwork type factory
  jewels::memory::ObjectPtr<ClkTypeFactory> factory_;
};

/// Clockwork variable string type
class ClkVarStringType : public ClkBuiltInType
{
public:
  /// Maximum total array size that can be upgraded with memcpy
  /// This is a hueristic to allow upgrading with memcpy when the
  /// size of the entire type is reasonably small.
  static constexpr size_t max_memcpy_size = 128U;

  /// Private constructor, use from_proto to create an instance
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  /// @param[in] string_size Size of the string storage in bytes
  ClkVarStringType(
    std::string_view fqn, ClkTypeId type_id, size_t type_index, size_t size, size_t alignment, size_t string_size);

  ~ClkVarStringType() override = default;

  ClkVarStringType(const ClkVarStringType&) = delete;
  ClkVarStringType& operator=(const ClkVarStringType&) = delete;
  ClkVarStringType(ClkVarStringType&&) = delete;
  ClkVarStringType& operator=(ClkVarStringType&&) = delete;

  /// Create an instance from a tachyon built in variable string type protobuf
  /// @param[in] builtin_proto Tachyon built in type protobuf
  /// @param[in] type_index Type index
  /// @param[in] maybe_strong_type_fqn Optional fully qualified name of a strong type wrapping this schema
  /// @returns Clockwork type instance
  /// @throws runtime_error on failure.
  [[nodiscard]] static std::unique_ptr<ClkVarStringType> from_proto(
    const metadata::BuiltInType& builtin_proto,
    size_t type_index,
    std::optional<std::string_view> maybe_strong_type_fqn);

  /// @return Number of bytes in the string storage
  [[nodiscard]] size_t get_string_size() const noexcept;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;

  /// @see ClkType::is_legacy_wire_compatible
  [[nodiscard]] bool is_legacy_wire_compatible(const ClkType& src_type) const override;

  /// @see ClkType::check_for_unexpected_schema_changes
  void check_for_unexpected_schema_changes(const ClkType& src_type, std::string_view name) override;

private:
  /// String size
  size_t string_size_;
};

/// Clockwork optional type
class ClkOptionalType : public ClkBuiltInType
{
public:
  /// Maximum optional size that can be upgraded with memcpy
  /// This is a hueristic to allow upgrading with memcpy when the
  /// size of the entire type is reasonably small.
  static constexpr size_t max_memcpy_size = 128U;

  /// Constructor, use from_proto to create an instance
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  /// @param[in] element_type_index Element type index
  /// @param[in] factory Clockwork type factory
  ClkOptionalType(
    std::string_view fqn,
    ClkTypeId type_id,
    size_t type_index,
    size_t size,
    size_t alignment,
    size_t element_type_index,
    jewels::memory::ObjectPtr<ClkTypeFactory> factory);

  ~ClkOptionalType() override = default;

  ClkOptionalType(const ClkOptionalType&) = delete;
  ClkOptionalType& operator=(const ClkOptionalType&) = delete;
  ClkOptionalType(ClkOptionalType&&) = delete;
  ClkOptionalType& operator=(ClkOptionalType&&) = delete;

  /// Create an instance from a tachyon built in optional type protobuf
  /// @param[in] factory Clockwork type factory
  /// @param[in] builtin_proto Tachyon built in type protobuf
  /// @param[in] type_index Type index
  /// @param[in] maybe_strong_type_fqn Optional fully qualified name of a strong type wrapping this schema
  /// @returns Clockwork type instance
  /// @throws runtime_error on failure.
  [[nodiscard]] static std::unique_ptr<ClkOptionalType> from_proto(
    jewels::memory::ObjectPtr<ClkTypeFactory> factory,
    const metadata::BuiltInType& builtin_proto,
    size_t type_index,
    std::optional<std::string_view> maybe_strong_type_fqn);

  /// @return Element type
  [[nodiscard]] const ClkType& get_element_type() const;

  /// @return Element type
  [[nodiscard]] ClkType& get_element_type();

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;

  /// @see ClkType::is_legacy_wire_compatible
  [[nodiscard]] bool is_legacy_wire_compatible(const ClkType& src_type) const override;

  /// @see ClkType::check_for_unexpected_schema_changes
  void check_for_unexpected_schema_changes(const ClkType& src_type, std::string_view name) override;

private:
  /// Element type index
  size_t element_type_index_;

  /// Representations for the types in the protobuf schema
  jewels::memory::ObjectPtr<ClkTypeFactory> factory_;
};

// NOLINTNEXTLINE(readability-function-size) TODO(OI-3646)
ClkFixedArrayType::ClkFixedArrayType(
  std::string_view fqn,
  ClkTypeId type_id,
  size_t type_index,
  size_t size,
  size_t alignment,
  size_t element_type_index,
  size_t array_size,
  jewels::memory::ObjectPtr<ClkTypeFactory> factory)
  : ClkBuiltInType(fqn, type_id, type_index, size, alignment),
    element_type_index_(element_type_index),
    array_size_(array_size),
    factory_(factory)
{
}

// NOLINTNEXTLINE(misc-no-recursion) Types are defined recursively
[[nodiscard]] std::unique_ptr<ClkFixedArrayType> ClkFixedArrayType::from_proto(
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  const metadata::BuiltInType& builtin_proto,
  size_t type_index,
  std::optional<std::string_view> maybe_strong_type_fqn)
{
  if (builtin_proto.arguments_size() != 2)
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Invalid protobuf for {} type, got {} arguments, expected 2",
        builtin_proto.fqn(),
        builtin_proto.arguments_size()));
  }
  const auto element_type_index = static_cast<size_t>(builtin_proto.arguments(0).type_id());
  std::ignore = factory->get_clk_type(element_type_index); // Ensure that the element type is populated
  return std::make_unique<ClkFixedArrayType>(
    maybe_strong_type_fqn.value_or(builtin_proto.fqn()),
    ClkTypeId::fixed_array,
    type_index,
    static_cast<size_t>(builtin_proto.size()),
    static_cast<size_t>(builtin_proto.alignment()),
    element_type_index,
    parse_size_parameter(builtin_proto.fqn(), builtin_proto.arguments(1).value()),
    factory);
}

[[nodiscard]] const ClkType& ClkFixedArrayType::get_element_type() const
{
  return *factory_->get_types().at(element_type_index_);
}

[[nodiscard]] ClkType& ClkFixedArrayType::get_element_type()
{
  return *factory_->get_types().at(element_type_index_);
}

[[nodiscard]] size_t ClkFixedArrayType::get_array_size() const noexcept
{
  return array_size_;
}

[[nodiscard]] std::optional<jewels::memory::ObjectPtr<const ClkValueInitializer>> ClkFixedArrayType::make_initializer()
{
  if (!cached_initializer_valid_)
  {
    auto& element_type = get_element_type();
    auto element_initializer = element_type.make_initializer();
    if (element_initializer)
    {
      maybe_initializer_.emplace(array_size_, element_type.get_size(), *element_initializer);
    }
    cached_initializer_valid_ = true;
  }
  if (maybe_initializer_.has_value())
  {
    return jewels::memory::make_non_null_from_ref(*maybe_initializer_);
  }
  return std::nullopt;
}

[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> ClkFixedArrayType::make_upgrader(const ClkType& src_type)
{
  auto& upgrader_cache = get_upgrader_cache();
  if (const auto cache_iter = upgrader_cache.find(src_type.get_type_index()); cache_iter != upgrader_cache.end())
  {
    return jewels::memory::make_non_null_from_ref(*cache_iter->second);
  }
  // Try to treat the source type as an element type
  try
  {
    auto array_upgrader = get_element_type().make_array_upgrader(src_type, array_size_, array_size_);
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_shared<ClkFixedArrayFromElementUpgrader>(std::move(array_upgrader)))
         .first->second);
  }
  catch (const ClkTypeUpgradeError& /*exc*/)
  {
    // Must be upgrading from a container
    if (src_type.get_type_id() == ClkTypeId::fixed_array)
    {
      // Upgrade the array elements
      const auto& src_array_type = dynamic_cast<const ClkFixedArrayType&>(src_type);
      auto array_upgrader =
        get_element_type().make_array_upgrader(src_array_type.get_element_type(), array_size_, array_size_);
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_shared<ClkFixedArrayFromFixedArrayUpgrader>(
               src_array_type.get_array_size(), std::move(array_upgrader)))
           .first->second);
    }
    if (src_type.get_type_id() == ClkTypeId::var_array)
    {
      const auto& src_array_type = dynamic_cast<const ClkVarArrayType&>(src_type);
      auto array_upgrader =
        get_element_type().make_array_upgrader(src_array_type.get_element_type(), array_size_, array_size_);
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_shared<ClkFixedArrayFromVarArrayUpgrader>(
               var_array_size_offset(src_array_type.get_array_size(), src_array_type.get_element_type().get_size()),
               std::move(array_upgrader)))
           .first->second);
    }
    if (
      src_type.get_type_id() == ClkTypeId::var_string &&
      (get_element_type().get_type_id() == ClkTypeId::byte || get_element_type().get_type_id() == ClkTypeId::int8 ||
       get_element_type().get_type_id() != ClkTypeId::uint8))
    {
      const auto& src_string_type = dynamic_cast<const ClkVarStringType&>(src_type);
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_shared<ClkFixedArrayFromVarStringUpgrader>(
               array_size_, var_array_size_offset(src_string_type.get_string_size(), 1U)))
           .first->second);
    }
    if (src_type.get_type_id() == ClkTypeId::optional)
    {
      const auto& src_optional_type = dynamic_cast<const ClkOptionalType&>(src_type);
      auto array_upgrader =
        get_element_type().make_array_upgrader(src_optional_type.get_element_type(), array_size_, array_size_);
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_shared<ClkFixedArrayFromOptionalUpgrader>(
               optional_has_value_offset(src_optional_type.get_element_type().get_size()), std::move(array_upgrader)))
           .first->second);
    }
    throw;
  }
}

[[nodiscard]] bool ClkFixedArrayType::use_memcpy_for_array_upgrade(const ClkType& src_type)
{
  if (src_type.get_type_id() != ClkTypeId::fixed_array)
  {
    return false;
  }
  const auto& src_array_type = dynamic_cast<const ClkFixedArrayType&>(src_type);
  return get_element_type().use_memcpy_for_array_upgrade(src_array_type.get_element_type());
}

[[nodiscard]] bool ClkFixedArrayType::is_legacy_wire_compatible(const ClkType& src_type) const
{
  if (!ClkBuiltInType::is_legacy_wire_compatible(src_type))
  {
    return false;
  }
  const auto& src_array_type = dynamic_cast<const ClkFixedArrayType&>(src_type);
  return src_array_type.array_size_ == array_size_ && src_array_type.element_type_index_ == element_type_index_;
}

void ClkFixedArrayType::check_for_unexpected_schema_changes(const ClkType& src_type, std::string_view name)
{
  ClkBuiltInType::check_for_unexpected_schema_changes(src_type, name);
  const auto& src_array_type = dynamic_cast<const ClkFixedArrayType&>(src_type);
  if (src_array_type.array_size_ != array_size_)
  {
    throw ClkTypeUpgradeError(
      fmt::format("Unexpected array size change from {} to {} for {}", src_array_type.array_size_, array_size_, name));
  }
  get_element_type().check_for_unexpected_schema_changes(src_array_type.get_element_type(), name);
}

// NOLINTNEXTLINE(readability-function-size) TODO(OI-3646)
ClkVarArrayType::ClkVarArrayType(
  std::string_view fqn,
  ClkTypeId type_id,
  size_t type_index,
  size_t size,
  size_t alignment,
  size_t element_type_index,
  size_t array_size,
  jewels::memory::ObjectPtr<ClkTypeFactory> factory)
  : ClkBuiltInType(fqn, type_id, type_index, size, alignment),
    element_type_index_(element_type_index),
    array_size_(array_size),
    factory_(factory)
{
}

// NOLINTNEXTLINE(misc-no-recursion) Types are defined recursively
[[nodiscard]] std::unique_ptr<ClkVarArrayType> ClkVarArrayType::from_proto(
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  const metadata::BuiltInType& builtin_proto,
  size_t type_index,
  std::optional<std::string_view> maybe_strong_type_fqn)
{
  if (builtin_proto.arguments_size() != 2)
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Invalid protobuf for {} type, got {} arguments, expected 2",
        builtin_proto.fqn(),
        builtin_proto.arguments_size()));
  }
  const auto element_type_index = static_cast<size_t>(builtin_proto.arguments(0).type_id());
  std::ignore = factory->get_clk_type(element_type_index); // Ensure that the element type is populated
  return std::make_unique<ClkVarArrayType>(
    maybe_strong_type_fqn.value_or(builtin_proto.fqn()),
    ClkTypeId::var_array,
    type_index,
    static_cast<size_t>(builtin_proto.size()),
    static_cast<size_t>(builtin_proto.alignment()),
    element_type_index,
    parse_size_parameter(builtin_proto.fqn(), builtin_proto.arguments(1).value()),
    factory);
}

[[nodiscard]] const ClkType& ClkVarArrayType::get_element_type() const
{
  return *factory_->get_types().at(element_type_index_);
}

[[nodiscard]] ClkType& ClkVarArrayType::get_element_type()
{
  return *factory_->get_types().at(element_type_index_);
}

[[nodiscard]] size_t ClkVarArrayType::get_array_size() const noexcept
{
  return array_size_;
}

[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> ClkVarArrayType::make_upgrader(const ClkType& src_type)
{
  auto& upgrader_cache = get_upgrader_cache();
  if (const auto cache_iter = upgrader_cache.find(src_type.get_type_index()); cache_iter != upgrader_cache.end())
  {
    return jewels::memory::make_non_null_from_ref(*cache_iter->second);
  }
  // Try to treat the source type as an element type
  try
  {
    auto array_upgrader = get_element_type().make_array_upgrader(src_type, 0U, array_size_);
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_shared<ClkVarArrayFromElementUpgrader>(
             var_array_size_offset(array_size_, get_element_type().get_size()), std::move(array_upgrader)))
         .first->second);
  }
  catch (const ClkTypeUpgradeError& /*exc*/)
  {
    // Must be upgrading from a container
    if (src_type.get_type_id() == ClkTypeId::var_array)
    {
      const auto& src_array_type = dynamic_cast<const ClkVarArrayType&>(src_type);
      auto array_upgrader = get_element_type().make_array_upgrader(src_array_type.get_element_type(), 0U, array_size_);
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_shared<ClkVarArrayFromVarArrayUpgrader>(
               var_array_size_offset(src_array_type.get_array_size(), src_array_type.get_element_type().get_size()),
               var_array_size_offset(array_size_, get_element_type().get_size()),
               std::move(array_upgrader)))
           .first->second);
    }
    if (src_type.get_type_id() == ClkTypeId::fixed_array)
    {
      const auto& src_array_type = dynamic_cast<const ClkFixedArrayType&>(src_type);
      auto array_upgrader = get_element_type().make_array_upgrader(src_array_type.get_element_type(), 0U, array_size_);
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_shared<ClkVarArrayFromFixedArrayUpgrader>(
               src_array_type.get_array_size(),
               var_array_size_offset(array_size_, get_element_type().get_size()),
               std::move(array_upgrader)))
           .first->second);
    }
    if (
      src_type.get_type_id() == ClkTypeId::var_string &&
      (get_element_type().get_type_id() == ClkTypeId::byte || get_element_type().get_type_id() == ClkTypeId::int8 ||
       get_element_type().get_type_id() != ClkTypeId::uint8))
    {
      const auto& src_string_type = dynamic_cast<const ClkVarStringType&>(src_type);
      return jewels::memory::make_non_null_from_ref(*upgrader_cache
                                                       .emplace(
                                                         src_type.get_type_index(),
                                                         jewels::memory::make_shared<ClkVarArrayFromVarStringUpgrader>(
                                                           array_size_,
                                                           var_array_size_offset(src_string_type.get_string_size(), 1U),
                                                           var_array_size_offset(array_size_, 1U)))
                                                       .first->second);
    }
    if (src_type.get_type_id() == ClkTypeId::optional)
    {
      const auto& src_optional_type = dynamic_cast<const ClkOptionalType&>(src_type);
      auto array_upgrader =
        get_element_type().make_array_upgrader(src_optional_type.get_element_type(), 0U, array_size_);
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_shared<ClkVarArrayFromOptionalUpgrader>(
               optional_has_value_offset(src_optional_type.get_element_type().get_size()),
               var_array_size_offset(array_size_, get_element_type().get_size()),
               std::move(array_upgrader)))
           .first->second);
    }
    throw;
  }
}

[[nodiscard]] bool ClkVarArrayType::use_memcpy_for_array_upgrade(const ClkType& src_type)
{
  if (src_type.get_type_id() != ClkTypeId::var_array)
  {
    return false;
  }
  const auto& src_array_type = dynamic_cast<const ClkVarArrayType&>(src_type);
  return src_array_type.get_size() == get_size() && get_size() <= max_memcpy_size &&
         get_element_type().use_memcpy_for_array_upgrade(src_array_type.get_element_type());
}

[[nodiscard]] bool ClkVarArrayType::is_legacy_wire_compatible(const ClkType& src_type) const
{
  if (!ClkBuiltInType::is_legacy_wire_compatible(src_type))
  {
    return false;
  }
  const auto& src_array_type = dynamic_cast<const ClkVarArrayType&>(src_type);
  return src_array_type.array_size_ == array_size_ && src_array_type.element_type_index_ == element_type_index_;
}

void ClkVarArrayType::check_for_unexpected_schema_changes(const ClkType& src_type, std::string_view name)
{
  ClkBuiltInType::check_for_unexpected_schema_changes(src_type, name);
  const auto& src_array_type = dynamic_cast<const ClkVarArrayType&>(src_type);
  if (src_array_type.array_size_ != array_size_)
  {
    throw ClkTypeUpgradeError(
      fmt::format("Unexpected array size change from {} to {} for {}", src_array_type.array_size_, array_size_, name));
  }
  get_element_type().check_for_unexpected_schema_changes(src_array_type.get_element_type(), name);
}

ClkVarStringType::ClkVarStringType(
  std::string_view fqn, ClkTypeId type_id, size_t type_index, size_t size, size_t alignment, size_t string_size)
  : ClkBuiltInType(fqn, type_id, type_index, size, alignment), string_size_(string_size)
{
}

[[nodiscard]] std::unique_ptr<ClkVarStringType> ClkVarStringType::from_proto(
  const metadata::BuiltInType& builtin_proto, size_t type_index, std::optional<std::string_view> maybe_strong_type_fqn)
{
  if (builtin_proto.arguments_size() != 1)
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Invalid protobuf for {} type, got {} arguments, expected 2",
        builtin_proto.fqn(),
        builtin_proto.arguments_size()));
  }
  return std::make_unique<ClkVarStringType>(
    maybe_strong_type_fqn.value_or(builtin_proto.fqn()),
    ClkTypeId::var_string,
    type_index,
    static_cast<size_t>(builtin_proto.size()),
    static_cast<size_t>(builtin_proto.alignment()),
    parse_size_parameter(builtin_proto.fqn(), builtin_proto.arguments(0).value()));
}

[[nodiscard]] size_t ClkVarStringType::get_string_size() const noexcept
{
  return string_size_;
}

[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> ClkVarStringType::make_upgrader(const ClkType& src_type)
{
  auto& upgrader_cache = get_upgrader_cache();
  if (const auto cache_iter = upgrader_cache.find(src_type.get_type_index()); cache_iter != upgrader_cache.end())
  {
    return jewels::memory::make_non_null_from_ref(*cache_iter->second);
  }
  if (src_type.get_type_id() == ClkTypeId::var_string)
  {
    const auto& src_string_type = dynamic_cast<const ClkVarStringType&>(src_type);
    return jewels::memory::make_non_null_from_ref(*upgrader_cache
                                                     .emplace(
                                                       src_type.get_type_index(),
                                                       jewels::memory::make_shared<ClkVarStringFromVarStringUpgrader>(
                                                         get_string_size() - 1U,
                                                         var_array_size_offset(src_string_type.get_string_size(), 1U),
                                                         var_array_size_offset(get_string_size(), 1U)))
                                                     .first->second);
  }
  if (src_type.get_type_id() == ClkTypeId::fixed_array)
  {
    const auto& src_array_type = dynamic_cast<const ClkFixedArrayType&>(src_type);
    if (
      src_array_type.get_element_type().get_type_id() == ClkTypeId::byte ||
      src_array_type.get_element_type().get_type_id() == ClkTypeId::int8 ||
      src_array_type.get_element_type().get_type_id() == ClkTypeId::uint8)
    {
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_shared<ClkVarStringFromFixedArrayUpgrader>(
               get_string_size() - 1U, src_array_type.get_array_size(), var_array_size_offset(get_string_size(), 1U)))
           .first->second);
    }
  }
  if (src_type.get_type_id() == ClkTypeId::var_array)
  {
    const auto& src_array_type = dynamic_cast<const ClkVarArrayType&>(src_type);
    if (
      src_array_type.get_element_type().get_type_id() == ClkTypeId::byte ||
      src_array_type.get_element_type().get_type_id() == ClkTypeId::int8 ||
      src_array_type.get_element_type().get_type_id() == ClkTypeId::uint8)
    {
      return jewels::memory::make_non_null_from_ref(*upgrader_cache
                                                       .emplace(
                                                         src_type.get_type_index(),
                                                         jewels::memory::make_shared<ClkVarStringFromVarStringUpgrader>(
                                                           get_string_size() - 1U,
                                                           var_array_size_offset(src_array_type.get_array_size(), 1U),
                                                           var_array_size_offset(get_string_size(), 1U)))
                                                       .first->second);
    }
  }
  if (src_type.get_type_id() == ClkTypeId::optional)
  {
    const auto& src_optional_type = dynamic_cast<const ClkOptionalType&>(src_type);
    if (
      src_optional_type.get_element_type().get_type_id() == ClkTypeId::byte ||
      src_optional_type.get_element_type().get_type_id() == ClkTypeId::int8 ||
      src_optional_type.get_element_type().get_type_id() == ClkTypeId::uint8)
    {
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_shared<ClkVarStringFromOptionalUpgrader>(
               optional_has_value_offset(1U), var_array_size_offset(get_string_size(), 1U)))
           .first->second);
    }
  }
  if (
    src_type.get_type_id() == ClkTypeId::byte || src_type.get_type_id() == ClkTypeId::int8 ||
    src_type.get_type_id() == ClkTypeId::uint8)
  {
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_shared<ClkVarStringFromFixedArrayUpgrader>(
             get_string_size() - 1U, 1U, var_array_size_offset(get_string_size(), 1U)))
         .first->second);
  }
  throw ClkTypeUpgradeError(
    fmt::format(
      "Cannot upgrade from {} ({}) to {} ({})", src_type.get_fqn(), src_type.get_type_id(), get_fqn(), get_type_id()));
}

[[nodiscard]] bool ClkVarStringType::use_memcpy_for_array_upgrade(const ClkType& src_type)
{
  if (src_type.get_type_id() != ClkTypeId::var_string)
  {
    return false;
  }
  const auto& src_string_type = dynamic_cast<const ClkVarStringType&>(src_type);
  return src_string_type.get_size() == get_size() && get_size() <= max_memcpy_size;
}

[[nodiscard]] bool ClkVarStringType::is_legacy_wire_compatible(const ClkType& src_type) const
{
  if (!ClkBuiltInType::is_legacy_wire_compatible(src_type))
  {
    return false;
  }
  const auto& src_string_type = dynamic_cast<const ClkVarStringType&>(src_type);
  return src_string_type.string_size_ == string_size_;
}

void ClkVarStringType::check_for_unexpected_schema_changes(const ClkType& src_type, std::string_view name)
{
  ClkBuiltInType::check_for_unexpected_schema_changes(src_type, name);
  const auto& src_string_type = dynamic_cast<const ClkVarStringType&>(src_type);
  if (src_string_type.string_size_ != string_size_)
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Unexpected string size change from {} to {} for {}", src_string_type.string_size_, string_size_, name));
  }
}

ClkOptionalType::ClkOptionalType(
  std::string_view fqn,
  ClkTypeId type_id,
  size_t type_index,
  size_t size,
  size_t alignment,
  size_t element_type_index,
  jewels::memory::ObjectPtr<ClkTypeFactory> factory)
  : ClkBuiltInType(fqn, type_id, type_index, size, alignment),
    element_type_index_(element_type_index),
    factory_(factory)
{
}

// NOLINTNEXTLINE(misc-no-recursion) Types are defined recursively
[[nodiscard]] std::unique_ptr<ClkOptionalType> ClkOptionalType::from_proto(
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  const metadata::BuiltInType& builtin_proto,
  size_t type_index,
  std::optional<std::string_view> maybe_strong_type_fqn)
{
  if (builtin_proto.arguments_size() != 1)
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Invalid protobuf for {} type, got {} arguments, expected 1",
        builtin_proto.fqn(),
        builtin_proto.arguments_size()));
  }
  const auto element_type_index = static_cast<size_t>(builtin_proto.arguments(0).type_id());
  std::ignore = factory->get_clk_type(element_type_index); // Ensure that the element type is populated
  return std::make_unique<ClkOptionalType>(
    maybe_strong_type_fqn.value_or(builtin_proto.fqn()),
    ClkTypeId::optional,
    type_index,
    static_cast<size_t>(builtin_proto.size()),
    static_cast<size_t>(builtin_proto.alignment()),
    element_type_index,
    factory);
}

[[nodiscard]] const ClkType& ClkOptionalType::get_element_type() const
{
  return *factory_->get_types().at(element_type_index_);
}

[[nodiscard]] ClkType& ClkOptionalType::get_element_type()
{
  return *factory_->get_types().at(element_type_index_);
}

[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> ClkOptionalType::make_upgrader(const ClkType& src_type)
{
  auto& upgrader_cache = get_upgrader_cache();
  if (const auto cache_iter = upgrader_cache.find(src_type.get_type_index()); cache_iter != upgrader_cache.end())
  {
    return jewels::memory::make_non_null_from_ref(*cache_iter->second);
  }
  // Try to upgrade from an element type
  try
  {
    const auto element_upgrader = get_element_type().make_upgrader(src_type);
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_shared<ClkOptionalFromFixedArrayUpgrader>(
             1U, optional_has_value_offset(get_element_type().get_size()), element_upgrader))
         .first->second);
  }
  catch (const ClkTypeUpgradeError& /*exc*/)
  {
    if (src_type.get_type_id() == ClkTypeId::fixed_array)
    {
      const auto& src_array_type = dynamic_cast<const ClkFixedArrayType&>(src_type);
      const auto element_upgrader = get_element_type().make_upgrader(src_array_type.get_element_type());
      return jewels::memory::make_non_null_from_ref(*upgrader_cache
                                                       .emplace(
                                                         src_type.get_type_index(),
                                                         jewels::memory::make_shared<ClkOptionalFromFixedArrayUpgrader>(
                                                           src_array_type.get_array_size(),
                                                           optional_has_value_offset(get_element_type().get_size()),
                                                           element_upgrader))
                                                       .first->second);
    }
    if (src_type.get_type_id() == ClkTypeId::var_array)
    {
      const auto& src_array_type = dynamic_cast<const ClkVarArrayType&>(src_type);
      const auto element_upgrader = get_element_type().make_upgrader(src_array_type.get_element_type());
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_shared<ClkOptionalFromVarArrayUpgrader>(
               var_array_size_offset(src_array_type.get_array_size(), src_array_type.get_element_type().get_size()),
               optional_has_value_offset(get_element_type().get_size()),
               element_upgrader))
           .first->second);
    }
    if (
      src_type.get_type_id() == ClkTypeId::var_string &&
      (get_element_type().get_type_id() == ClkTypeId::int8 || get_element_type().get_type_id() == ClkTypeId::uint8 ||
       get_element_type().get_type_id() == ClkTypeId::byte))
    {
      const auto& src_string_type = dynamic_cast<const ClkVarStringType&>(src_type);
      return jewels::memory::make_non_null_from_ref(*upgrader_cache
                                                       .emplace(
                                                         src_type.get_type_index(),
                                                         jewels::memory::make_shared<ClkOptionalFromVarStringUpgrader>(
                                                           var_array_size_offset(src_string_type.get_string_size(), 1U),
                                                           optional_has_value_offset(get_element_type().get_size())))
                                                       .first->second);
    }
    if (src_type.get_type_id() == ClkTypeId::optional)
    {
      const auto& src_optional_type = dynamic_cast<const ClkOptionalType&>(src_type);
      const auto element_upgrader = get_element_type().make_upgrader(src_optional_type.get_element_type());
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_shared<ClkOptionalFromOptionalUpgrader>(
               optional_has_value_offset(src_optional_type.get_element_type().get_size()),
               optional_has_value_offset(get_element_type().get_size()),
               element_upgrader))
           .first->second);
    }
    throw;
  }
}

[[nodiscard]] bool ClkOptionalType::use_memcpy_for_array_upgrade(const ClkType& src_type)
{
  if (src_type.get_type_id() != ClkTypeId::optional || get_size() > max_memcpy_size)
  {
    return false;
  }
  const auto& src_optional_type = dynamic_cast<const ClkOptionalType&>(src_type);
  return get_element_type().use_memcpy_for_array_upgrade(src_optional_type.get_element_type());
}

[[nodiscard]] bool ClkOptionalType::is_legacy_wire_compatible(const ClkType& src_type) const
{
  if (!ClkBuiltInType::is_legacy_wire_compatible(src_type))
  {
    return false;
  }
  const auto& src_optional_type = dynamic_cast<const ClkOptionalType&>(src_type);
  return src_optional_type.element_type_index_ == element_type_index_;
}

void ClkOptionalType::check_for_unexpected_schema_changes(const ClkType& src_type, std::string_view name)
{
  ClkBuiltInType::check_for_unexpected_schema_changes(src_type, name);
  const auto& src_optional_type = dynamic_cast<const ClkOptionalType&>(src_type);
  get_element_type().check_for_unexpected_schema_changes(src_optional_type.get_element_type(), name);
}

} // namespace

[[nodiscard]] size_t var_array_size_offset(size_t array_size, size_t element_size)
{
  const auto data_size = array_size * element_size;
  const auto remainder = data_size % sizeof(uint64_t);
  const auto padding = remainder == 0UL ? 0UL : sizeof(uint64_t) - remainder;
  return data_size + padding;
}

[[nodiscard]] size_t optional_has_value_offset(size_t value_size)
{
  return value_size;
}

// NOLINTNEXTLINE(misc-no-recursion) Types are defined recursively
[[nodiscard]] std::unique_ptr<ClkType> ClkBuiltInTypeFactoryPlugin::make_clk_type(
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  const metadata::TypeDesc& type_proto,
  size_t type_index,
  std::optional<std::string_view> maybe_strong_type_fqn) const
{
  if (type_proto.has_tag())
  {
    return std::make_unique<ClkTagType>(
      maybe_strong_type_fqn.value_or(type_proto.tag().fqn()), ClkTypeId::tag, type_index);
  }
  if (!type_proto.has_built_in())
  {
    return nullptr;
  }
  const auto& built_in_proto = type_proto.built_in();
  const auto type_id = built_in_fqn_to_type_id(built_in_proto.fqn());
  switch (type_id)
  {
  case ClkTypeId::boolean:
    return std::make_unique<ClkPrimitiveType<bool>>(
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::boolean,
      type_index,
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::uint8:
    return std::make_unique<ClkIntegerType<uint8_t>>(
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::uint8,
      type_index,
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::uint16:
    return std::make_unique<ClkIntegerType<uint16_t>>(
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::uint16,
      type_index,
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::uint32:
    return std::make_unique<ClkIntegerType<uint32_t>>(
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::uint32,
      type_index,
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::uint64:
    return std::make_unique<ClkIntegerType<uint64_t>>(
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::uint64,
      type_index,
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::int8:
    return std::make_unique<ClkIntegerType<int8_t>>(
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::int8,
      type_index,
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::int16:
    return std::make_unique<ClkIntegerType<int16_t>>(
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::int16,
      type_index,
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::int32:
    return std::make_unique<ClkIntegerType<int32_t>>(
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::int32,
      type_index,
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::int64:
    return std::make_unique<ClkIntegerType<int64_t>>(
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::int64,
      type_index,
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::float32:
    return std::make_unique<ClkFloatingPointType<float>>(
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::float32,
      type_index,
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::float64:
    return std::make_unique<ClkFloatingPointType<double>>(
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::float64,
      type_index,
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::synctime:
    return std::make_unique<ClkTimeType>(
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::synctime,
      type_index,
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::duration:
    return std::make_unique<ClkTimeType>(
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::duration,
      type_index,
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::byte:
    return std::make_unique<ClkPrimitiveType<std::byte>>(
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::byte,
      type_index,
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::uuid:
    return std::make_unique<ClkPrimitiveType<SchemaUuid>>(
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::uuid,
      type_index,
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::fixed_array:
    return ClkFixedArrayType::from_proto(factory, built_in_proto, type_index, maybe_strong_type_fqn);
  case ClkTypeId::var_array:
    return ClkVarArrayType::from_proto(factory, built_in_proto, type_index, maybe_strong_type_fqn);
  case ClkTypeId::var_string:
    return ClkVarStringType::from_proto(built_in_proto, type_index, maybe_strong_type_fqn);
  case ClkTypeId::optional:
    return ClkOptionalType::from_proto(factory, built_in_proto, type_index, maybe_strong_type_fqn);
  default:
    throw ClkTypeUpgradeError(fmt::format("Unhandled built in type id: {}", type_id));
  }
}

} // namespace clockwork::serialization
