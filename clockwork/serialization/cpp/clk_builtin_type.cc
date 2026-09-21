// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/serialization/cpp/clk_builtin_type.hh"

#include "clockwork/serialization/cpp/clk_schema_type_lib.hh"
#include "clockwork/serialization/cpp/clk_type.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/bits.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"

#include <fmt/format.h>
#include <google/protobuf/repeated_ptr_field.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <memory_resource>
#include <numeric>
#include <optional>
#include <ranges>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace clockwork::serialization
{

using jewels::failure;
using jewels::InOut;
using jewels::ok;
using jewels::success;

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
  {".Optional", ClkTypeId::optional},  {".FixedSoa", ClkTypeId::fixed_soa},
  {".VarSoa", ClkTypeId::var_soa},     {".Tensor", ClkTypeId::tensor},
  {".Bitset", ClkTypeId::bitset},
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

/// Upgrade a numeric value from SrcValueType to DestValueType
/// @tparam SrcValueType Source value type
/// @tparam DestValueType Destination value type
/// @param[in] src_span Source value span
/// @param[in] dest_span Destination value span
template <typename SrcValueType, typename DestValueType>
void upgrade_numeric(std::span<const std::byte> src_span, std::span<std::byte> dest_span)
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

/// Clockwork upgrader for numeric types
/// @tparam SrcValueType Source value type
/// @tparam DestValueType Destination value type
template <typename SrcValueType, typename DestValueType>
class ClkNumericUpgrader : public ClkTypeUpgrader
{
public:
  ClkNumericUpgrader() noexcept = default;

  ~ClkNumericUpgrader() noexcept override = default;

  ClkNumericUpgrader(const ClkNumericUpgrader&) = delete;
  ClkNumericUpgrader& operator=(const ClkNumericUpgrader&) = delete;
  ClkNumericUpgrader(ClkNumericUpgrader&&) = delete;
  ClkNumericUpgrader& operator=(ClkNumericUpgrader&&) = delete;

  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;
};

template <typename SrcValueType, typename DestValueType>
void ClkNumericUpgrader<SrcValueType, DestValueType>::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  upgrade_numeric<SrcValueType, DestValueType>(src_span, dest_span);
}

/// Clockwork upgrader for fixed array types
class ClkFixedArrayFromFixedArrayUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] src_array_size Source array size
  /// @param[in] array_upgrader Array upgrader
  ClkFixedArrayFromFixedArrayUpgrader(size_t src_array_size, std::shared_ptr<ClkArrayUpgrader> array_upgrader);

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
  std::shared_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkFixedArrayFromFixedArrayUpgrader::ClkFixedArrayFromFixedArrayUpgrader(
  size_t src_array_size, std::shared_ptr<ClkArrayUpgrader> array_upgrader)
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
  ClkFixedArrayFromVarArrayUpgrader(size_t src_size_offset, std::shared_ptr<ClkArrayUpgrader> array_upgrader);

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
  std::shared_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkFixedArrayFromVarArrayUpgrader::ClkFixedArrayFromVarArrayUpgrader(
  size_t src_size_offset, std::shared_ptr<ClkArrayUpgrader> array_upgrader)
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
  ClkFixedArrayFromOptionalUpgrader(size_t src_has_value_offset, std::shared_ptr<ClkArrayUpgrader> array_upgrader);

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
  std::shared_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkFixedArrayFromOptionalUpgrader::ClkFixedArrayFromOptionalUpgrader(
  size_t src_has_value_offset, std::shared_ptr<ClkArrayUpgrader> array_upgrader)
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
  explicit ClkFixedArrayFromElementUpgrader(std::shared_ptr<ClkArrayUpgrader> array_upgrader);

  ~ClkFixedArrayFromElementUpgrader() noexcept override = default;

  ClkFixedArrayFromElementUpgrader(const ClkFixedArrayFromElementUpgrader&) = delete;
  ClkFixedArrayFromElementUpgrader& operator=(const ClkFixedArrayFromElementUpgrader&) = delete;
  ClkFixedArrayFromElementUpgrader(ClkFixedArrayFromElementUpgrader&&) = delete;
  ClkFixedArrayFromElementUpgrader& operator=(ClkFixedArrayFromElementUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Array upgrader
  std::shared_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkFixedArrayFromElementUpgrader::ClkFixedArrayFromElementUpgrader(std::shared_ptr<ClkArrayUpgrader> array_upgrader)
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
    size_t src_size_offset, size_t dest_size_offset, std::shared_ptr<ClkArrayUpgrader> array_upgrader);

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
  std::shared_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkVarArrayFromVarArrayUpgrader::ClkVarArrayFromVarArrayUpgrader(
  size_t src_size_offset, size_t dest_size_offset, std::shared_ptr<ClkArrayUpgrader> array_upgrader)
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
    size_t src_array_size, size_t dest_size_offset, std::shared_ptr<ClkArrayUpgrader> array_upgrader);

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
  std::shared_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkVarArrayFromFixedArrayUpgrader::ClkVarArrayFromFixedArrayUpgrader(
  size_t src_array_size, size_t dest_size_offset, std::shared_ptr<ClkArrayUpgrader> array_upgrader)
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
    size_t src_has_value_offset, size_t dest_size_offset, std::shared_ptr<ClkArrayUpgrader> array_upgrader);

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
  std::shared_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkVarArrayFromOptionalUpgrader::ClkVarArrayFromOptionalUpgrader(
  size_t src_has_value_offset, size_t dest_size_offset, std::shared_ptr<ClkArrayUpgrader> array_upgrader)
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
  ClkVarArrayFromElementUpgrader(size_t dest_size_offset, std::shared_ptr<ClkArrayUpgrader> array_upgrader);

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
  std::shared_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkVarArrayFromElementUpgrader::ClkVarArrayFromElementUpgrader(
  size_t dest_size_offset, std::shared_ptr<ClkArrayUpgrader> array_upgrader)
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

/// Upgrade a variable string type from a UUID type
class ClkVarStringFromUuidUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] dest_size_offset Destination string size offset
  explicit ClkVarStringFromUuidUpgrader(size_t dest_size_offset);

  ~ClkVarStringFromUuidUpgrader() noexcept override = default;

  ClkVarStringFromUuidUpgrader(const ClkVarStringFromUuidUpgrader&) = delete;
  ClkVarStringFromUuidUpgrader& operator=(const ClkVarStringFromUuidUpgrader&) = delete;
  ClkVarStringFromUuidUpgrader(ClkVarStringFromUuidUpgrader&&) = delete;
  ClkVarStringFromUuidUpgrader& operator=(ClkVarStringFromUuidUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Destination size offset
  size_t dest_size_offset_;
};

ClkVarStringFromUuidUpgrader::ClkVarStringFromUuidUpgrader(size_t dest_size_offset)
  : dest_size_offset_(dest_size_offset)
{
}

void ClkVarStringFromUuidUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  SchemaUuid uuid{};
  std::memcpy(uuid.uuid.data(), src_span.data(), sizeof(SchemaUuid));
  const auto uuid_str = uuid.to_string();
  const auto uuid_str_size = static_cast<uint64_t>(uuid_str.size());
  std::memcpy(dest_span.data(), uuid_str.data(), uuid_str_size);
  jewels::memory::write_as_bytes(
    uuid_str_size, std::span<std::byte, sizeof(uint64_t)>{dest_span.subspan(dest_size_offset_, sizeof(uint64_t))});
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

/// Field mapping information for SoA-to-SoA upgrade
struct SoaFieldMapping
{
  /// Source field layout
  FieldLayoutInfo src_field;

  /// Destination field layout
  FieldLayoutInfo dest_field;

  /// Type upgrader for converting field values
  jewels::memory::ObjectPtr<const ClkTypeUpgrader> field_upgrader;
};

/// Parameters for ClkSoaToSoaUpgrader constructor
struct ClkSoaToSoaUpgraderParams
{
  jewels::memory::MemoryResource memory_resource;
  std::string_view src_type_fqn;
  std::string_view dest_type_fqn;
  size_t src_size_field_offset;
  size_t src_size_field_length;
  size_t dest_size_field_offset;
  size_t dest_size_field_length;
  size_t array_size;
  std::pmr::vector<SoaFieldMapping> field_mappings;
  std::pmr::vector<std::pair<FieldLayoutInfo, jewels::memory::ObjectPtr<const ClkValueInitializer>>>
    new_field_initializers;
};

/// Clockwork SoA-to-SoA upgrader
/// Handles conversion between different SoA schemas with field-level transformations
class ClkSoaToSoaUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor for SoA-to-SoA upgrader
  explicit ClkSoaToSoaUpgrader(ClkSoaToSoaUpgraderParams params);

  ~ClkSoaToSoaUpgrader() override = default;

  ClkSoaToSoaUpgrader(const ClkSoaToSoaUpgrader&) = delete;
  ClkSoaToSoaUpgrader& operator=(const ClkSoaToSoaUpgrader&) = delete;
  ClkSoaToSoaUpgrader(ClkSoaToSoaUpgrader&&) = delete;
  ClkSoaToSoaUpgrader& operator=(ClkSoaToSoaUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source type FQN for error messages
  std::pmr::string src_type_fqn_;

  /// Destination type FQN for error messages
  std::pmr::string dest_type_fqn_;

  /// Source size field offset (for VarSoa)
  size_t src_size_field_offset_;

  /// Source size field length (for VarSoa)
  size_t src_size_field_length_;

  /// Destination size field offset (for VarSoa)
  size_t dest_size_field_offset_;

  /// Destination size field length (for VarSoa)
  size_t dest_size_field_length_;

  /// Array size for FixedSoa (ignored for VarSoa)
  size_t array_size_;

  /// Field mapping information
  std::pmr::vector<SoaFieldMapping> field_mappings_;

  /// Initializers for newly added fields
  std::pmr::vector<std::pair<FieldLayoutInfo, jewels::memory::ObjectPtr<const ClkValueInitializer>>>
    new_field_initializers_;
};

ClkSoaToSoaUpgrader::ClkSoaToSoaUpgrader(ClkSoaToSoaUpgraderParams params)
  : src_type_fqn_(params.src_type_fqn, params.memory_resource),
    dest_type_fqn_(params.dest_type_fqn, params.memory_resource),
    src_size_field_offset_(params.src_size_field_offset),
    src_size_field_length_(params.src_size_field_length),
    dest_size_field_offset_(params.dest_size_field_offset),
    dest_size_field_length_(params.dest_size_field_length),
    array_size_(params.array_size),
    field_mappings_(std::move(params.field_mappings)),
    new_field_initializers_(std::move(params.new_field_initializers))
{
}

void ClkSoaToSoaUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const bool src_is_var_soa = (src_size_field_offset_ != 0U);
  const bool dest_is_var_soa = (dest_size_field_offset_ != 0U);

  size_t array_length = array_size_;

  if (src_is_var_soa)
  {
    array_length = load_soa_size(src_span, src_size_field_offset_, src_size_field_length_);
  }

  if (dest_is_var_soa)
  {
    store_soa_size(dest_span, dest_size_field_offset_, dest_size_field_length_, array_length);
  }

  for (const auto& mapping : field_mappings_)
  {
    const auto& src_field = mapping.src_field;
    const auto& dest_field = mapping.dest_field;
    const auto& field_upgrader = mapping.field_upgrader;

    auto src_field_array = get_soa_field_array(src_span, src_field, array_length);
    auto dest_field_array = get_soa_field_array(dest_span, dest_field, array_length);

    try
    {
      for (size_t i = 0U; i < array_length; ++i)
      {
        auto src_element = src_field_array.subspan(i * src_field.size, src_field.size);
        auto dest_element = dest_field_array.subspan(i * dest_field.size, dest_field.size);

        field_upgrader->upgrade(src_element, dest_element);
      }
    }
    catch (const ClkTypeUpgradeError& exc)
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Failed to upgrade field {} from {} to {}: {}",
          src_field.field_num,
          src_type_fqn_,
          dest_type_fqn_,
          exc.what()));
    }
  }

  for (const auto& [new_field, initializer] : new_field_initializers_)
  {
    auto dest_field_array = get_soa_field_array(dest_span, new_field, array_length);

    try
    {
      for (size_t i = 0U; i < array_length; ++i)
      {
        initializer->initialize(dest_field_array.subspan(i * new_field.size, new_field.size));
      }
    }
    catch (const ClkTypeUpgradeError& exc)
    {
      throw ClkTypeUpgradeError(
        fmt::format("Failed to initialize new field {} in {}: {}", new_field.field_num, dest_type_fqn_, exc.what()));
    }
  }
}

/// Field mapping for AoS-to-SoA upgrade (maps schema field offset to SoA field array layout)
struct AosToSoaFieldMapping
{
  /// Field number
  int32_t field_num;

  /// Source field offset within a single AoS element
  size_t src_field_offset;

  /// Source field size
  size_t src_field_size;

  /// Destination field layout in SoA
  FieldLayoutInfo dest_field;

  /// Type upgrader for converting field values
  jewels::memory::ObjectPtr<const ClkTypeUpgrader> field_upgrader;
};

/// Parameters for ClkArrayToSoaUpgrader constructor
struct ClkArrayToSoaUpgraderParams
{
  jewels::memory::MemoryResource memory_resource;
  std::string_view src_type_fqn;
  std::string_view dest_type_fqn;
  size_t src_element_size;
  size_t src_size_field_offset;
  size_t src_fixed_array_size;
  bool src_is_optional;
  size_t dest_size_field_offset;
  size_t dest_size_field_length;
  size_t dest_array_size;
  std::pmr::vector<AosToSoaFieldMapping> field_mappings;
  std::pmr::vector<std::pair<FieldLayoutInfo, jewels::memory::ObjectPtr<const ClkValueInitializer>>>
    new_field_initializers;
};

/// Clockwork AoS-to-SoA upgrader (transpose from Array-of-Structures to Struct-of-Arrays)
/// Handles conversion from VarArray/FixedArray/Optional of schema to VarSoa/FixedSoa
class ClkArrayToSoaUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor for AoS-to-SoA upgrader
  explicit ClkArrayToSoaUpgrader(ClkArrayToSoaUpgraderParams params);

  ~ClkArrayToSoaUpgrader() override = default;

  ClkArrayToSoaUpgrader(const ClkArrayToSoaUpgrader&) = delete;
  ClkArrayToSoaUpgrader& operator=(const ClkArrayToSoaUpgrader&) = delete;
  ClkArrayToSoaUpgrader(ClkArrayToSoaUpgrader&&) = delete;
  ClkArrayToSoaUpgrader& operator=(ClkArrayToSoaUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source type FQN for error messages
  std::pmr::string src_type_fqn_;

  /// Destination type FQN for error messages
  std::pmr::string dest_type_fqn_;

  /// Source element size (for AoS)
  size_t src_element_size_;

  /// Source size field offset (for VarArray, has_value offset for Optional, 0 for FixedArray)
  size_t src_size_field_offset_;

  /// Source fixed array size (for FixedArray, 0 for VarArray/Optional)
  size_t src_fixed_array_size_;

  /// True if source type is Optional
  bool src_is_optional_;

  /// Destination size field offset (for VarSoa, 0 for FixedSoa)
  size_t dest_size_field_offset_;

  /// Desitnation size field length (for VarSoa 0 for FixedSoa)
  size_t dest_size_field_length_;

  /// Destination array size (capacity for VarSoa, size for FixedSoa)
  size_t dest_array_size_;

  /// Field mappings from AoS element to SoA field arrays
  std::pmr::vector<AosToSoaFieldMapping> field_mappings_;

  /// Initializers for newly added fields
  std::pmr::vector<std::pair<FieldLayoutInfo, jewels::memory::ObjectPtr<const ClkValueInitializer>>>
    new_field_initializers_;
};

ClkArrayToSoaUpgrader::ClkArrayToSoaUpgrader(ClkArrayToSoaUpgraderParams params)
  : src_type_fqn_(params.src_type_fqn, params.memory_resource),
    dest_type_fqn_(params.dest_type_fqn, params.memory_resource),
    src_element_size_(params.src_element_size),
    src_size_field_offset_(params.src_size_field_offset),
    src_fixed_array_size_(params.src_fixed_array_size),
    src_is_optional_(params.src_is_optional),
    dest_size_field_offset_(params.dest_size_field_offset),
    dest_size_field_length_(params.dest_size_field_length),
    dest_array_size_(params.dest_array_size),
    field_mappings_(std::move(params.field_mappings)),
    new_field_initializers_(std::move(params.new_field_initializers))
{
}

void ClkArrayToSoaUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  // Determine source array length
  size_t src_array_length = src_fixed_array_size_;
  if (src_is_optional_)
  {
    // Optional: read has_value flag (1 byte)
    const bool has_value = (src_span[src_size_field_offset_] != std::byte{0});
    src_array_length = has_value ? 1U : 0U;
  }
  else if (src_size_field_offset_ != 0U)
  {
    // VarArray: read size from size field (uint64_t)
    src_array_length = jewels::memory::bit_cast_to<uint64_t>(
      std::span<const std::byte, sizeof(uint64_t)>{src_span.subspan(src_size_field_offset_, sizeof(uint64_t))});
  }

  // For FixedSoa destination, the array_length is always dest_array_size_
  // For VarSoa destination, write the size and use actual element count
  const size_t dest_array_length = (dest_size_field_offset_ != 0U) ? src_array_length : dest_array_size_;

  if (dest_size_field_offset_ != 0U)
  {
    store_soa_size(dest_span, dest_size_field_offset_, dest_size_field_length_, src_array_length);
  }

  // Transpose: iterate over each element in the source array and extract fields into SoA field arrays
  for (size_t elem_idx = 0U; elem_idx < src_array_length; ++elem_idx)
  {
    const size_t src_elem_offset = elem_idx * src_element_size_;

    for (const auto& mapping : field_mappings_)
    {
      auto src_field = src_span.subspan(src_elem_offset + mapping.src_field_offset, mapping.src_field_size);
      auto dest_field_array = get_soa_field_array(dest_span, mapping.dest_field, dest_array_length);
      auto dest_field = dest_field_array.subspan(elem_idx * mapping.dest_field.size, mapping.dest_field.size);

      try
      {
        mapping.field_upgrader->upgrade(src_field, dest_field);
      }
      catch (const ClkTypeUpgradeError& exc)
      {
        throw ClkTypeUpgradeError(
          fmt::format(
            "Failed to upgrade field {} at element {} from {} to {}: {}",
            mapping.field_num,
            elem_idx,
            src_type_fqn_,
            dest_type_fqn_,
            exc.what()));
      }
    }
  }

  // Initialize newly added fields for all elements
  for (const auto& [new_field, initializer] : new_field_initializers_)
  {
    auto dest_field_array = get_soa_field_array(dest_span, new_field, dest_array_length);

    try
    {
      for (size_t elem_idx = 0U; elem_idx < dest_array_length; ++elem_idx)
      {
        initializer->initialize(dest_field_array.subspan(elem_idx * new_field.size, new_field.size));
      }
    }
    catch (const ClkTypeUpgradeError& exc)
    {
      throw ClkTypeUpgradeError(
        fmt::format("Failed to initialize new field {} in {}: {}", new_field.field_num, dest_type_fqn_, exc.what()));
    }
  }
}

/// Field mapping for SoA-to-AoS upgrade (maps SoA field array layout to schema field offset)
struct SoaToAosFieldMapping
{
  /// Field number
  int32_t field_num;

  /// Source field layout in SoA
  FieldLayoutInfo src_field;

  /// Destination field offset within a single AoS element
  size_t dest_field_offset;

  /// Destination field size
  size_t dest_field_size;

  /// Type upgrader for converting field values
  jewels::memory::ObjectPtr<const ClkTypeUpgrader> field_upgrader;
};

/// Parameters for ClkSoaToArrayUpgrader constructor
struct ClkSoaToArrayUpgraderParams
{
  jewels::memory::MemoryResource memory_resource;
  std::string_view src_type_fqn;
  std::string_view dest_type_fqn;
  size_t src_size_field_offset;
  size_t src_size_field_length;
  size_t src_fixed_array_size;
  size_t dest_element_size;
  size_t dest_size_field_offset;
  size_t dest_array_size;
  std::pmr::vector<SoaToAosFieldMapping> field_mappings;
  std::pmr::vector<ClkFieldInitializer> new_field_initializers;
};

/// Clockwork SoA-to-AoS upgrader (un-transpose from Struct-of-Arrays to Array-of-Structures)
/// Handles conversion from VarSoa/FixedSoa to VarArray/FixedArray/Optional of schema
class ClkSoaToArrayUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor for SoA-to-AoS upgrader
  explicit ClkSoaToArrayUpgrader(ClkSoaToArrayUpgraderParams params);

  ~ClkSoaToArrayUpgrader() override = default;

  ClkSoaToArrayUpgrader(const ClkSoaToArrayUpgrader&) = delete;
  ClkSoaToArrayUpgrader& operator=(const ClkSoaToArrayUpgrader&) = delete;
  ClkSoaToArrayUpgrader(ClkSoaToArrayUpgrader&&) = delete;
  ClkSoaToArrayUpgrader& operator=(ClkSoaToArrayUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source type FQN for error messages
  std::pmr::string src_type_fqn_;

  /// Destination type FQN for error messages
  std::pmr::string dest_type_fqn_;

  /// Source size field offset (for VarSoa, 0 for FixedSoa)
  size_t src_size_field_offset_;

  /// Source size field length (for VarSoa, 0 for FixedSoa)
  size_t src_size_field_length_;

  /// Source fixed array size (for FixedSoa, 0 for VarSoa)
  size_t src_fixed_array_size_;

  /// Destination element size (for AoS)
  size_t dest_element_size_;

  /// Destination size field offset (for VarArray, 0 for FixedArray/Optional)
  size_t dest_size_field_offset_;

  /// Destination array size (capacity for VarArray, size for FixedArray)
  size_t dest_array_size_;

  /// Field mappings from SoA field arrays to AoS element
  std::pmr::vector<SoaToAosFieldMapping> field_mappings_;

  /// Initializers for newly added fields in destination schema
  std::pmr::vector<ClkFieldInitializer> new_field_initializers_;
};

ClkSoaToArrayUpgrader::ClkSoaToArrayUpgrader(ClkSoaToArrayUpgraderParams params)
  : src_type_fqn_(params.src_type_fqn, params.memory_resource),
    dest_type_fqn_(params.dest_type_fqn, params.memory_resource),
    src_size_field_offset_(params.src_size_field_offset),
    src_size_field_length_(params.src_size_field_length),
    src_fixed_array_size_(params.src_fixed_array_size),
    dest_element_size_(params.dest_element_size),
    dest_size_field_offset_(params.dest_size_field_offset),
    dest_array_size_(params.dest_array_size),
    field_mappings_(std::move(params.field_mappings)),
    new_field_initializers_(std::move(params.new_field_initializers))
{
}

void ClkSoaToArrayUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  size_t src_array_length = src_fixed_array_size_;
  if (src_size_field_offset_ != 0U)
  {
    // VarSoa: read size field
    src_array_length = load_soa_size(src_span, src_size_field_offset_, src_size_field_length_);
  }

  if (dest_size_field_offset_ != 0U)
  {
    // VarArray: write size field
    jewels::memory::write_as_bytes(
      static_cast<uint64_t>(src_array_length),
      std::span<std::byte, sizeof(uint64_t)>{dest_span.subspan(dest_size_field_offset_, sizeof(uint64_t))});
  }

  // Un-transpose: iterate over each element index and reconstruct AoS elements from SoA field arrays
  for (size_t elem_idx = 0U; elem_idx < src_array_length; ++elem_idx)
  {
    const size_t dest_elem_offset = elem_idx * dest_element_size_;

    for (const auto& initializer : new_field_initializers_)
    {
      initializer.initialize(dest_span.subspan(dest_elem_offset, dest_element_size_));
    }

    for (const auto& mapping : field_mappings_)
    {
      auto src_field_array = get_soa_field_array(src_span, mapping.src_field, src_array_length);
      auto src_field = src_field_array.subspan(elem_idx * mapping.src_field.size, mapping.src_field.size);
      auto dest_field = dest_span.subspan(dest_elem_offset + mapping.dest_field_offset, mapping.dest_field_size);

      try
      {
        mapping.field_upgrader->upgrade(src_field, dest_field);
      }
      catch (const ClkTypeUpgradeError& exc)
      {
        throw ClkTypeUpgradeError(
          fmt::format(
            "Failed to upgrade field {} at element {} from {} to {}: {}",
            mapping.field_num,
            elem_idx,
            src_type_fqn_,
            dest_type_fqn_,
            exc.what()));
      }
    }
  }

  // For FixedArray destinations with fewer source elements, initialize remaining elements with defaults
  if (dest_size_field_offset_ == 0U && src_array_length < dest_array_size_)
  {
    for (size_t elem_idx = src_array_length; elem_idx < dest_array_size_; ++elem_idx)
    {
      const size_t dest_elem_offset = elem_idx * dest_element_size_;
      for (const auto& initializer : new_field_initializers_)
      {
        initializer.initialize(dest_span.subspan(dest_elem_offset, dest_element_size_));
      }
    }
  }
}

/// Parameters for ClkSoaToOptionalUpgrader constructor
struct ClkSoaToOptionalUpgraderParams
{
  jewels::memory::MemoryResource memory_resource;
  std::string_view src_type_fqn;
  std::string_view dest_type_fqn;
  size_t src_size_field_offset;
  size_t src_size_field_length;
  size_t src_fixed_array_size;
  size_t dest_element_size;
  size_t dest_has_value_offset;
  std::pmr::vector<SoaToAosFieldMapping> field_mappings;
  std::pmr::vector<ClkFieldInitializer> new_field_initializers;
};

/// Clockwork SoA-to-Optional upgrader (un-transpose from SoA to Optional<Schema>)
/// Handles conversion from VarSoa/FixedSoa to Optional<Schema>
/// Requires that the SoA has at most 1 element
class ClkSoaToOptionalUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor for SoA-to-Optional upgrader
  explicit ClkSoaToOptionalUpgrader(ClkSoaToOptionalUpgraderParams params);

  ~ClkSoaToOptionalUpgrader() override = default;

  ClkSoaToOptionalUpgrader(const ClkSoaToOptionalUpgrader&) = delete;
  ClkSoaToOptionalUpgrader& operator=(const ClkSoaToOptionalUpgrader&) = delete;
  ClkSoaToOptionalUpgrader(ClkSoaToOptionalUpgrader&&) = delete;
  ClkSoaToOptionalUpgrader& operator=(ClkSoaToOptionalUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source type FQN for error messages
  std::pmr::string src_type_fqn_;

  /// Destination type FQN for error messages
  std::pmr::string dest_type_fqn_;

  /// Source size field offset (for VarSoa, 0 for FixedSoa)
  size_t src_size_field_offset_;

  /// Source size field length (for VarSoa, 0 for FixedSoa)
  size_t src_size_field_length_;

  /// Source fixed array size (for FixedSoa, 0 for VarSoa)
  size_t src_fixed_array_size_;

  /// Destination element size
  size_t dest_element_size_;

  /// Destination has_value field offset
  size_t dest_has_value_offset_;

  /// Field mappings from SoA field arrays to Optional element
  std::pmr::vector<SoaToAosFieldMapping> field_mappings_;

  /// Initializers for newly added fields in destination schema
  std::pmr::vector<ClkFieldInitializer> new_field_initializers_;
};

ClkSoaToOptionalUpgrader::ClkSoaToOptionalUpgrader(ClkSoaToOptionalUpgraderParams params)
  : src_type_fqn_(params.src_type_fqn, params.memory_resource),
    dest_type_fqn_(params.dest_type_fqn, params.memory_resource),
    src_size_field_offset_(params.src_size_field_offset),
    src_size_field_length_(params.src_size_field_length),
    src_fixed_array_size_(params.src_fixed_array_size),
    dest_element_size_(params.dest_element_size),
    dest_has_value_offset_(params.dest_has_value_offset),
    field_mappings_(std::move(params.field_mappings)),
    new_field_initializers_(std::move(params.new_field_initializers))
{
}

void ClkSoaToOptionalUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  size_t src_array_length = src_fixed_array_size_;
  if (src_size_field_offset_ != 0U)
  {
    // VarSoa: read size field
    src_array_length = load_soa_size(src_span, src_size_field_offset_, src_size_field_length_);
  }

  // Optional can only hold 0 or 1 elements
  if (src_array_length > 1U)
  {
    throw ClkTypeUpgradeError(
      fmt::format("Cannot convert SoA with {} elements to Optional (max 1 element)", src_array_length));
  }

  const bool has_value = (src_array_length == 1U);
  dest_span[dest_has_value_offset_] = static_cast<std::byte>(has_value ? 1 : 0);

  if (!has_value)
  {
    return;
  }

  for (const auto& initializer : new_field_initializers_)
  {
    initializer.initialize(dest_span.subspan(0U, dest_element_size_));
  }

  // Un-transpose the single element
  for (const auto& mapping : field_mappings_)
  {
    auto src_field_array = get_soa_field_array(src_span, mapping.src_field, 1U);
    auto src_field = src_field_array.subspan(0U, mapping.src_field.size);
    auto dest_field = dest_span.subspan(mapping.dest_field_offset, mapping.dest_field_size);

    try
    {
      mapping.field_upgrader->upgrade(src_field, dest_field);
    }
    catch (const ClkTypeUpgradeError& exc)
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Failed to upgrade field {} from {} to {}: {}",
          mapping.field_num,
          src_type_fqn_,
          dest_type_fqn_,
          exc.what()));
    }
  }
}

// Forward declarations for helper functions used by make_upgrader methods
void build_soa_to_aos_upgrader_components(
  const jewels::memory::MemoryResource& memory_resource,
  jewels::Out<std::pmr::vector<SoaToAosFieldMapping>> field_mappings_out,
  jewels::Out<std::pmr::vector<ClkFieldInitializer>> new_field_initializers_out,
  const ClkSchemaType& src_schema,
  const ClkSchemaType& dest_schema,
  const std::pmr::vector<FieldLayoutInfo>& src_field_layouts);

/// Parameters for make_soa_to_array_upgrader helper function
struct MakeSoaToArrayUpgraderParams
{
  jewels::memory::MemoryResource memory_resource;
  std::reference_wrapper<std::pmr::unordered_map<size_t, jewels::memory::NonNullSharedPtr<const ClkTypeUpgrader>>>
    upgrader_cache;
  std::reference_wrapper<const ClkType> src_type;
  std::reference_wrapper<const ClkSchemaType> src_schema;
  std::reference_wrapper<const ClkSchemaType> dest_schema;
  std::reference_wrapper<const std::pmr::vector<FieldLayoutInfo>> src_field_layouts;
  size_t src_size_field_offset;
  size_t src_size_field_length;
  size_t src_fixed_array_size;
  size_t dest_element_size;
  size_t dest_size_field_offset;
  size_t dest_array_size;
};

[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader>
make_soa_to_array_upgrader(const MakeSoaToArrayUpgraderParams& params);

/// Parameters for make_soa_to_optional_upgrader helper function
struct MakeSoaToOptionalUpgraderParams
{
  jewels::memory::MemoryResource memory_resource;
  std::reference_wrapper<std::pmr::unordered_map<size_t, jewels::memory::NonNullSharedPtr<const ClkTypeUpgrader>>>
    upgrader_cache;
  std::reference_wrapper<const ClkType> src_type;
  std::reference_wrapper<const ClkSchemaType> src_schema;
  std::reference_wrapper<const ClkSchemaType> dest_schema;
  std::reference_wrapper<const std::pmr::vector<FieldLayoutInfo>> src_field_layouts;
  size_t src_size_field_offset;
  size_t src_size_field_length;
  size_t src_fixed_array_size;
  size_t dest_element_size;
  size_t dest_has_value_offset;
};

[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader>
make_soa_to_optional_upgrader(const MakeSoaToOptionalUpgraderParams& params);

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
  void check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name) override;
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

void ClkTagType::check_for_unexpected_schema_changes(ClkType& src_type, bool /*allow_changes*/, std::string_view name)
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
  /// @param[in] memory_resource Memory resource
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] metadata_version Schema mnetadata version
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  ClkBuiltInType(
    jewels::memory::MemoryResource memory_resource,
    std::string_view fqn,
    ClkTypeId type_id,
    size_t type_index,
    int32_t metadata_version,
    size_t size,
    size_t alignment);

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
  void check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name) override;

  /// Check for unexpected underlying schema changes
  /// @param[in] src_type Source clockwor type
  /// @param[in] name Name to use in error messages
  /// @throws runtime_error if an unexpected schema change is found
  void check_for_unexpected_underlying_schema_changes(ClkType& src_type, std::string_view name);

private:
  /// Type size in bytes
  size_t size_;

  /// Type alignment in bytes
  size_t alignment_;
};

ClkBuiltInType::ClkBuiltInType(
  jewels::memory::MemoryResource memory_resource,
  std::string_view fqn,
  ClkTypeId type_id,
  size_t type_index,
  int32_t metadata_version,
  size_t size,
  size_t alignment)
  : ClkType(std::move(memory_resource), fqn, type_id, type_index, metadata_version), size_(size), alignment_(alignment)
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

void ClkBuiltInType::check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name)
{
  if (!allow_changes && src_type.get_type_id() != get_type_id())
  {
    throw ClkTypeUpgradeError(
      fmt::format("Unexpected type change from {} to {} for {}", src_type.get_type_id(), get_type_id(), name));
  }
}

void ClkBuiltInType::check_for_unexpected_underlying_schema_changes(ClkType& src_type, std::string_view name)
{
  auto& this_underlying_type = get_lowest_underlying_type();
  if (
    this_underlying_type.get_type_id() == ClkTypeId::schema ||
    this_underlying_type.get_type_id() == ClkTypeId::clk_enum)
  {
    auto& src_underlying_type = src_type.get_lowest_underlying_type();
    this_underlying_type.check_for_unexpected_schema_changes(src_underlying_type, true, name);
  }
}

/// Clockwork fixed-size bitset type.
class ClkBitsetType : public ClkBuiltInType
{
public:
  /// Create an instance from a Tachyon built-in type protobuf.
  [[nodiscard]] static std::shared_ptr<ClkBitsetType> from_proto(
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
    const auto bit_size = parse_size_parameter(builtin_proto.fqn(), builtin_proto.arguments(0).value());
    if (bit_size == 0U)
    {
      throw ClkTypeUpgradeError(fmt::format("Invalid Bitset size 0 for {}", builtin_proto.fqn()));
    }
    constexpr size_t bits_per_byte{8U};
    const auto byte_size = (bit_size / bits_per_byte) + ((bit_size % bits_per_byte) == 0U ? 0U : 1U);
    if (
      byte_size > static_cast<size_t>(std::numeric_limits<int32_t>::max()) ||
      builtin_proto.size() != static_cast<int32_t>(byte_size) || builtin_proto.alignment() != 1)
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Invalid layout for Bitset<{}>: size {} alignment {}",
          bit_size,
          builtin_proto.size(),
          builtin_proto.alignment()));
    }
    return jewels::memory::make_pmr_shared<ClkBitsetType>(
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(builtin_proto.fqn()),
      type_index,
      factory->get_metadata_version(),
      byte_size,
      bit_size,
      factory);
  }

  ClkBitsetType(
    std::string_view fqn,
    size_t type_index,
    int32_t metadata_version,
    size_t byte_size,
    size_t bit_size,
    jewels::memory::ObjectPtr<ClkTypeFactory> factory)
    : ClkBuiltInType(
        factory->get_memory_resource(), fqn, ClkTypeId::bitset, type_index, metadata_version, byte_size, 1U),
      bit_size_(bit_size)
  {
  }

  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override
  {
    if (src_type.get_type_id() != ClkTypeId::bitset || !is_same_type(src_type))
    {
      throw ClkTypeUpgradeError(fmt::format("Cannot upgrade from {} to Bitset<{}>", src_type.get_fqn(), bit_size_));
    }
    auto& upgrader_cache = get_upgrader_cache();
    if (const auto cache_iter = upgrader_cache.find(src_type.get_type_index()); cache_iter != upgrader_cache.end())
    {
      return jewels::memory::make_non_null_from_ref(*cache_iter->second);
    }
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkMemcpyUpgrader>(get_memory_resource(), get_size()))
         .first->second);
  }

  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override
  {
    return is_same_type(src_type);
  }

  [[nodiscard]] bool is_legacy_wire_compatible(const ClkType& src_type) const override
  {
    return is_same_type(src_type) && ClkBuiltInType::is_legacy_wire_compatible(src_type);
  }

  void check_for_unexpected_schema_changes(ClkType& src_type, bool /*allow_changes*/, std::string_view name) override
  {
    if (!is_same_type(src_type))
    {
      throw ClkTypeUpgradeError(fmt::format("Unexpected Bitset size change for {}", name));
    }
  }

  [[nodiscard]] bool is_same_type(const ClkType& src_type) const override
  {
    if (src_type.get_type_id() != ClkTypeId::bitset)
    {
      return false;
    }
    return bit_size_ == dynamic_cast<const ClkBitsetType&>(src_type).bit_size_;
  }

private:
  size_t bit_size_;
};

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
  /// @param[in] memory_resource Memory resource
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] metadata_version Schema metadata version
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  ClkPrimitiveType(
    jewels::memory::MemoryResource memory_resource,
    std::string_view fqn,
    ClkTypeId type_id,
    size_t type_index,
    int32_t metadata_version,
    size_t size,
    size_t alignment);

  ~ClkPrimitiveType() override = default;

  ClkPrimitiveType(const ClkPrimitiveType&) = delete;
  ClkPrimitiveType& operator=(const ClkPrimitiveType&) = delete;
  ClkPrimitiveType(ClkPrimitiveType&&) = delete;
  ClkPrimitiveType& operator=(ClkPrimitiveType&&) = delete;

  /// @see ClkType::make_value_initializer
  [[nodiscard]] std::shared_ptr<ClkValueInitializer>
  make_value_initializer(const metadata::InitialValue& value) const override;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;
};

template <typename ValueType>
ClkPrimitiveType<ValueType>::ClkPrimitiveType(
  jewels::memory::MemoryResource memory_resource,
  std::string_view fqn,
  ClkTypeId type_id,
  size_t type_index,
  int32_t metadata_version,
  size_t size,
  size_t alignment)
  : ClkBuiltInType(std::move(memory_resource), fqn, type_id, type_index, metadata_version, size, alignment)
{
  if (size != sizeof(ValueType))
  {
    throw ClkTypeUpgradeError(fmt::format("Invalid size for {}, got {} expected {}", type_id, size, sizeof(ValueType)));
  }
}

template <typename ValueType>
[[nodiscard]] std::shared_ptr<ClkValueInitializer>
ClkPrimitiveType<ValueType>::make_value_initializer(const metadata::InitialValue& value) const
{
  if constexpr (std::is_same_v<ValueType, bool>)
  {
    return jewels::memory::make_pmr_shared<ClkPrimitiveInitializer<ValueType>>(
      get_memory_resource(), value.bool_value());
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
    *upgrader_cache
       .emplace(
         src_type.get_type_index(),
         jewels::memory::make_pmr_shared<ClkPrimitiveUpgrader<ValueType>>(get_memory_resource()))
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
  /// @param[in] memory_resource Memory resource
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] metadata_version Schema metadata version
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  ClkIntegerType(
    jewels::memory::MemoryResource memory_resource,
    std::string_view fqn,
    ClkTypeId type_id,
    size_t type_index,
    int32_t metadata_version,
    size_t size,
    size_t alignment);

  ~ClkIntegerType() override = default;

  ClkIntegerType(const ClkIntegerType&) = delete;
  ClkIntegerType& operator=(const ClkIntegerType&) = delete;
  ClkIntegerType(ClkIntegerType&&) = delete;
  ClkIntegerType& operator=(ClkIntegerType&&) = delete;

  /// @see ClkType::make_value_initializer
  [[nodiscard]] std::shared_ptr<ClkValueInitializer>
  make_value_initializer(const metadata::InitialValue& value) const override;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;
};

template <typename ValueType>
ClkIntegerType<ValueType>::ClkIntegerType(
  jewels::memory::MemoryResource memory_resource,
  std::string_view fqn,
  ClkTypeId type_id,
  size_t type_index,
  int32_t metadata_version,
  size_t size,
  size_t alignment)
  : ClkBuiltInType(std::move(memory_resource), fqn, type_id, type_index, metadata_version, size, alignment)
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
[[nodiscard]] std::shared_ptr<ClkValueInitializer>
ClkIntegerType<ValueType>::make_value_initializer(const metadata::InitialValue& value) const
{
  if constexpr (std::is_unsigned_v<ValueType>)
  {
    return jewels::memory::make_pmr_shared<ClkPrimitiveInitializer<ValueType>>(
      get_memory_resource(), value.unsigned_value());
  }
  return jewels::memory::make_pmr_shared<ClkPrimitiveInitializer<ValueType>>(
    get_memory_resource(), value.signed_value());
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
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<uint8_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::uint16:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<uint16_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::uint32:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<uint32_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::uint64:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<uint64_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::int8:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<int8_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::int16:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<int16_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::int32:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<int32_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::int64:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<int64_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::float32:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<float, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::float64:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<double, ValueType>>(get_memory_resource()))
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
  /// @param[in] memory_resource Memory resource
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] metadata_version Schema metadata version
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  ClkTimeType(
    jewels::memory::MemoryResource memory_resource,
    std::string_view fqn,
    ClkTypeId type_id,
    size_t type_index,
    int32_t metadata_version,
    size_t size,
    size_t alignment);

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

ClkTimeType::ClkTimeType(
  jewels::memory::MemoryResource memory_resource,
  std::string_view fqn,
  ClkTypeId type_id,
  size_t type_index,
  int32_t metadata_version,
  size_t size,
  size_t alignment)
  : ClkIntegerType<int64_t>(std::move(memory_resource), fqn, type_id, type_index, metadata_version, size, alignment)
{
}

[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> ClkTimeType::make_upgrader(const ClkType& src_type)
{
  if (src_type.get_type_id() == get_type_id())
  {
    return jewels::memory::make_non_null_from_ref(
      *get_upgrader_cache()
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<int64_t, int64_t>>(get_memory_resource()))
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
  /// @param[in] memory_resource
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] metadata_version Schema metadata version
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  ClkFloatingPointType(
    jewels::memory::MemoryResource memory_resource,
    std::string_view fqn,
    ClkTypeId type_id,
    size_t type_index,
    int32_t metadata_version,
    size_t size,
    size_t alignment);

  ~ClkFloatingPointType() override = default;

  ClkFloatingPointType(const ClkFloatingPointType&) = delete;
  ClkFloatingPointType& operator=(const ClkFloatingPointType&) = delete;
  ClkFloatingPointType(ClkFloatingPointType&&) = delete;
  ClkFloatingPointType& operator=(ClkFloatingPointType&&) = delete;

  /// @see ClkType::make_value_initializer
  [[nodiscard]] std::shared_ptr<ClkValueInitializer>
  make_value_initializer(const metadata::InitialValue& value) const override;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;
};

template <typename ValueType>
ClkFloatingPointType<ValueType>::ClkFloatingPointType(
  jewels::memory::MemoryResource memory_resource,
  std::string_view fqn,
  ClkTypeId type_id,
  size_t type_index,
  int32_t metadata_version,
  size_t size,
  size_t alignment)
  : ClkBuiltInType(std::move(memory_resource), fqn, type_id, type_index, metadata_version, size, alignment)
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
[[nodiscard]] std::shared_ptr<ClkValueInitializer>
ClkFloatingPointType<ValueType>::make_value_initializer(const metadata::InitialValue& value) const
{
  if constexpr (std::is_same_v<ValueType, float>)
  {
    return jewels::memory::make_pmr_shared<ClkPrimitiveInitializer<ValueType>>(
      get_memory_resource(), string_to_float(value.float_value()));
  }
  return jewels::memory::make_pmr_shared<ClkPrimitiveInitializer<ValueType>>(
    get_memory_resource(), string_to_double(value.float_value()));
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
  case ClkTypeId::uint8:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<uint8_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::uint16:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<uint16_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::uint32:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<uint32_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::uint64:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<uint64_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::int8:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<int8_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::int16:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<int16_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::int32:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<int32_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::int64:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<int64_t, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::float32:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<float, ValueType>>(get_memory_resource()))
         .first->second);
  case ClkTypeId::float64:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkNumericUpgrader<double, ValueType>>(get_memory_resource()))
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

/// Clockwork lite-compressor for fixed array types
class ClkFixedArrayLiteCompressor : public ClkTypeLiteCompressor
{
public:
  /// Constructor
  /// @param[in] array_size Number of elements in the array
  /// @param[in] element_size Element size in bytes
  /// @param[in] compressor Element lite-compressor
  ClkFixedArrayLiteCompressor(
    size_t array_size, size_t element_size, std::shared_ptr<ClkTypeLiteCompressor> compressor);

  ~ClkFixedArrayLiteCompressor() noexcept override = default;

  ClkFixedArrayLiteCompressor(const ClkFixedArrayLiteCompressor&) = delete;
  ClkFixedArrayLiteCompressor& operator=(const ClkFixedArrayLiteCompressor&) = delete;
  ClkFixedArrayLiteCompressor(ClkFixedArrayLiteCompressor&&) = delete;
  ClkFixedArrayLiteCompressor& operator=(ClkFixedArrayLiteCompressor&&) = delete;

  /// @see ClkTypeLiteCompressor::compress
  jewels::BinaryOutcome compress(
    std::span<const std::byte> data_span,
    size_t offset,
    InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const override;

private:
  /// Number of elements in the array
  size_t array_size_;

  /// Array lite-compressor
  ClkArrayLiteCompressor compressor_;
};

ClkFixedArrayLiteCompressor::ClkFixedArrayLiteCompressor(
  size_t array_size, size_t element_size, std::shared_ptr<ClkTypeLiteCompressor> compressor)
  : array_size_(array_size), compressor_(array_size, element_size, std::move(compressor))
{
}

jewels::BinaryOutcome ClkFixedArrayLiteCompressor::compress(
  std::span<const std::byte> data_span, size_t offset, InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const
{
  return compressor_.compress(array_size_, data_span, offset, InOut{*zero_chunks});
}

/// Clockwork fixed array type
class ClkFixedArrayType : public ClkBuiltInType
{
public:
  /// Constructor, use from_proto to create an instance
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] metadata_version Schema metadata version
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  /// @param[in] element_type_index Array element type index
  /// @param[in] array_size Number of elements in the array
  /// @param[in] factory Clockwork type factory
  ClkFixedArrayType(
    std::string_view fqn,
    ClkTypeId type_id,
    size_t type_index,
    int32_t metadata_version,
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
  [[nodiscard]] static std::shared_ptr<ClkFixedArrayType> from_proto(
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
  void check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name) override;

  /// @see ClkType::get_lowest_underlying_type
  [[nodiscard]] ClkType& get_lowest_underlying_type() override;

  /// @see ClkType::is_same_type
  [[nodiscard]] bool is_same_type(const ClkType& src_type) const override;

  /// @see ClkType::make_lite_compressor
  [[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> make_lite_compressor() override;

  /// @see ClkType::is_compressible
  [[nodiscard]] bool is_compressible() override;

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

/// Clockwork lite-compressor for variable array types
class ClkVarArrayLiteCompressor : public ClkTypeLiteCompressor
{
public:
  /// Constructor
  /// @param[in] max_size Maximum array size
  /// @param[in] size_offset Array size offset
  /// @param[in] element_size Element size in bytes
  /// @param[in] compressor Element lite-compressor
  ClkVarArrayLiteCompressor(
    size_t max_size, size_t size_offset, size_t element_size, std::shared_ptr<ClkTypeLiteCompressor> compressor);

  ~ClkVarArrayLiteCompressor() noexcept override = default;

  ClkVarArrayLiteCompressor(const ClkVarArrayLiteCompressor&) = delete;
  ClkVarArrayLiteCompressor& operator=(const ClkVarArrayLiteCompressor&) = delete;
  ClkVarArrayLiteCompressor(ClkVarArrayLiteCompressor&&) = delete;
  ClkVarArrayLiteCompressor& operator=(ClkVarArrayLiteCompressor&&) = delete;

  /// @see ClkTypeLiteCompressor::compress
  jewels::BinaryOutcome compress(
    std::span<const std::byte> data_span,
    size_t offset,
    InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const override;

private:
  /// Maximum number of elements in the array
  size_t max_size_;

  /// Array size offset
  size_t size_offset_;

  /// Element size in bytes
  size_t element_size_;

  /// Array lite-compressor
  ClkArrayLiteCompressor compressor_;
};

ClkVarArrayLiteCompressor::ClkVarArrayLiteCompressor(
  size_t max_size, size_t size_offset, size_t element_size, std::shared_ptr<ClkTypeLiteCompressor> compressor)
  : max_size_(max_size),
    size_offset_(size_offset),
    element_size_(element_size),
    compressor_(max_size, element_size, std::move(compressor))
{
}

jewels::BinaryOutcome ClkVarArrayLiteCompressor::compress(
  std::span<const std::byte> data_span, size_t offset, InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const
{
  const auto array_size = jewels::memory::bit_cast_to<uint64_t>(
    std::span<const std::byte, sizeof(uint64_t)>{data_span.subspan(size_offset_, sizeof(uint64_t))});
  if (array_size == 0U)
  {
    zero_chunks->emplace_back(offset, data_span.size());
    return success;
  }
  if (!ok(compressor_.compress(array_size, data_span, offset, InOut{*zero_chunks})))
  {
    return failure;
  }
  if (array_size < max_size_)
  {
    zero_chunks->emplace_back(offset + (element_size_ * array_size), size_offset_ - (element_size_ * array_size));
  }
  return success;
}

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
  /// @param[in] metadata_version Schema metadata version
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  /// @param[in] element_type_index Array element type index
  /// @param[in] array_size Number of elements in the array
  /// @param[in] factory Clockwork type factory
  ClkVarArrayType(
    std::string_view fqn,
    ClkTypeId type_id,
    size_t type_index,
    int32_t metadata_version,
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
  [[nodiscard]] static std::shared_ptr<ClkVarArrayType> from_proto(
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
  void check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name) override;

  /// @see ClkType::get_lowest_underlying_type
  [[nodiscard]] ClkType& get_lowest_underlying_type() override;

  /// @see ClkType::is_same_type
  [[nodiscard]] bool is_same_type(const ClkType& src_type) const override;

  /// @see ClkType::make_lite_compressor
  [[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> make_lite_compressor() override;

  /// @see ClkType::is_compressible
  [[nodiscard]] bool is_compressible() override;

private:
  /// Element type index
  size_t element_type_index_;

  /// Array size
  size_t array_size_;

  /// Clockwork type factory
  jewels::memory::ObjectPtr<ClkTypeFactory> factory_;
};

/// Clockwork tensor type
class ClkTensorType : public ClkBuiltInType
{
public:
  /// Constructor, use from_proto to create an instance
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] metadata_version Schema metadata version
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  /// @param[in] element_type_index Array element type index
  /// @param[in] shape The shape array
  /// @param[in] layout The memory layout enum value encoded as a string
  /// @param[in] factory Clockwork type factory
  ClkTensorType(
    std::string_view fqn,
    ClkTypeId type_id,
    size_t type_index,
    int32_t metadata_version,
    size_t size,
    size_t alignment,
    size_t element_type_index,
    const std::pmr::vector<size_t>& shape,
    std::pmr::string layout,
    jewels::memory::ObjectPtr<ClkTypeFactory> factory);

  ~ClkTensorType() override = default;

  ClkTensorType(const ClkTensorType&) = delete;
  ClkTensorType& operator=(const ClkTensorType&) = delete;
  ClkTensorType(ClkTensorType&&) = delete;
  ClkTensorType& operator=(ClkTensorType&&) = delete;

  /// Create an instance from a tachyon model protobuf
  /// @param[in] factory Clockwork type factory
  /// @param[in] builtin_proto Tachyon built in type protobuf
  /// @param[in] type_index Type index
  /// @param[in] maybe_strong_type_fqn Optional fully qualified name of a strong type wrapping this schema
  /// @returns Clockwork type instance
  /// @throws runtime_error on failure.
  [[nodiscard]] static std::shared_ptr<ClkTensorType> from_proto(
    jewels::memory::ObjectPtr<ClkTypeFactory> factory,
    const metadata::BuiltInType& builtin_proto,
    size_t type_index,
    std::optional<std::string_view> maybe_strong_type_fqn);

  /// @return Element type
  [[nodiscard]] const ClkType& get_element_type() const;

  /// @return Element type
  [[nodiscard]] ClkType& get_element_type();

  /// @return Get the shape array
  [[nodiscard]] const std::pmr::vector<size_t>& get_shape() const noexcept;

  /// @return The number of elements in the backing array.
  [[nodiscard]] size_t num_elements() const noexcept;

  /// @return Get the memory layout
  [[nodiscard]] std::string_view get_layout() const noexcept;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;

  /// @see ClkType::is_legacy_wire_compatible
  [[nodiscard]] bool is_legacy_wire_compatible(const ClkType& src_type) const override;

  /// @see ClkType::check_for_unexpected_schema_changes
  void check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name) override;

  /// @see ClkType::get_lowest_underlying_type
  [[nodiscard]] ClkType& get_lowest_underlying_type() override;

  /// @see ClkType::is_same_type
  [[nodiscard]] bool is_same_type(const ClkType& src_type) const override;

  /// @see ClkType::make_lite_compressor
  [[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> make_lite_compressor() override;

  /// @see ClkType::is_compressible
  [[nodiscard]] bool is_compressible() override;

private:
  /// Element type index
  size_t element_type_index_;

  /// Tensor dimensions
  std::pmr::vector<size_t> shape_;

  /// Memory layout
  std::pmr::string layout_;

  /// Clockwork type factory
  jewels::memory::ObjectPtr<ClkTypeFactory> factory_;
};

/// ClkVarStringType constructor parameters
struct ClkVarStringTypeParams
{
  jewels::memory::MemoryResource memory_resource;
  std::string_view fqn;
  ClkTypeId type_id;
  size_t type_index;
  int32_t metadata_version;
  size_t size;
  size_t alignment;
  size_t string_size;
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
  /// @param[in] params Constructor parameters
  explicit ClkVarStringType(const ClkVarStringTypeParams& params);

  ~ClkVarStringType() override = default;

  ClkVarStringType(const ClkVarStringType&) = delete;
  ClkVarStringType& operator=(const ClkVarStringType&) = delete;
  ClkVarStringType(ClkVarStringType&&) = delete;
  ClkVarStringType& operator=(ClkVarStringType&&) = delete;

  /// Create an instance from a tachyon built in variable string type protobuf
  /// @param[in] factory Clockwork type factory
  /// @param[in] builtin_proto Tachyon built in type protobuf
  /// @param[in] type_index Type index
  /// @param[in] maybe_strong_type_fqn Optional fully qualified name of a strong type wrapping this schema
  /// @returns Clockwork type instance
  /// @throws runtime_error on failure.
  [[nodiscard]] static std::shared_ptr<ClkVarStringType> from_proto(
    jewels::memory::ObjectPtr<ClkTypeFactory> factory,
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
  void check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name) override;

  /// @see ClkType::is_same_type
  [[nodiscard]] bool is_same_type(const ClkType& src_type) const override;

  /// @see ClkType::make_lite_compressor
  [[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> make_lite_compressor() override;

  /// @see ClkType::is_compressible
  [[nodiscard]] bool is_compressible() override;

private:
  /// String size
  size_t string_size_;
};

/// Clockwork lite-compressor for optional types
class ClkOptionalLiteCompressor : public ClkTypeLiteCompressor
{
public:
  /// Constructor
  /// @param[in] element_size Element size in bytes
  /// @param[in] compressor Element lite-compressor
  ClkOptionalLiteCompressor(size_t element_size, std::shared_ptr<ClkTypeLiteCompressor> compressor);

  ~ClkOptionalLiteCompressor() noexcept override = default;

  ClkOptionalLiteCompressor(const ClkOptionalLiteCompressor&) = delete;
  ClkOptionalLiteCompressor& operator=(const ClkOptionalLiteCompressor&) = delete;
  ClkOptionalLiteCompressor(ClkOptionalLiteCompressor&&) = delete;
  ClkOptionalLiteCompressor& operator=(ClkOptionalLiteCompressor&&) = delete;

  /// @see ClkTypeLiteCompressor::compress
  jewels::BinaryOutcome compress(
    std::span<const std::byte> data_span,
    size_t offset,
    InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const override;

private:
  /// Element size in bytes
  size_t element_size_;

  /// Element compressor, set to nullptr if element type is incompressible
  std::shared_ptr<ClkTypeLiteCompressor> compressor_;
};

ClkOptionalLiteCompressor::ClkOptionalLiteCompressor(
  size_t element_size, std::shared_ptr<ClkTypeLiteCompressor> compressor)
  : element_size_(element_size), compressor_(std::move(compressor))
{
}

jewels::BinaryOutcome ClkOptionalLiteCompressor::compress(
  std::span<const std::byte> data_span, size_t offset, InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const
{
  if (data_span[optional_has_value_offset(element_size_)] == std::byte{0})
  {
    zero_chunks->emplace_back(offset, data_span.size());
    return success;
  }
  if (compressor_)
  {
    return compressor_->compress(data_span.subspan(0, element_size_), offset, InOut{*zero_chunks});
  }
  ClkTypeLiteCompressor::compress_opaque_data(data_span, offset, InOut{*zero_chunks});
  return success;
}

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
  /// @param[in] metadata_version Schema metadata version
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  /// @param[in] element_type_index Element type index
  /// @param[in] factory Clockwork type factory
  ClkOptionalType(
    std::string_view fqn,
    ClkTypeId type_id,
    size_t type_index,
    int32_t metadata_version,
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
  [[nodiscard]] static std::shared_ptr<ClkOptionalType> from_proto(
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
  void check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name) override;

  /// @see ClkType::get_lowest_underlying_type
  [[nodiscard]] ClkType& get_lowest_underlying_type() override;

  /// @see ClkType::is_same_type
  [[nodiscard]] bool is_same_type(const ClkType& src_type) const override;

  /// @see ClkType::make_lite_compressor
  [[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> make_lite_compressor() override;

  /// @see ClkType::is_compressible
  [[nodiscard]] bool is_compressible() override;

private:
  /// Element type index
  size_t element_type_index_;

  /// Representations for the types in the protobuf schema
  jewels::memory::ObjectPtr<ClkTypeFactory> factory_;
};

/// Clockwork lite-compressor for FixedSoa fields
class ClkFixedSoaFieldLiteCompressor
{
public:
  /// Constructor
  /// @param[in] soa_size SOA size
  /// @param[in] field_offset Field offset in the SOA
  /// @param[in] element_size Element size in bytes
  /// @param[in] compressor Element lite-compressor
  ClkFixedSoaFieldLiteCompressor(
    size_t soa_size, size_t field_offset, size_t element_size, std::shared_ptr<ClkTypeLiteCompressor> compressor);

  /// Locate the chunks of zeros that can be replaced with a count in the lite compressed message
  /// @param[in] soa_span Data span containing the SOA to compress
  /// @param[in] offset Message offset to the SOA
  /// @param[in,out] zero_chunks Storage for the zero chunks
  /// @return Success of failure
  jewels::BinaryOutcome compress(
    std::span<const std::byte> soa_span,
    size_t offset,
    jewels::InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const;

  /// Comparison operator for sorting field compressors by field offset so that the chunks of zeros
  /// are returned sorted by the offset into the schema
  [[nodiscard]] friend bool
  operator<(const ClkFixedSoaFieldLiteCompressor& lhs, const ClkFixedSoaFieldLiteCompressor& rhs) noexcept
  {
    return lhs.field_offset_ < rhs.field_offset_;
  }

private:
  /// SOA size
  size_t soa_size_;

  /// Field offset in the SOA
  size_t field_offset_;

  /// Element size in bytes
  size_t element_size_;

  /// Array lite compressor
  ClkArrayLiteCompressor compressor_;
};

ClkFixedSoaFieldLiteCompressor::ClkFixedSoaFieldLiteCompressor(
  size_t soa_size, size_t field_offset, size_t element_size, std::shared_ptr<ClkTypeLiteCompressor> compressor)
  : soa_size_(soa_size),
    field_offset_(field_offset),
    element_size_(element_size),
    compressor_(soa_size, element_size, std::move(compressor))
{
}

jewels::BinaryOutcome ClkFixedSoaFieldLiteCompressor::compress(
  std::span<const std::byte> soa_span, size_t offset, jewels::InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const
{
  return compressor_.compress(
    soa_size_, soa_span.subspan(field_offset_, soa_size_ * element_size_), offset + field_offset_, InOut{*zero_chunks});
}

/// Clockwork lite-compressor for fixed SOA types
class ClkFixedSoaLiteCompressor : public ClkTypeLiteCompressor
{
public:
  /// Constructor
  /// @param[in] compressors Field lite-compressor
  explicit ClkFixedSoaLiteCompressor(std::pmr::vector<ClkFixedSoaFieldLiteCompressor> compressors);

  ~ClkFixedSoaLiteCompressor() noexcept override = default;

  ClkFixedSoaLiteCompressor(const ClkFixedSoaLiteCompressor&) = delete;
  ClkFixedSoaLiteCompressor& operator=(const ClkFixedSoaLiteCompressor&) = delete;
  ClkFixedSoaLiteCompressor(ClkFixedSoaLiteCompressor&&) = delete;
  ClkFixedSoaLiteCompressor& operator=(ClkFixedSoaLiteCompressor&&) = delete;

  /// @see ClkTypeLiteCompressor::compress
  jewels::BinaryOutcome compress(
    std::span<const std::byte> data_span,
    size_t offset,
    InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const override;

private:
  /// SOA field lite-compressors
  std::pmr::vector<ClkFixedSoaFieldLiteCompressor> compressors_;
};

ClkFixedSoaLiteCompressor::ClkFixedSoaLiteCompressor(std::pmr::vector<ClkFixedSoaFieldLiteCompressor> compressors)
  : compressors_(std::move(compressors))
{
}

jewels::BinaryOutcome ClkFixedSoaLiteCompressor::compress(
  std::span<const std::byte> data_span, size_t offset, InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const
{
  for (const auto& compressor : compressors_)
  {
    if (!ok(compressor.compress(data_span, offset, InOut{*zero_chunks})))
    {
      return failure;
    }
  }
  return success;
}

/// Clockwork fixed SoA (Struct-of-Arrays) type
class ClkFixedSoaType : public ClkBuiltInType
{
public:
  /// Parameters for ClkFixedSoaType constructor
  struct ConstructorParams
  {
    std::string_view fqn;
    ClkTypeId type_id;
    size_t type_index;
    size_t size;
    size_t alignment;
    size_t element_type_index;
    size_t array_size;
    std::pmr::vector<FieldLayoutInfo> field_layouts;
    jewels::memory::ObjectPtr<ClkTypeFactory> factory;
  };

  /// Constructor, use from_proto to create an instance
  explicit ClkFixedSoaType(ConstructorParams params);

  ~ClkFixedSoaType() override = default;

  ClkFixedSoaType(const ClkFixedSoaType&) = delete;
  ClkFixedSoaType& operator=(const ClkFixedSoaType&) = delete;
  ClkFixedSoaType(ClkFixedSoaType&&) = delete;
  ClkFixedSoaType& operator=(const ClkFixedSoaType&&) = delete;

  /// Create an instance from a tachyon SoA type protobuf
  /// @param[in] factory Clockwork type factory
  /// @param[in] soa_proto Tachyon SoA type protobuf
  /// @param[in] type_index Type index
  /// @param[in] maybe_strong_type_fqn Optional fully qualified name of a strong type wrapping this schema
  /// @returns Clockwork type instance
  /// @throws runtime_error on failure.
  [[nodiscard]] static std::shared_ptr<ClkFixedSoaType> from_proto(
    jewels::memory::ObjectPtr<ClkTypeFactory> factory,
    const metadata::SoaType& soa_proto,
    size_t type_index,
    std::optional<std::string_view> maybe_strong_type_fqn);

  /// @return Element schema type
  [[nodiscard]] const ClkType& get_element_type() const;

  /// @return Element schema type
  [[nodiscard]] ClkType& get_element_type();

  /// @return Number of elements in the SoA
  [[nodiscard]] size_t get_array_size() const noexcept;

  /// @return Field layout information
  [[nodiscard]] const std::pmr::vector<FieldLayoutInfo>& get_field_layouts() const noexcept;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;

  /// @see ClkType::is_legacy_wire_compatible
  [[nodiscard]] bool is_legacy_wire_compatible(const ClkType& src_type) const override;

  /// @see ClkType::check_for_unexpected_schema_changes
  void check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name) override;

  /// @see ClkType::get_lowest_underlying_type
  [[nodiscard]] ClkType& get_lowest_underlying_type() override;

  /// @see ClkType::is_same_type
  [[nodiscard]] bool is_same_type(const ClkType& src_type) const override;

  /// @see ClkType::make_lite_compressor
  [[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> make_lite_compressor() override;

  /// @see ClkType::is_compressible
  [[nodiscard]] bool is_compressible() override;

private:
  /// Element schema type index
  size_t element_type_index_;

  /// SoA size (number of elements)
  size_t array_size_;

  /// Field layout information
  std::pmr::vector<FieldLayoutInfo> field_layouts_;

  /// Clockwork type factory
  jewels::memory::ObjectPtr<ClkTypeFactory> factory_;
};

/// Clockwork lite-compressor for VarSoa fields
class ClkVarSoaFieldLiteCompressor
{
public:
  /// Constructor
  /// @param[in] max_size Maximum SOA size
  /// @param[in] field_offset Field offset in the SOA
  /// @param[in] element_size Element size in bytes
  /// @param[in] compressor Element lite-compressor (nullptr if element is not compressible)
  ClkVarSoaFieldLiteCompressor(
    size_t max_size, size_t field_offset, size_t element_size, std::shared_ptr<ClkTypeLiteCompressor> compressor);

  /// Locate the chunks of zeros that can be replaced with a count in the lite compressed message
  /// @param[in] soa_size Number of elements in the SOA
  /// @param[in] soa_span Data span containing the SOA to compress
  /// @param[in] offset Message offset to the SOA
  /// @param[in,out] zero_chunks Storage for the zero chunks
  /// @return Success of failure
  jewels::BinaryOutcome compress(
    size_t soa_size,
    std::span<const std::byte> soa_span,
    size_t offset,
    jewels::InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const;

  /// Comparison operator for sorting field compressors by field offset so that the chunks of zeros
  /// are returned sorted by the offset into the schema
  [[nodiscard]] friend bool
  operator<(const ClkVarSoaFieldLiteCompressor& lhs, const ClkVarSoaFieldLiteCompressor& rhs) noexcept
  {
    return lhs.field_offset_ < rhs.field_offset_;
  }

private:
  /// Maximum SOA size
  size_t max_size_;

  /// Field offset in the SOA
  size_t field_offset_;

  /// Element size in bytes
  size_t element_size_;

  /// Array lite compressor
  ClkArrayLiteCompressor compressor_;
};

ClkVarSoaFieldLiteCompressor::ClkVarSoaFieldLiteCompressor(
  size_t max_size, size_t field_offset, size_t element_size, std::shared_ptr<ClkTypeLiteCompressor> compressor)
  : max_size_(max_size),
    field_offset_(field_offset),
    element_size_(element_size),
    compressor_(max_size, element_size, std::move(compressor))
{
}

jewels::BinaryOutcome ClkVarSoaFieldLiteCompressor::compress(
  size_t soa_size,
  std::span<const std::byte> soa_span,
  size_t offset,
  jewels::InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const
{
  if (!ok(compressor_.compress(
        soa_size,
        soa_span.subspan(field_offset_, max_size_ * element_size_),
        offset + field_offset_,
        InOut{*zero_chunks})))
  {
    return failure;
  }
  if (soa_size < max_size_)
  {
    zero_chunks->emplace_back(
      offset + field_offset_ + (soa_size * element_size_), (max_size_ - soa_size) * element_size_);
  }
  return success;
}

/// Clockwork lite-compressor for variable SOA types
class ClkVarSoaLiteCompressor : public ClkTypeLiteCompressor
{
public:
  /// Constructor
  /// @param[in] size_field_offset Size field offset (zero for FixedSoa)
  /// @param[in] size_field_length Size field offset (zero for FixedSoa)
  /// @param[in] compressors Field lite-compressor
  ClkVarSoaLiteCompressor(
    size_t size_field_offset, size_t size_field_length, std::pmr::vector<ClkVarSoaFieldLiteCompressor> compressors);

  ~ClkVarSoaLiteCompressor() noexcept override = default;

  ClkVarSoaLiteCompressor(const ClkVarSoaLiteCompressor&) = delete;
  ClkVarSoaLiteCompressor& operator=(const ClkVarSoaLiteCompressor&) = delete;
  ClkVarSoaLiteCompressor(ClkVarSoaLiteCompressor&&) = delete;
  ClkVarSoaLiteCompressor& operator=(ClkVarSoaLiteCompressor&&) = delete;

  /// @see ClkTypeLiteCompressor::compress
  jewels::BinaryOutcome compress(
    std::span<const std::byte> data_span,
    size_t offset,
    InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const override;

private:
  /// Size field offset
  size_t size_field_offset_;

  /// Size field length
  size_t size_field_length_;

  /// SOA field lite-compressors
  std::pmr::vector<ClkVarSoaFieldLiteCompressor> compressors_;
};

ClkVarSoaLiteCompressor::ClkVarSoaLiteCompressor(
  size_t size_field_offset, size_t size_field_length, std::pmr::vector<ClkVarSoaFieldLiteCompressor> compressors)
  : size_field_offset_(size_field_offset), size_field_length_(size_field_length), compressors_(std::move(compressors))
{
}

jewels::BinaryOutcome ClkVarSoaLiteCompressor::compress(
  std::span<const std::byte> data_span, size_t offset, InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const
{
  const auto soa_size = load_soa_size(data_span, size_field_offset_, size_field_length_);
  if (soa_size == 0U)
  {
    zero_chunks->emplace_back(offset, data_span.size());
    return success;
  }
  for (const auto& compressor : compressors_)
  {
    if (!ok(compressor.compress(soa_size, data_span, offset, InOut{*zero_chunks})))
    {
      return failure;
    }
  }
  return success;
}

/// Clockwork variable SoA (Struct-of-Arrays) type
class ClkVarSoaType : public ClkBuiltInType
{
public:
  /// Maximum total SoA size that can be upgraded with memcpy
  /// This is a heuristic to allow upgrading with memcpy when the
  /// size of the entire type is reasonably small.
  static constexpr size_t max_memcpy_size = 128U;

  /// Parameters for ClkVarSoaType constructor
  struct ConstructorParams
  {
    std::string_view fqn;
    ClkTypeId type_id;
    size_t type_index;
    size_t size;
    size_t alignment;
    size_t element_type_index;
    size_t array_size;
    std::pmr::vector<FieldLayoutInfo> field_layouts;
    size_t size_field_offset;
    size_t size_field_type_index;
    jewels::memory::ObjectPtr<ClkTypeFactory> factory;
  };

  /// Constructor, use from_proto to create an instance
  explicit ClkVarSoaType(ConstructorParams params);

  ~ClkVarSoaType() override = default;

  ClkVarSoaType(const ClkVarSoaType&) = delete;
  ClkVarSoaType& operator=(const ClkVarSoaType&) = delete;
  ClkVarSoaType(ClkVarSoaType&&) = delete;
  ClkVarSoaType& operator=(const ClkVarSoaType&&) = delete;

  /// Create an instance from a tachyon SoA type protobuf
  /// @param[in] factory Clockwork type factory
  /// @param[in] soa_proto Tachyon SoA type protobuf
  /// @param[in] type_index Type index
  /// @param[in] maybe_strong_type_fqn Optional fully qualified name of a strong type wrapping this schema
  /// @returns Clockwork type instance
  /// @throws runtime_error on failure.
  [[nodiscard]] static std::shared_ptr<ClkVarSoaType> from_proto(
    jewels::memory::ObjectPtr<ClkTypeFactory> factory,
    const metadata::SoaType& soa_proto,
    size_t type_index,
    std::optional<std::string_view> maybe_strong_type_fqn);

  /// @return Element schema type
  [[nodiscard]] const ClkType& get_element_type() const;

  /// @return Element schema type
  [[nodiscard]] ClkType& get_element_type();

  /// @return Maximum number of elements in the SoA
  [[nodiscard]] size_t get_array_size() const noexcept;

  /// @return Field layout information
  [[nodiscard]] const std::pmr::vector<FieldLayoutInfo>& get_field_layouts() const noexcept;

  /// @return Byte offset of the size field
  [[nodiscard]] size_t get_size_field_offset() const noexcept;

  /// @return Length in bytes  of the size field
  [[nodiscard]] size_t get_size_field_length() const noexcept;

  /// @return Type of the size field
  [[nodiscard]] const ClkType& get_size_field_type() const;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;

  /// @see ClkType::is_legacy_wire_compatible
  [[nodiscard]] bool is_legacy_wire_compatible(const ClkType& src_type) const override;

  /// @see ClkType::check_for_unexpected_schema_changes
  void check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name) override;

  /// @see ClkType::get_lowest_underlying_type
  [[nodiscard]] ClkType& get_lowest_underlying_type() override;

  /// @see ClkType::is_same_type
  [[nodiscard]] bool is_same_type(const ClkType& src_type) const override;

  /// @see ClkType::make_lite_compressor
  [[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> make_lite_compressor() override;

  /// @see ClkType::is_compressible
  [[nodiscard]] bool is_compressible() override;

private:
  /// Element schema type index
  size_t element_type_index_;

  /// SoA maximum size
  size_t array_size_;

  /// Field layout information
  std::pmr::vector<FieldLayoutInfo> field_layouts_;

  /// Byte offset of the size field
  size_t size_field_offset_;

  /// Type index of the size field
  size_t size_field_type_index_;

  /// Clockwork type factory
  jewels::memory::ObjectPtr<ClkTypeFactory> factory_;
};

// NOLINTNEXTLINE(readability-function-size) TODO(OI-3646)
ClkFixedArrayType::ClkFixedArrayType(
  std::string_view fqn,
  ClkTypeId type_id,
  size_t type_index,
  int32_t metadata_version,
  size_t size,
  size_t alignment,
  size_t element_type_index,
  size_t array_size,
  jewels::memory::ObjectPtr<ClkTypeFactory> factory)
  : ClkBuiltInType(factory->get_memory_resource(), fqn, type_id, type_index, metadata_version, size, alignment),
    element_type_index_(element_type_index),
    array_size_(array_size),
    factory_(factory)
{
}

// NOLINTNEXTLINE(misc-no-recursion) Types are defined recursively
[[nodiscard]] std::shared_ptr<ClkFixedArrayType> ClkFixedArrayType::from_proto(
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
  return jewels::memory::make_pmr_shared<ClkFixedArrayType>(
    factory->get_memory_resource(),
    maybe_strong_type_fqn.value_or(builtin_proto.fqn()),
    ClkTypeId::fixed_array,
    type_index,
    factory->get_metadata_version(),
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
           jewels::memory::make_pmr_shared<ClkFixedArrayFromElementUpgrader>(
             get_memory_resource(), std::move(array_upgrader)))
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
             jewels::memory::make_pmr_shared<ClkFixedArrayFromFixedArrayUpgrader>(
               get_memory_resource(), src_array_type.get_array_size(), std::move(array_upgrader)))
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
             jewels::memory::make_pmr_shared<ClkFixedArrayFromVarArrayUpgrader>(
               get_memory_resource(),
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
             jewels::memory::make_pmr_shared<ClkFixedArrayFromVarStringUpgrader>(
               get_memory_resource(), array_size_, var_array_size_offset(src_string_type.get_string_size(), 1U)))
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
             jewels::memory::make_pmr_shared<ClkFixedArrayFromOptionalUpgrader>(
               get_memory_resource(),
               optional_has_value_offset(src_optional_type.get_element_type().get_size()),
               std::move(array_upgrader)))
           .first->second);
    }
    if (src_type.get_type_id() == ClkTypeId::fixed_soa)
    {
      const auto& src_soa_type = dynamic_cast<const ClkFixedSoaType&>(src_type);
      if (src_soa_type.get_array_size() != array_size_)
      {
        throw ClkTypeUpgradeError(
          fmt::format(
            "Cannot convert FixedSoa with size {} to FixedArray with size {}",
            src_soa_type.get_array_size(),
            array_size_));
      }
      return make_soa_to_array_upgrader(
        MakeSoaToArrayUpgraderParams{
          .memory_resource = get_memory_resource(),
          .upgrader_cache = upgrader_cache,
          .src_type = src_type,
          .src_schema = dynamic_cast<const ClkSchemaType&>(src_soa_type.get_element_type()),
          .dest_schema = dynamic_cast<const ClkSchemaType&>(get_element_type()),
          .src_field_layouts = src_soa_type.get_field_layouts(),
          .src_size_field_offset = 0U,
          .src_size_field_length = 0U,
          .src_fixed_array_size = src_soa_type.get_array_size(),
          .dest_element_size = get_element_type().get_size(),
          .dest_size_field_offset = 0U,
          .dest_array_size = array_size_});
    }
    if (src_type.get_type_id() == ClkTypeId::var_soa)
    {
      const auto& src_soa_type = dynamic_cast<const ClkVarSoaType&>(src_type);
      return make_soa_to_array_upgrader(
        MakeSoaToArrayUpgraderParams{
          .memory_resource = get_memory_resource(),
          .upgrader_cache = upgrader_cache,
          .src_type = src_type,
          .src_schema = dynamic_cast<const ClkSchemaType&>(src_soa_type.get_element_type()),
          .dest_schema = dynamic_cast<const ClkSchemaType&>(get_element_type()),
          .src_field_layouts = src_soa_type.get_field_layouts(),
          .src_size_field_offset = src_soa_type.get_size_field_offset(),
          .src_size_field_length = src_soa_type.get_size_field_length(),
          .src_fixed_array_size = 0U,
          .dest_element_size = get_element_type().get_size(),
          .dest_size_field_offset = 0U,
          .dest_array_size = array_size_});
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

void ClkFixedArrayType::check_for_unexpected_schema_changes(
  ClkType& src_type, bool allow_changes, std::string_view name)
{
  if (allow_changes)
  {
    check_for_unexpected_underlying_schema_changes(src_type, name);
  }
  else
  {
    ClkBuiltInType::check_for_unexpected_schema_changes(src_type, false, name);
    auto& src_array_type = dynamic_cast<ClkFixedArrayType&>(src_type);
    if (src_array_type.array_size_ != array_size_)
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Unexpected array size change from {} to {} for {}", src_array_type.array_size_, array_size_, name));
    }
    get_element_type().check_for_unexpected_schema_changes(src_array_type.get_element_type(), false, name);
  }
}

[[nodiscard]] ClkType& ClkFixedArrayType::get_lowest_underlying_type()
{
  return get_element_type().get_lowest_underlying_type();
}

[[nodiscard]] bool ClkFixedArrayType::is_same_type(const ClkType& src_type) const
{
  if (src_type.get_type_id() != ClkTypeId::fixed_array)
  {
    return false;
  }
  const auto& src_array_type = dynamic_cast<const ClkFixedArrayType&>(src_type);
  if (src_array_type.get_array_size() != get_array_size())
  {
    return false;
  }
  return get_element_type().is_same_type(src_array_type.get_element_type());
}

[[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> ClkFixedArrayType::make_lite_compressor()
{
  auto& cached_lite_compressor = get_cached_lite_compressor();
  if (!cached_lite_compressor)
  {
    std::shared_ptr<ClkTypeLiteCompressor> element_compressor;
    if (get_element_type().is_compressible())
    {
      element_compressor = get_element_type().make_lite_compressor();
    }
    cached_lite_compressor = jewels::memory::make_pmr_shared<ClkFixedArrayLiteCompressor>(
      get_memory_resource(), get_array_size(), get_element_type().get_size(), std::move(element_compressor));
  }
  return jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor>{cached_lite_compressor};
}

[[nodiscard]] bool ClkFixedArrayType::is_compressible()
{
  auto& cached_is_compressible = get_cached_is_compressible();
  if (!cached_is_compressible.has_value())
  {
    cached_is_compressible = true;
  }
  return cached_is_compressible.value();
}

// NOLINTNEXTLINE(readability-function-size) TODO(OI-3646)
ClkVarArrayType::ClkVarArrayType(
  std::string_view fqn,
  ClkTypeId type_id,
  size_t type_index,
  int32_t metadata_version,
  size_t size,
  size_t alignment,
  size_t element_type_index,
  size_t array_size,
  jewels::memory::ObjectPtr<ClkTypeFactory> factory)
  : ClkBuiltInType(factory->get_memory_resource(), fqn, type_id, type_index, metadata_version, size, alignment),
    element_type_index_(element_type_index),
    array_size_(array_size),
    factory_(factory)
{
}

// NOLINTNEXTLINE(misc-no-recursion) Types are defined recursively
[[nodiscard]] std::shared_ptr<ClkVarArrayType> ClkVarArrayType::from_proto(
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
  return jewels::memory::make_pmr_shared<ClkVarArrayType>(
    factory->get_memory_resource(),
    maybe_strong_type_fqn.value_or(builtin_proto.fqn()),
    ClkTypeId::var_array,
    type_index,
    factory->get_metadata_version(),
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
           jewels::memory::make_pmr_shared<ClkVarArrayFromElementUpgrader>(
             get_memory_resource(),
             var_array_size_offset(array_size_, get_element_type().get_size()),
             std::move(array_upgrader)))
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
             jewels::memory::make_pmr_shared<ClkVarArrayFromVarArrayUpgrader>(
               get_memory_resource(),
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
             jewels::memory::make_pmr_shared<ClkVarArrayFromFixedArrayUpgrader>(
               get_memory_resource(),
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
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_pmr_shared<ClkVarArrayFromVarStringUpgrader>(
               get_memory_resource(),
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
             jewels::memory::make_pmr_shared<ClkVarArrayFromOptionalUpgrader>(
               get_memory_resource(),
               optional_has_value_offset(src_optional_type.get_element_type().get_size()),
               var_array_size_offset(array_size_, get_element_type().get_size()),
               std::move(array_upgrader)))
           .first->second);
    }
    if (src_type.get_type_id() == ClkTypeId::fixed_soa)
    {
      const auto& src_soa_type = dynamic_cast<const ClkFixedSoaType&>(src_type);
      return make_soa_to_array_upgrader(
        MakeSoaToArrayUpgraderParams{
          .memory_resource = get_memory_resource(),
          .upgrader_cache = upgrader_cache,
          .src_type = src_type,
          .src_schema = dynamic_cast<const ClkSchemaType&>(src_soa_type.get_element_type()),
          .dest_schema = dynamic_cast<const ClkSchemaType&>(get_element_type()),
          .src_field_layouts = src_soa_type.get_field_layouts(),
          .src_size_field_offset = 0U,
          .src_size_field_length = 0U,
          .src_fixed_array_size = src_soa_type.get_array_size(),
          .dest_element_size = get_element_type().get_size(),
          .dest_size_field_offset = var_array_size_offset(array_size_, get_element_type().get_size()),
          .dest_array_size = array_size_});
    }
    if (src_type.get_type_id() == ClkTypeId::var_soa)
    {
      const auto& src_soa_type = dynamic_cast<const ClkVarSoaType&>(src_type);
      return make_soa_to_array_upgrader(
        MakeSoaToArrayUpgraderParams{
          .memory_resource = get_memory_resource(),
          .upgrader_cache = upgrader_cache,
          .src_type = src_type,
          .src_schema = dynamic_cast<const ClkSchemaType&>(src_soa_type.get_element_type()),
          .dest_schema = dynamic_cast<const ClkSchemaType&>(get_element_type()),
          .src_field_layouts = src_soa_type.get_field_layouts(),
          .src_size_field_offset = src_soa_type.get_size_field_offset(),
          .src_size_field_length = src_soa_type.get_size_field_length(),
          .src_fixed_array_size = 0U,
          .dest_element_size = get_element_type().get_size(),
          .dest_size_field_offset = var_array_size_offset(array_size_, get_element_type().get_size()),
          .dest_array_size = array_size_});
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

void ClkVarArrayType::check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name)
{
  if (allow_changes)
  {
    check_for_unexpected_underlying_schema_changes(src_type, name);
  }
  else
  {
    ClkBuiltInType::check_for_unexpected_schema_changes(src_type, false, name);
    auto& src_array_type = dynamic_cast<ClkVarArrayType&>(src_type);
    if (src_array_type.array_size_ != array_size_)
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Unexpected array size change from {} to {} for {}", src_array_type.array_size_, array_size_, name));
    }
    get_element_type().check_for_unexpected_schema_changes(src_array_type.get_element_type(), false, name);
  }
}

[[nodiscard]] ClkType& ClkVarArrayType::get_lowest_underlying_type()
{
  return get_element_type().get_lowest_underlying_type();
}

[[nodiscard]] bool ClkVarArrayType::is_same_type(const ClkType& src_type) const
{
  if (src_type.get_type_id() != ClkTypeId::var_array)
  {
    return false;
  }
  const auto& src_array_type = dynamic_cast<const ClkVarArrayType&>(src_type);
  if (src_array_type.get_array_size() != get_array_size())
  {
    return false;
  }
  return get_element_type().is_same_type(src_array_type.get_element_type());
}

[[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> ClkVarArrayType::make_lite_compressor()
{
  auto& cached_lite_compressor = get_cached_lite_compressor();
  if (!cached_lite_compressor)
  {
    std::shared_ptr<ClkTypeLiteCompressor> element_compressor;
    if (get_element_type().is_compressible())
    {
      element_compressor = get_element_type().make_lite_compressor();
    }
    cached_lite_compressor = jewels::memory::make_pmr_shared<ClkVarArrayLiteCompressor>(
      get_memory_resource(),
      get_array_size(),
      var_array_size_offset(get_array_size(), get_element_type().get_size()),
      get_element_type().get_size(),
      std::move(element_compressor));
  }
  return jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor>{cached_lite_compressor};
}

[[nodiscard]] bool ClkVarArrayType::is_compressible()
{
  auto& cached_is_compressible = get_cached_is_compressible();
  if (!cached_is_compressible.has_value())
  {
    cached_is_compressible = true;
  }
  return cached_is_compressible.value();
}

/// Clockwork upgrader to a tensor from a fixed array
class ClkTensorFromFixedArrayUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] src_array_size Source array size
  /// @param[in] array_upgrader Array upgrader
  ClkTensorFromFixedArrayUpgrader(size_t src_array_size, std::shared_ptr<ClkArrayUpgrader> array_upgrader);

  ~ClkTensorFromFixedArrayUpgrader() noexcept override = default;

  ClkTensorFromFixedArrayUpgrader(const ClkTensorFromFixedArrayUpgrader&) = delete;
  ClkTensorFromFixedArrayUpgrader& operator=(const ClkTensorFromFixedArrayUpgrader&) = delete;
  ClkTensorFromFixedArrayUpgrader(ClkTensorFromFixedArrayUpgrader&&) = delete;
  ClkTensorFromFixedArrayUpgrader& operator=(ClkTensorFromFixedArrayUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source array size
  uint64_t src_array_size_;

  /// Array upgrader
  std::shared_ptr<ClkArrayUpgrader> array_upgrader_;
};

ClkTensorFromFixedArrayUpgrader::ClkTensorFromFixedArrayUpgrader(
  size_t src_array_size, std::shared_ptr<ClkArrayUpgrader> array_upgrader)
  : src_array_size_(src_array_size), array_upgrader_(std::move(array_upgrader))
{
}

void ClkTensorFromFixedArrayUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  array_upgrader_->upgrade(src_array_size_, src_span, dest_span);
}

// NOLINTNEXTLINE(readability-function-size) TODO(OI-3646)
ClkTensorType::ClkTensorType(
  std::string_view fqn,
  ClkTypeId type_id,
  size_t type_index,
  int32_t metadata_version,
  size_t size,
  size_t alignment,
  size_t element_type_index,
  const std::pmr::vector<size_t>& shape,
  std::pmr::string layout,
  jewels::memory::ObjectPtr<ClkTypeFactory> factory)
  : ClkBuiltInType(factory->get_memory_resource(), fqn, type_id, type_index, metadata_version, size, alignment),
    element_type_index_(element_type_index),
    shape_(shape),
    layout_(std::move(layout)),
    factory_(factory)
{
}

// NOLINTNEXTLINE(misc-no-recursion) Types are defined recursively
[[nodiscard]] std::shared_ptr<ClkTensorType> ClkTensorType::from_proto(
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  const metadata::BuiltInType& builtin_proto,
  size_t type_index,
  std::optional<std::string_view> maybe_strong_type_fqn)
{
  if (builtin_proto.arguments_size() != 3)
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Invalid protobuf for {} type, got {} arguments, expected 3",
        builtin_proto.fqn(),
        builtin_proto.arguments_size()));
  }
  const auto element_type_index = static_cast<size_t>(builtin_proto.arguments(0).type_id());
  std::ignore = factory->get_clk_type(element_type_index); // Ensure that the element type is populated

  const std::regex shape_pat{"\\[([0-9]+(,[0-9]+)*)\\]"};
  const auto& shape_str = builtin_proto.arguments(1).value();
  std::smatch match;
  if (!std::regex_match(shape_str, match, shape_pat))
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Invalid protobuf for {} type, got {} arguments, expected 3",
        builtin_proto.fqn(),
        builtin_proto.arguments_size()));
  }

  std::pmr::vector<size_t> dimensions(factory->get_memory_resource());
  const auto& inner = std::string_view{&*match[1].first, static_cast<size_t>(match[1].length())};
  for (const auto& dim_str : inner | std::views::split(','))
  {
    const auto len = static_cast<size_t>(std::ranges::distance(dim_str));
    const auto dim_sv = std::string_view{&*dim_str.begin(), static_cast<size_t>(len)};
    size_t dim{};
    auto result = std::from_chars(dim_sv.begin(), dim_sv.end(), dim);
    if (result.ec != std::errc{})
    {
      throw ClkTypeUpgradeError(fmt::format("Malformed tensor dimension for {}: {}", builtin_proto.fqn(), dim_sv));
    }
    dimensions.push_back(dim);
  }
  const auto& layout = builtin_proto.arguments(2).value();

  return jewels::memory::make_pmr_shared<ClkTensorType>(
    factory->get_memory_resource(),
    maybe_strong_type_fqn.value_or(builtin_proto.fqn()),
    ClkTypeId::tensor,
    type_index,
    factory->get_metadata_version(),
    static_cast<size_t>(builtin_proto.size()),
    static_cast<size_t>(builtin_proto.alignment()),
    element_type_index,
    dimensions,
    std::pmr::string{layout, factory->get_memory_resource()},
    factory);
}

[[nodiscard]] const ClkType& ClkTensorType::get_element_type() const
{
  return *factory_->get_types().at(element_type_index_);
}

[[nodiscard]] ClkType& ClkTensorType::get_element_type()
{
  return *factory_->get_types().at(element_type_index_);
}

[[nodiscard]] const std::pmr::vector<size_t>& ClkTensorType::get_shape() const noexcept
{
  return shape_;
}

[[nodiscard]] size_t ClkTensorType::num_elements() const noexcept
{
  return std::reduce(shape_.begin(), shape_.end(), size_t{1}, std::multiplies<>());
}

[[nodiscard]] std::string_view ClkTensorType::get_layout() const noexcept
{
  return layout_;
}

[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> ClkTensorType::make_upgrader(const ClkType& src_type)
{
  auto& upgrader_cache = get_upgrader_cache();
  if (const auto cache_iter = upgrader_cache.find(src_type.get_type_index()); cache_iter != upgrader_cache.end())
  {
    return jewels::memory::make_non_null_from_ref(*cache_iter->second);
  }

  if (src_type.get_type_id() == ClkTypeId::fixed_array)
  {
    const auto& src_array_type = dynamic_cast<const ClkFixedArrayType&>(src_type);
    if (
      src_array_type.get_array_size() == num_elements() &&
      get_element_type().is_same_type(src_array_type.get_element_type()))
    {
      auto array_upgrader =
        get_element_type().make_array_upgrader(src_array_type.get_element_type(), num_elements(), num_elements());
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_pmr_shared<ClkTensorFromFixedArrayUpgrader>(
               get_memory_resource(), num_elements(), std::move(array_upgrader)))
           .first->second);
    }
  }

  if (src_type.get_type_id() == ClkTypeId::tensor)
  {
    const auto& src_tensor = dynamic_cast<const ClkTensorType&>(src_type);
    if (src_tensor.get_shape() == get_shape() && src_tensor.get_layout() == get_layout())
    {
      // TODO(OI-4451): we can support shape changes if the product of the
      // shapes are same. We can also support memory layout changes most (if not
      // all) of the time. But we need a helper type like the inconing std::mdspan
      // backport to make these transormations easier to write.
      auto array_upgrader =
        get_element_type().make_array_upgrader(src_tensor.get_element_type(), num_elements(), num_elements());
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_pmr_shared<ClkTensorFromFixedArrayUpgrader>(
               get_memory_resource(), num_elements(), std::move(array_upgrader)))
           .first->second);
    }
  }

  throw ClkTypeUpgradeError(
    fmt::format(
      "Cannot upgrade from {} ({}) to {} ({})", src_type.get_fqn(), src_type.get_type_id(), get_fqn(), get_type_id()));
}

[[nodiscard]] bool ClkTensorType::use_memcpy_for_array_upgrade(const ClkType& src_type)
{
  return is_legacy_wire_compatible(src_type);
}

[[nodiscard]] bool ClkTensorType::is_legacy_wire_compatible(const ClkType& src_type) const
{
  if (!ClkBuiltInType::is_legacy_wire_compatible(src_type))
  {
    return false;
  }

  if (src_type.get_type_id() == ClkTypeId::fixed_array)
  {
    const auto& src_array = dynamic_cast<const ClkFixedArrayType&>(src_type);
    return get_element_type().is_same_type(src_array.get_element_type()) &&
           num_elements() == src_array.get_array_size();
  }

  return is_same_type(src_type);
}

void ClkTensorType::check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name)
{
  if (allow_changes)
  {
    check_for_unexpected_underlying_schema_changes(src_type, name);
    return;
  }

  ClkBuiltInType::check_for_unexpected_schema_changes(src_type, false, name);
  auto& src_tensor = dynamic_cast<ClkTensorType&>(src_type);
  if (src_tensor.get_shape() != get_shape() || src_tensor.get_layout() != get_layout())
  {
    // TODO(OI-4451): we can support shape changes if the product of the
    // shapes are same. We can also support memory layout changes most (if not
    // all) of the time. But we need a helper type like the inconing std::mdspan
    // backport to make these transormations easier to write.
    throw ClkTypeUpgradeError(
      fmt::format("Unexpected change from {} to {} for {}", src_type.get_fqn(), get_fqn(), name));
  }
  get_element_type().check_for_unexpected_schema_changes(src_tensor.get_element_type(), false, name);
}

[[nodiscard]] ClkType& ClkTensorType::get_lowest_underlying_type()
{
  return get_element_type().get_lowest_underlying_type();
}

[[nodiscard]] bool ClkTensorType::is_same_type(const ClkType& src_type) const
{
  if (src_type.get_type_id() != ClkTypeId::tensor)
  {
    return false;
  }
  const auto& src_tensor = dynamic_cast<const ClkTensorType&>(src_type);
  return src_tensor.get_shape() == get_shape() && src_tensor.get_layout() == get_layout() &&
         get_element_type().is_same_type(src_tensor.get_element_type());
}

[[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> ClkTensorType::make_lite_compressor()
{
  auto& cached_lite_compressor = get_cached_lite_compressor();
  if (!cached_lite_compressor)
  {
    std::shared_ptr<ClkTypeLiteCompressor> element_compressor;
    if (get_element_type().is_compressible())
    {
      element_compressor = get_element_type().make_lite_compressor();
    }
    cached_lite_compressor = jewels::memory::make_pmr_shared<ClkFixedArrayLiteCompressor>(
      get_memory_resource(), num_elements(), get_element_type().get_size(), std::move(element_compressor));
  }
  return jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor>{cached_lite_compressor};
}

[[nodiscard]] bool ClkTensorType::is_compressible()
{
  auto& cached_is_compressible = get_cached_is_compressible();
  if (!cached_is_compressible.has_value())
  {
    cached_is_compressible = true;
  }
  return cached_is_compressible.value();
}

ClkVarStringType::ClkVarStringType(const ClkVarStringTypeParams& params)
  : ClkBuiltInType(
      params.memory_resource,
      params.fqn,
      params.type_id,
      params.type_index,
      params.metadata_version,
      params.size,
      params.alignment),
    string_size_(params.string_size)
{
}

[[nodiscard]] std::shared_ptr<ClkVarStringType> ClkVarStringType::from_proto(
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  const metadata::BuiltInType& builtin_proto,
  size_t type_index,
  std::optional<std::string_view> maybe_strong_type_fqn)
{
  if (builtin_proto.arguments_size() != 1)
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Invalid protobuf for {} type, got {} arguments, expected 2",
        builtin_proto.fqn(),
        builtin_proto.arguments_size()));
  }
  return jewels::memory::make_pmr_shared<ClkVarStringType>(
    factory->get_memory_resource(),
    ClkVarStringTypeParams{
      .memory_resource = factory->get_memory_resource(),
      .fqn = maybe_strong_type_fqn.value_or(builtin_proto.fqn()),
      .type_id = ClkTypeId::var_string,
      .type_index = type_index,
      .metadata_version = factory->get_metadata_version(),
      .size = static_cast<size_t>(builtin_proto.size()),
      .alignment = static_cast<size_t>(builtin_proto.alignment()),
      .string_size = parse_size_parameter(builtin_proto.fqn(), builtin_proto.arguments(0).value()),
    });
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
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkVarStringFromVarStringUpgrader>(
             get_memory_resource(),
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
             jewels::memory::make_pmr_shared<ClkVarStringFromFixedArrayUpgrader>(
               get_memory_resource(),
               get_string_size() - 1U,
               src_array_type.get_array_size(),
               var_array_size_offset(get_string_size(), 1U)))
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
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_pmr_shared<ClkVarStringFromVarStringUpgrader>(
               get_memory_resource(),
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
             jewels::memory::make_pmr_shared<ClkVarStringFromOptionalUpgrader>(
               get_memory_resource(), optional_has_value_offset(1U), var_array_size_offset(get_string_size(), 1U)))
           .first->second);
    }
  }
  if (src_type.get_type_id() == ClkTypeId::uuid)
  {
    constexpr size_t uuid_string_length = 36U;
    // VarString<max_size=N> reserves one byte for the null terminator, so it holds at most N-1 characters.
    // The minimum max_size must therefore be uuid_string_length + 1.
    constexpr size_t min_varstring_max_size = uuid_string_length + 1U;
    if (get_string_size() < min_varstring_max_size)
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "VarString max_size must be at least {} to hold a UUID string, got {}",
          min_varstring_max_size,
          get_string_size()));
    }
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkVarStringFromUuidUpgrader>(
             get_memory_resource(), var_array_size_offset(get_string_size(), 1U)))
         .first->second);
  }
  if (
    src_type.get_type_id() == ClkTypeId::byte || src_type.get_type_id() == ClkTypeId::int8 ||
    src_type.get_type_id() == ClkTypeId::uint8)
  {
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkVarStringFromFixedArrayUpgrader>(
             get_memory_resource(), get_string_size() - 1U, 1U, var_array_size_offset(get_string_size(), 1U)))
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

void ClkVarStringType::check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name)
{
  if (!allow_changes)
  {
    ClkBuiltInType::check_for_unexpected_schema_changes(src_type, false, name);
    const auto& src_string_type = dynamic_cast<const ClkVarStringType&>(src_type);
    if (src_string_type.string_size_ != string_size_)
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Unexpected string size change from {} to {} for {}", src_string_type.string_size_, string_size_, name));
    }
  }
}

[[nodiscard]] bool ClkVarStringType::is_same_type(const ClkType& src_type) const
{
  if (src_type.get_type_id() != ClkTypeId::var_string)
  {
    return false;
  }
  const auto& src_string_type = dynamic_cast<const ClkVarStringType&>(src_type);
  return src_string_type.get_string_size() == get_string_size();
}

[[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> ClkVarStringType::make_lite_compressor()
{
  auto& cached_lite_compressor = get_cached_lite_compressor();
  if (!cached_lite_compressor)
  {
    cached_lite_compressor = jewels::memory::make_pmr_shared<ClkVarArrayLiteCompressor>(
      get_memory_resource(), get_string_size(), var_array_size_offset(get_string_size(), 1U), 1U, nullptr);
  }
  return jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor>{cached_lite_compressor};
}

[[nodiscard]] bool ClkVarStringType::is_compressible()
{
  auto& cached_is_compressible = get_cached_is_compressible();
  if (!cached_is_compressible.has_value())
  {
    cached_is_compressible = true;
  }
  return cached_is_compressible.value();
}

// NOLINTNEXTLINE(readability-function-size) Parameters are only used for private constructor.
ClkOptionalType::ClkOptionalType(
  std::string_view fqn,
  ClkTypeId type_id,
  size_t type_index,
  int32_t metadata_version,
  size_t size,
  size_t alignment,
  size_t element_type_index,
  jewels::memory::ObjectPtr<ClkTypeFactory> factory)
  : ClkBuiltInType(factory->get_memory_resource(), fqn, type_id, type_index, metadata_version, size, alignment),
    element_type_index_(element_type_index),
    factory_(factory)
{
}

// NOLINTNEXTLINE(misc-no-recursion) Types are defined recursively
[[nodiscard]] std::shared_ptr<ClkOptionalType> ClkOptionalType::from_proto(
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
  return jewels::memory::make_pmr_shared<ClkOptionalType>(
    factory->get_memory_resource(),
    maybe_strong_type_fqn.value_or(builtin_proto.fqn()),
    ClkTypeId::optional,
    type_index,
    factory->get_metadata_version(),
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
           jewels::memory::make_pmr_shared<ClkOptionalFromFixedArrayUpgrader>(
             get_memory_resource(), 1U, optional_has_value_offset(get_element_type().get_size()), element_upgrader))
         .first->second);
  }
  catch (const ClkTypeUpgradeError& /*exc*/)
  {
    if (src_type.get_type_id() == ClkTypeId::fixed_array)
    {
      const auto& src_array_type = dynamic_cast<const ClkFixedArrayType&>(src_type);
      const auto element_upgrader = get_element_type().make_upgrader(src_array_type.get_element_type());
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_pmr_shared<ClkOptionalFromFixedArrayUpgrader>(
               get_memory_resource(),
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
             jewels::memory::make_pmr_shared<ClkOptionalFromVarArrayUpgrader>(
               get_memory_resource(),
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
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache
           .emplace(
             src_type.get_type_index(),
             jewels::memory::make_pmr_shared<ClkOptionalFromVarStringUpgrader>(
               get_memory_resource(),
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
             jewels::memory::make_pmr_shared<ClkOptionalFromOptionalUpgrader>(
               get_memory_resource(),
               optional_has_value_offset(src_optional_type.get_element_type().get_size()),
               optional_has_value_offset(get_element_type().get_size()),
               element_upgrader))
           .first->second);
    }
    if (src_type.get_type_id() == ClkTypeId::fixed_soa)
    {
      const auto& src_soa_type = dynamic_cast<const ClkFixedSoaType&>(src_type);
      return make_soa_to_optional_upgrader(
        MakeSoaToOptionalUpgraderParams{
          .memory_resource = get_memory_resource(),
          .upgrader_cache = upgrader_cache,
          .src_type = src_type,
          .src_schema = dynamic_cast<const ClkSchemaType&>(src_soa_type.get_element_type()),
          .dest_schema = dynamic_cast<const ClkSchemaType&>(get_element_type()),
          .src_field_layouts = src_soa_type.get_field_layouts(),
          .src_size_field_offset = 0U,
          .src_size_field_length = 0U,
          .src_fixed_array_size = src_soa_type.get_array_size(),
          .dest_element_size = get_element_type().get_size(),
          .dest_has_value_offset = optional_has_value_offset(get_element_type().get_size())});
    }
    if (src_type.get_type_id() == ClkTypeId::var_soa)
    {
      const auto& src_soa_type = dynamic_cast<const ClkVarSoaType&>(src_type);
      return make_soa_to_optional_upgrader(
        MakeSoaToOptionalUpgraderParams{
          .memory_resource = get_memory_resource(),
          .upgrader_cache = upgrader_cache,
          .src_type = src_type,
          .src_schema = dynamic_cast<const ClkSchemaType&>(src_soa_type.get_element_type()),
          .dest_schema = dynamic_cast<const ClkSchemaType&>(get_element_type()),
          .src_field_layouts = src_soa_type.get_field_layouts(),
          .src_size_field_offset = src_soa_type.get_size_field_offset(),
          .src_size_field_length = src_soa_type.get_size_field_length(),
          .src_fixed_array_size = 0U,
          .dest_element_size = get_element_type().get_size(),
          .dest_has_value_offset = optional_has_value_offset(get_element_type().get_size())});
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

void ClkOptionalType::check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name)
{
  if (allow_changes)
  {
    check_for_unexpected_underlying_schema_changes(src_type, name);
  }
  else
  {
    ClkBuiltInType::check_for_unexpected_schema_changes(src_type, false, name);
    auto& src_optional_type = dynamic_cast<ClkOptionalType&>(src_type);
    get_element_type().check_for_unexpected_schema_changes(src_optional_type.get_element_type(), false, name);
  }
}

[[nodiscard]] ClkType& ClkOptionalType::get_lowest_underlying_type()
{
  return get_element_type().get_lowest_underlying_type();
}

[[nodiscard]] bool ClkOptionalType::is_same_type(const ClkType& src_type) const
{
  if (src_type.get_type_id() != ClkTypeId::optional)
  {
    return false;
  }
  const auto& src_optional_type = dynamic_cast<const ClkOptionalType&>(src_type);
  return get_element_type().is_same_type(src_optional_type.get_element_type());
}

[[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> ClkOptionalType::make_lite_compressor()
{
  auto& cached_lite_compressor = get_cached_lite_compressor();
  if (!cached_lite_compressor)
  {
    std::shared_ptr<ClkTypeLiteCompressor> element_compressor;
    if (get_element_type().is_compressible())
    {
      element_compressor = get_element_type().make_lite_compressor();
    }
    cached_lite_compressor = jewels::memory::make_pmr_shared<ClkOptionalLiteCompressor>(
      get_memory_resource(), get_element_type().get_size(), std::move(element_compressor));
  }
  return jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor>{cached_lite_compressor};
}

[[nodiscard]] bool ClkOptionalType::is_compressible()
{
  auto cached_is_compressible = get_cached_is_compressible();
  if (!cached_is_compressible.has_value())
  {
    cached_is_compressible = true;
  }
  return cached_is_compressible.value();
}

ClkFixedSoaType::ClkFixedSoaType(ConstructorParams params)
  : ClkBuiltInType(
      params.factory->get_memory_resource(),
      params.fqn,
      params.type_id,
      params.type_index,
      params.factory->get_metadata_version(),
      params.size,
      params.alignment),
    element_type_index_(params.element_type_index),
    array_size_(params.array_size),
    field_layouts_(std::move(params.field_layouts)),
    factory_(params.factory)
{
}

// NOLINTNEXTLINE(misc-no-recursion) Types are defined recursively
[[nodiscard]] std::shared_ptr<ClkFixedSoaType> ClkFixedSoaType::from_proto(
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  const metadata::SoaType& soa_proto,
  size_t type_index,
  std::optional<std::string_view> maybe_strong_type_fqn)
{
  // Ensure that the element schema type is populated in the factory cache
  const auto element_type_index = static_cast<size_t>(soa_proto.schema_type_id());
  // std::ignore: We only need to trigger type loading; the pointer will be fetched later
  std::ignore = factory->get_clk_type(element_type_index);

  // Convert field_layouts from protobuf to FieldLayoutInfo
  std::pmr::vector<FieldLayoutInfo> field_layouts;
  field_layouts.reserve(static_cast<size_t>(soa_proto.field_layouts_size()));
  for (const auto& field_proto : soa_proto.field_layouts())
  {
    // Get the field type to extract size and alignment
    const auto& field_type = *factory->get_clk_type(static_cast<size_t>(field_proto.type_id()));
    field_layouts.push_back(
      FieldLayoutInfo{
        .field_num = field_proto.num(),
        .size = field_type.get_size(),
        .alignment = field_type.get_alignment(),
        .offset = static_cast<size_t>(field_proto.offset()),
      });
  }

  return jewels::memory::make_pmr_shared<ClkFixedSoaType>(
    factory->get_memory_resource(),
    ClkFixedSoaType::ConstructorParams{
      .fqn = maybe_strong_type_fqn.value_or(soa_proto.fqn()),
      .type_id = ClkTypeId::fixed_soa,
      .type_index = type_index,
      .size = static_cast<size_t>(soa_proto.size()),
      .alignment = static_cast<size_t>(soa_proto.alignment()),
      .element_type_index = element_type_index,
      .array_size = static_cast<size_t>(soa_proto.container_size()),
      .field_layouts = std::move(field_layouts),
      .factory = factory,
    });
}

[[nodiscard]] const ClkType& ClkFixedSoaType::get_element_type() const
{
  return *factory_->get_types().at(element_type_index_);
}

[[nodiscard]] ClkType& ClkFixedSoaType::get_element_type()
{
  return *factory_->get_types().at(element_type_index_);
}

[[nodiscard]] size_t ClkFixedSoaType::get_array_size() const noexcept
{
  return array_size_;
}

[[nodiscard]] const std::pmr::vector<FieldLayoutInfo>& ClkFixedSoaType::get_field_layouts() const noexcept
{
  return field_layouts_;
}

/// Trace a field number forward through schema history
int32_t trace_field_forward(int32_t src_field_num, const ClkSchemaType& dest_schema)
{
  int32_t dest_field_num = src_field_num;
  while (dest_schema.get_became().contains(dest_field_num))
  {
    dest_field_num = dest_schema.get_became().at(dest_field_num);
  }
  return dest_field_num;
}

enum class FieldProcessingResult : std::uint8_t
{
  mapped,
  removed
};

/// Process a source field and create mapping or handle removal
jewels::Outcome<FieldProcessingResult> process_source_field(
  jewels::Out<std::pmr::vector<SoaFieldMapping>> field_mappings_out,
  int32_t src_field_num,
  const std::shared_ptr<ClkField>& src_field,
  const ClkSchemaType& dest_schema,
  const std::pmr::unordered_map<int32_t, FieldLayoutInfo>& src_layout_map,
  const std::pmr::unordered_map<int32_t, FieldLayoutInfo>& dest_layout_map)
{
  int32_t dest_field_num = trace_field_forward(src_field_num, dest_schema);

  if (dest_schema.get_removed().contains(dest_field_num))
  {
    return FieldProcessingResult::removed;
  }

  const auto dest_field_iter = dest_schema.get_fields().find(dest_field_num);
  if (dest_field_iter == dest_schema.get_fields().end())
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Field {} ({}) removed from {} without updating history",
        src_field_num,
        src_field->get_name(),
        dest_schema.get_fqn()));
  }

  const auto& dest_field = dest_field_iter->second;

  const auto src_layout_iter = src_layout_map.find(src_field_num);
  const auto dest_layout_iter = dest_layout_map.find(dest_field_num);

  if (src_layout_iter == src_layout_map.end() || dest_layout_iter == dest_layout_map.end())
  {
    throw ClkTypeUpgradeError(
      fmt::format("Missing layout information for field {} in schema {}", src_field_num, dest_schema.get_fqn()));
  }

  try
  {
    auto field_upgrader = dest_field->get_field_type().make_upgrader(src_field->get_field_type());

    field_mappings_out->emplace_back(
      SoaFieldMapping{
        .src_field = src_layout_iter->second,
        .dest_field = dest_layout_iter->second,
        .field_upgrader = field_upgrader});
  }
  catch (const ClkTypeUpgradeError& exc)
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Failed to create upgrader for field {} ({}) in {}: {}",
        dest_field_num,
        dest_field->get_name(),
        dest_schema.get_fqn(),
        exc.what()));
  }

  return FieldProcessingResult::mapped;
}

/// Initialize a newly added field with default or explicit value
void initialize_new_field(
  jewels::Out<std::pmr::vector<std::pair<FieldLayoutInfo, jewels::memory::ObjectPtr<const ClkValueInitializer>>>>
    new_field_initializers_out,
  int32_t dest_field_num,
  const std::shared_ptr<ClkField>& dest_field,
  const ClkSchemaType& dest_schema,
  const std::pmr::unordered_map<int32_t, FieldLayoutInfo>& dest_layout_map)
{
  const auto dest_layout_iter = dest_layout_map.find(dest_field_num);
  if (dest_layout_iter == dest_layout_map.end())
  {
    throw ClkTypeUpgradeError(
      fmt::format("Missing layout information for new field {} in schema {}", dest_field_num, dest_schema.get_fqn()));
  }

  jewels::FactoryResult<jewels::memory::ObjectPtr<const ClkValueInitializer>> value_initializer;
  if (ok(dest_field->get_value_initializer(jewels::Out{value_initializer})))
  {
    new_field_initializers_out->emplace_back(dest_layout_iter->second, *value_initializer);
  }
  else
  {
    // No explicit initial value - use the field type's default initializer
    auto maybe_type_initializer = dest_field->get_field_type().make_initializer();
    if (!maybe_type_initializer.has_value())
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "New field {} ({}) in {} requires an initial value but none was provided",
          dest_field_num,
          dest_field->get_name(),
          dest_schema.get_fqn()));
    }
    new_field_initializers_out->emplace_back(dest_layout_iter->second, *maybe_type_initializer);
  }
}

/// Helper function to build AoS-to-SoA upgrader components
/// @param[in] memory_resource Memory resource
/// @param[in] src_schema Source element schema type (from the AoS array)
/// @param[in] dest_schema Destination element schema type (for the SoA)
/// @param[in] dest_field_layouts Destination field layout information (from SoA)
/// @param[out] field_mappings_out Output vector of field mappings
/// @param[out] new_field_initializers_out Output vector of initializers for new fields
/// @throws ClkTypeUpgradeError on failure
void build_aos_to_soa_upgrader_components(
  const jewels::memory::MemoryResource& memory_resource,
  jewels::Out<std::pmr::vector<AosToSoaFieldMapping>> field_mappings_out,
  jewels::Out<std::pmr::vector<std::pair<FieldLayoutInfo, jewels::memory::ObjectPtr<const ClkValueInitializer>>>>
    new_field_initializers_out,
  const ClkSchemaType& src_schema,
  const ClkSchemaType& dest_schema,
  const std::pmr::vector<FieldLayoutInfo>& dest_field_layouts)
{
  // Build a map from field number to layout info for quick lookup
  std::pmr::unordered_map<int32_t, FieldLayoutInfo> dest_layout_map(memory_resource);
  for (const auto& layout : dest_field_layouts)
  {
    dest_layout_map[layout.field_num] = layout;
  }

  std::pmr::unordered_set<int32_t> upgraded_fields(memory_resource);

  for (const auto& [src_field_num, src_field] : src_schema.get_fields())
  {
    auto dest_field_num = trace_field_forward(src_field_num, dest_schema);

    if (dest_schema.get_removed().contains(dest_field_num))
    {
      continue;
    }

    const auto dest_field_iter = dest_schema.get_fields().find(dest_field_num);
    if (dest_field_iter == dest_schema.get_fields().end())
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Field {} (src: {}) not found in destination schema {}",
          dest_field_num,
          src_field_num,
          dest_schema.get_fqn()));
    }
    const auto& dest_field = dest_field_iter->second;

    const auto dest_layout_iter = dest_layout_map.find(dest_field_num);
    if (dest_layout_iter == dest_layout_map.end())
    {
      throw ClkTypeUpgradeError(
        fmt::format("Missing layout information for field {} in schema {}", dest_field_num, dest_schema.get_fqn()));
    }

    try
    {
      auto field_upgrader = dest_field->get_field_type().make_upgrader(src_field->get_field_type());

      field_mappings_out->emplace_back(
        AosToSoaFieldMapping{
          .field_num = src_field_num,
          .src_field_offset = src_field->get_offset(),
          .src_field_size = src_field->get_field_type().get_size(),
          .dest_field = dest_layout_iter->second,
          .field_upgrader = field_upgrader});
    }
    catch (const ClkTypeUpgradeError& exc)
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Failed to create upgrader for field {} ({}) in {}: {}",
          dest_field_num,
          dest_field->get_name(),
          dest_schema.get_fqn(),
          exc.what()));
    }

    upgraded_fields.insert(dest_field_num);
  }

  for (const auto& [dest_field_num, dest_field] : dest_schema.get_fields())
  {
    if (!upgraded_fields.contains(dest_field_num))
    {
      initialize_new_field(
        jewels::Out{*new_field_initializers_out}, dest_field_num, dest_field, dest_schema, dest_layout_map);
    }
  }
}

/// Helper function to build SoA-to-AoS upgrader components
/// @param[in] src_schema Source element schema type (from the SoA)
/// @param[in] dest_schema Destination element schema type (for the AoS array)
/// @param[in] src_field_layouts Source field layout information (from SoA)
/// @param[out] field_mappings_out Output vector of field mappings
/// @param[out] new_field_initializers_out Output vector of field initializers for new fields
/// @throws ClkTypeUpgradeError on failure
void build_soa_to_aos_upgrader_components(
  const jewels::memory::MemoryResource& memory_resource,
  jewels::Out<std::pmr::vector<SoaToAosFieldMapping>> field_mappings_out,
  jewels::Out<std::pmr::vector<ClkFieldInitializer>> new_field_initializers_out,
  const ClkSchemaType& src_schema,
  const ClkSchemaType& dest_schema,
  const std::pmr::vector<FieldLayoutInfo>& src_field_layouts)
{
  // Build a map from field number to layout info for quick lookup
  std::pmr::unordered_map<int32_t, FieldLayoutInfo> src_layout_map(memory_resource);
  for (const auto& layout : src_field_layouts)
  {
    src_layout_map[layout.field_num] = layout;
  }

  std::pmr::unordered_set<int32_t> upgraded_fields(memory_resource);

  for (const auto& [src_field_num, src_field] : src_schema.get_fields())
  {
    auto dest_field_num = trace_field_forward(src_field_num, dest_schema);

    if (dest_schema.get_removed().contains(dest_field_num))
    {
      continue;
    }

    const auto dest_field_iter = dest_schema.get_fields().find(dest_field_num);
    if (dest_field_iter == dest_schema.get_fields().end())
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Field {} (src: {}) not found in destination schema {}",
          dest_field_num,
          src_field_num,
          dest_schema.get_fqn()));
    }
    const auto& dest_field = dest_field_iter->second;

    const auto src_layout_iter = src_layout_map.find(src_field_num);
    if (src_layout_iter == src_layout_map.end())
    {
      throw ClkTypeUpgradeError(
        fmt::format("Missing layout information for field {} in schema {}", src_field_num, src_schema.get_fqn()));
    }

    try
    {
      auto field_upgrader = dest_field->get_field_type().make_upgrader(src_field->get_field_type());

      field_mappings_out->emplace_back(
        SoaToAosFieldMapping{
          .field_num = src_field_num,
          .src_field = src_layout_iter->second,
          .dest_field_offset = dest_field->get_offset(),
          .dest_field_size = dest_field->get_field_type().get_size(),
          .field_upgrader = field_upgrader});
    }
    catch (const ClkTypeUpgradeError& exc)
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Failed to create upgrader for field {} ({}) in {}: {}",
          dest_field_num,
          dest_field->get_name(),
          dest_schema.get_fqn(),
          exc.what()));
    }

    upgraded_fields.insert(dest_field_num);
  }

  for (const auto& [dest_field_num, dest_field] : dest_schema.get_fields())
  {
    if (!upgraded_fields.contains(dest_field_num))
    {
      auto maybe_initializer = dest_field->make_initializer();
      if (maybe_initializer.has_value())
      {
        new_field_initializers_out->emplace_back(*std::move(maybe_initializer));
      }
    }
  }
}

/// Helper function to create a SoA-to-Array upgrader
/// @param[in] params Parameters for creating the upgrader
/// @return Upgrader instance
[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader>
make_soa_to_array_upgrader(const MakeSoaToArrayUpgraderParams& params)
{
  std::pmr::vector<SoaToAosFieldMapping> field_mappings(params.memory_resource);
  std::pmr::vector<ClkFieldInitializer> new_field_initializers(params.memory_resource);

  build_soa_to_aos_upgrader_components(
    params.memory_resource,
    jewels::Out{field_mappings},
    jewels::Out{new_field_initializers},
    params.src_schema,
    params.dest_schema,
    params.src_field_layouts);

  return jewels::memory::make_non_null_from_ref(*params.upgrader_cache.get()
                                                   .emplace(
                                                     params.src_type.get().get_type_index(),
                                                     jewels::memory::make_pmr_shared<ClkSoaToArrayUpgrader>(
                                                       params.memory_resource,
                                                       ClkSoaToArrayUpgraderParams{
                                                         .memory_resource = params.memory_resource,
                                                         .src_type_fqn = params.src_type.get().get_fqn(),
                                                         .dest_type_fqn = params.dest_schema.get().get_fqn(),
                                                         .src_size_field_offset = params.src_size_field_offset,
                                                         .src_size_field_length = params.src_size_field_length,
                                                         .src_fixed_array_size = params.src_fixed_array_size,
                                                         .dest_element_size = params.dest_element_size,
                                                         .dest_size_field_offset = params.dest_size_field_offset,
                                                         .dest_array_size = params.dest_array_size,
                                                         .field_mappings = std::move(field_mappings),
                                                         .new_field_initializers = std::move(new_field_initializers)}))
                                                   .first->second);
}

/// Helper function to create a SoA-to-Optional upgrader
/// @param[in] params Parameters for creating the upgrader
/// @return Upgrader instance
[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader>
make_soa_to_optional_upgrader(const MakeSoaToOptionalUpgraderParams& params)
{
  std::pmr::vector<SoaToAosFieldMapping> field_mappings;
  std::pmr::vector<ClkFieldInitializer> new_field_initializers;

  build_soa_to_aos_upgrader_components(
    params.memory_resource,
    jewels::Out{field_mappings},
    jewels::Out{new_field_initializers},
    params.src_schema,
    params.dest_schema,
    params.src_field_layouts);

  return jewels::memory::make_non_null_from_ref(*params.upgrader_cache.get()
                                                   .emplace(
                                                     params.src_type.get().get_type_index(),
                                                     jewels::memory::make_pmr_shared<ClkSoaToOptionalUpgrader>(
                                                       params.memory_resource,
                                                       ClkSoaToOptionalUpgraderParams{
                                                         .memory_resource = params.memory_resource,
                                                         .src_type_fqn = params.src_type.get().get_fqn(),
                                                         .dest_type_fqn = params.dest_schema.get().get_fqn(),
                                                         .src_size_field_offset = params.src_size_field_offset,
                                                         .src_size_field_length = params.src_size_field_length,
                                                         .src_fixed_array_size = params.src_fixed_array_size,
                                                         .dest_element_size = params.dest_element_size,
                                                         .dest_has_value_offset = params.dest_has_value_offset,
                                                         .field_mappings = std::move(field_mappings),
                                                         .new_field_initializers = std::move(new_field_initializers)}))
                                                   .first->second);
}

/// Helper function to build SoA-to-SoA upgrader components
/// @param[in] memory_resource Memory resource
/// @param[in] src_schema Source element schema type
/// @param[in] dest_schema Destination element schema type
/// @param[in] src_field_layouts Source field layout information
/// @param[in] dest_field_layouts Destination field layout information
/// @param[out] field_mappings_out Output vector of field mappings
/// @param[out] new_field_initializers_out Output vector of initializers for new fields
/// @throws ClkTypeUpgradeError on failure
void build_soa_to_soa_upgrader_components(
  jewels::memory::MemoryResource memory_resource,
  jewels::Out<std::pmr::vector<SoaFieldMapping>> field_mappings_out,
  jewels::Out<std::pmr::vector<std::pair<FieldLayoutInfo, jewels::memory::ObjectPtr<const ClkValueInitializer>>>>
    new_field_initializers_out,
  const ClkSchemaType& src_schema,
  const ClkSchemaType& dest_schema,
  const std::pmr::vector<FieldLayoutInfo>& src_field_layouts,
  const std::pmr::vector<FieldLayoutInfo>& dest_field_layouts)
{
  // Build a map from field number to layout info for quick lookup
  std::pmr::unordered_map<int32_t, FieldLayoutInfo> src_layout_map(memory_resource);
  for (const auto& layout : src_field_layouts)
  {
    src_layout_map[layout.field_num] = layout;
  }

  std::pmr::unordered_map<int32_t, FieldLayoutInfo> dest_layout_map(memory_resource);
  for (const auto& layout : dest_field_layouts)
  {
    dest_layout_map[layout.field_num] = layout;
  }

  std::pmr::unordered_set<int32_t> upgraded_fields(memory_resource);

  for (const auto& [src_field_num, src_field] : src_schema.get_fields())
  {
    switch (process_source_field(
              jewels::Out{*field_mappings_out}, src_field_num, src_field, dest_schema, src_layout_map, dest_layout_map)
              .get())
    {
    case FieldProcessingResult::mapped:
      upgraded_fields.insert(trace_field_forward(src_field_num, dest_schema));
      break;
    case FieldProcessingResult::removed:
      // Field was removed, nothing to do
      break;
    }
  }

  for (const auto& [dest_field_num, dest_field] : dest_schema.get_fields())
  {
    if (!upgraded_fields.contains(dest_field_num))
    {
      initialize_new_field(
        jewels::Out{*new_field_initializers_out}, dest_field_num, dest_field, dest_schema, dest_layout_map);
    }
  }
}

[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> ClkFixedSoaType::make_upgrader(const ClkType& src_type)
{
  // Helper lambda to create SoA upgrader from source element type and field layouts
  auto make_soa_upgrader = [this, &src_type](
                             const ClkType& src_element_type,
                             const std::pmr::vector<FieldLayoutInfo>& src_field_layouts,
                             size_t src_size_field_offset,
                             size_t src_size_field_length) -> jewels::memory::ObjectPtr<const ClkTypeUpgrader>
  {
    const auto& src_schema = dynamic_cast<const ClkSchemaType&>(src_element_type);
    const auto& dest_schema = dynamic_cast<const ClkSchemaType&>(get_element_type());

    std::pmr::vector<SoaFieldMapping> field_mappings(get_memory_resource());
    std::pmr::vector<std::pair<FieldLayoutInfo, jewels::memory::ObjectPtr<const ClkValueInitializer>>>
      new_field_initializers(get_memory_resource());

    build_soa_to_soa_upgrader_components(
      get_memory_resource(),
      jewels::Out{field_mappings},
      jewels::Out{new_field_initializers},
      src_schema,
      dest_schema,
      src_field_layouts,
      field_layouts_);

    auto& upgrader_cache = get_upgrader_cache();
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkSoaToSoaUpgrader>(
             get_memory_resource(),
             ClkSoaToSoaUpgraderParams{
               .memory_resource = get_memory_resource(),
               .src_type_fqn = src_type.get_fqn(),
               .dest_type_fqn = get_fqn(),
               .src_size_field_offset = src_size_field_offset,
               .src_size_field_length = src_size_field_length,
               .dest_size_field_offset = 0U,
               .dest_size_field_length = 0U,
               .array_size = get_array_size(),
               .field_mappings = std::move(field_mappings),
               .new_field_initializers = std::move(new_field_initializers)}))
         .first->second);
  };

  if (src_type.get_type_id() == ClkTypeId::fixed_soa)
  {
    const auto& src_soa_type = dynamic_cast<const ClkFixedSoaType&>(src_type);
    return make_soa_upgrader(src_soa_type.get_element_type(), src_soa_type.get_field_layouts(), 0U, 0U);
  }

  if (src_type.get_type_id() == ClkTypeId::var_soa)
  {
    const auto& src_soa_type = dynamic_cast<const ClkVarSoaType&>(src_type);
    return make_soa_upgrader(
      src_soa_type.get_element_type(),
      src_soa_type.get_field_layouts(),
      src_soa_type.get_size_field_offset(),
      src_soa_type.get_size_field_length());
  }

  // Helper lambda to create AoS-to-SoA upgrader
  auto make_aos_to_soa_upgrader = [this, &src_type](
                                    const ClkType& src_element_type,
                                    size_t src_size_field_offset,
                                    size_t src_fixed_array_size,
                                    bool is_optional) -> jewels::memory::ObjectPtr<const ClkTypeUpgrader>
  {
    const auto& src_schema = dynamic_cast<const ClkSchemaType&>(src_element_type);
    const auto& dest_schema = dynamic_cast<const ClkSchemaType&>(get_element_type());

    std::pmr::vector<AosToSoaFieldMapping> field_mappings(get_memory_resource());
    std::pmr::vector<std::pair<FieldLayoutInfo, jewels::memory::ObjectPtr<const ClkValueInitializer>>>
      new_field_initializers(get_memory_resource());

    build_aos_to_soa_upgrader_components(
      get_memory_resource(),
      jewels::Out{field_mappings},
      jewels::Out{new_field_initializers},
      src_schema,
      dest_schema,
      field_layouts_);

    auto& upgrader_cache = get_upgrader_cache();
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkArrayToSoaUpgrader>(
             get_memory_resource(),
             ClkArrayToSoaUpgraderParams{
               .memory_resource = get_memory_resource(),
               .src_type_fqn = src_type.get_fqn(),
               .dest_type_fqn = get_fqn(),
               .src_element_size = src_element_type.get_size(),
               .src_size_field_offset = src_size_field_offset,
               .src_fixed_array_size = src_fixed_array_size,
               .src_is_optional = is_optional,
               .dest_size_field_offset = 0U,
               .dest_size_field_length = 0U,
               .dest_array_size = get_array_size(),
               .field_mappings = std::move(field_mappings),
               .new_field_initializers = std::move(new_field_initializers)}))
         .first->second);
  };

  if (src_type.get_type_id() == ClkTypeId::fixed_array)
  {
    const auto& src_array_type = dynamic_cast<const ClkFixedArrayType&>(src_type);
    if (src_array_type.get_array_size() != get_array_size())
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Cannot convert FixedArray with size {} to FixedSoa with size {}",
          src_array_type.get_array_size(),
          get_array_size()));
    }
    return make_aos_to_soa_upgrader(src_array_type.get_element_type(), 0U, src_array_type.get_array_size(), false);
  }

  if (src_type.get_type_id() == ClkTypeId::var_array)
  {
    const auto& src_array_type = dynamic_cast<const ClkVarArrayType&>(src_type);
    return make_aos_to_soa_upgrader(
      src_array_type.get_element_type(),
      var_array_size_offset(src_array_type.get_array_size(), src_array_type.get_element_type().get_size()),
      0U,
      false);
  }

  if (src_type.get_type_id() == ClkTypeId::optional)
  {
    const auto& src_optional_type = dynamic_cast<const ClkOptionalType&>(src_type);
    // Optional to FixedSoa: treat as 0 or 1 element array
    // src_size_field_offset stores the has_value offset, src_fixed_array_size is 0 to indicate optional
    return make_aos_to_soa_upgrader(
      src_optional_type.get_element_type(),
      optional_has_value_offset(src_optional_type.get_element_type().get_size()),
      0U,
      true);
  }

  throw ClkTypeUpgradeError(fmt::format("Conversion from {} to FixedSoa not yet implemented", src_type.get_type_id()));
}

[[nodiscard]] bool ClkFixedSoaType::use_memcpy_for_array_upgrade(const ClkType& src_type)
{
  if (src_type.get_type_id() != ClkTypeId::fixed_soa)
  {
    return false;
  }
  const auto& src_soa_type = dynamic_cast<const ClkFixedSoaType&>(src_type);
  return get_element_type().use_memcpy_for_array_upgrade(src_soa_type.get_element_type());
}

[[nodiscard]] bool ClkFixedSoaType::is_legacy_wire_compatible(const ClkType& src_type) const
{
  if (!ClkBuiltInType::is_legacy_wire_compatible(src_type))
  {
    return false;
  }
  if (src_type.get_type_id() != ClkTypeId::fixed_soa)
  {
    return false;
  }
  const auto& src_soa_type = dynamic_cast<const ClkFixedSoaType&>(src_type);
  return get_element_type().is_legacy_wire_compatible(src_soa_type.get_element_type()) &&
         get_array_size() == src_soa_type.get_array_size();
}

void ClkFixedSoaType::check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name)
{
  if (src_type.get_type_id() == ClkTypeId::var_soa)
  {
    auto& src_soa_type = dynamic_cast<ClkVarSoaType&>(src_type);
    get_element_type().check_for_unexpected_schema_changes(src_soa_type.get_element_type(), allow_changes, name);
    return;
  }

  // Allow transitions from AoS types to FixedSoa (transposition)
  if (src_type.get_type_id() == ClkTypeId::fixed_array || src_type.get_type_id() == ClkTypeId::var_array)
  {
    check_for_unexpected_underlying_schema_changes(src_type, name);
    return;
  }

  if (src_type.get_type_id() != ClkTypeId::fixed_soa)
  {
    if (!allow_changes)
    {
      throw ClkTypeUpgradeError(fmt::format("{} changed from {} to FixedSoa", name, src_type.get_type_id()));
    }
    return;
  }
  auto& src_soa_type = dynamic_cast<ClkFixedSoaType&>(src_type);
  if (get_array_size() != src_soa_type.get_array_size())
  {
    throw ClkTypeUpgradeError(
      fmt::format("{} FixedSoa size changed from {} to {}", name, src_soa_type.get_array_size(), get_array_size()));
  }
  get_element_type().check_for_unexpected_schema_changes(src_soa_type.get_element_type(), allow_changes, name);
}

[[nodiscard]] ClkType& ClkFixedSoaType::get_lowest_underlying_type()
{
  return get_element_type().get_lowest_underlying_type();
}

[[nodiscard]] bool ClkFixedSoaType::is_same_type(const ClkType& src_type) const
{
  if (src_type.get_type_id() != ClkTypeId::fixed_soa)
  {
    return false;
  }
  const auto& src_soa_type = dynamic_cast<const ClkFixedSoaType&>(src_type);
  return get_element_type().is_same_type(src_soa_type.get_element_type()) &&
         get_array_size() == src_soa_type.get_array_size();
}

[[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> ClkFixedSoaType::make_lite_compressor()
{
  if (!is_compressible())
  {
    return ClkType::make_lite_compressor();
  }
  auto& cached_lite_compressor = get_cached_lite_compressor();
  if (!cached_lite_compressor)
  {
    const auto& schema_type = dynamic_cast<const ClkSchemaType&>(get_element_type());
    std::pmr::vector<ClkFixedSoaFieldLiteCompressor> compressors{get_memory_resource()};
    compressors.reserve(field_layouts_.size());
    for (const auto& field_layout : field_layouts_)
    {
      const auto field_iter = schema_type.get_fields().find(field_layout.field_num);
      if (field_iter == schema_type.get_fields().end())
      {
        throw ClkTypeUpgradeError(
          fmt::format("Field {} not found in SOA schema {}", field_layout.field_num, schema_type.get_fqn()));
      }
      const auto& field = field_iter->second;
      std::shared_ptr<ClkTypeLiteCompressor> field_compressor;
      if (field->get_field_type().is_compressible())
      {
        field_compressor = field->get_field_type().make_lite_compressor();
      }
      compressors.emplace_back(array_size_, field_layout.offset, field_layout.size, std::move(field_compressor));
    }
    std::sort(compressors.begin(), compressors.end());
    cached_lite_compressor =
      jewels::memory::make_pmr_shared<ClkFixedSoaLiteCompressor>(get_memory_resource(), std::move(compressors));
  }
  return jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor>{cached_lite_compressor};
}

[[nodiscard]] bool ClkFixedSoaType::is_compressible()
{
  auto& cached_is_compressible = get_cached_is_compressible();
  if (!cached_is_compressible.has_value())
  {
    cached_is_compressible = true;
  }
  return cached_is_compressible.value();
}

ClkVarSoaType::ClkVarSoaType(ConstructorParams params)
  : ClkBuiltInType(
      params.factory->get_memory_resource(),
      params.fqn,
      params.type_id,
      params.type_index,
      params.factory->get_metadata_version(),
      params.size,
      params.alignment),
    element_type_index_(params.element_type_index),
    array_size_(params.array_size),
    field_layouts_(std::move(params.field_layouts)),
    size_field_offset_(params.size_field_offset),
    size_field_type_index_(params.size_field_type_index),
    factory_(params.factory)
{
}

// NOLINTNEXTLINE(misc-no-recursion) Types are defined recursively
[[nodiscard]] std::shared_ptr<ClkVarSoaType> ClkVarSoaType::from_proto(
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  const metadata::SoaType& soa_proto,
  size_t type_index,
  std::optional<std::string_view> maybe_strong_type_fqn)
{
  const auto element_type_index = static_cast<size_t>(soa_proto.schema_type_id());
  // std::ignore: We only need to trigger type loading; the pointer will be fetched later
  std::ignore = factory->get_clk_type(element_type_index);

  if (!soa_proto.has_size_field_offset() || !soa_proto.has_size_field_type_id())
  {
    throw ClkTypeUpgradeError(fmt::format("VarSoa type {} missing size field information", soa_proto.fqn()));
  }

  // Validate the size field type
  const auto field_size_type_index = static_cast<size_t>(soa_proto.size_field_type_id());
  const auto& field_size_type = factory->get_clk_type(field_size_type_index);
  if (
    field_size_type->get_type_id() != ClkTypeId::uint8 && field_size_type->get_type_id() != ClkTypeId::uint16 &&
    field_size_type->get_type_id() != ClkTypeId::uint32 && field_size_type->get_type_id() != ClkTypeId::uint64)
  {
    throw ClkTypeUpgradeError(
      fmt::format("VarSoa type {} has invalid size field type", soa_proto.fqn(), field_size_type->get_fqn()));
  }

  std::pmr::vector<FieldLayoutInfo> field_layouts;
  field_layouts.reserve(static_cast<size_t>(soa_proto.field_layouts_size()));
  for (const auto& field_proto : soa_proto.field_layouts())
  {
    const auto& field_type = *factory->get_clk_type(static_cast<size_t>(field_proto.type_id()));
    field_layouts.push_back(
      FieldLayoutInfo{
        .field_num = field_proto.num(),
        .size = field_type.get_size(),
        .alignment = field_type.get_alignment(),
        .offset = static_cast<size_t>(field_proto.offset()),
      });
  }

  return jewels::memory::make_pmr_shared<ClkVarSoaType>(
    factory->get_memory_resource(),
    ClkVarSoaType::ConstructorParams{
      .fqn = maybe_strong_type_fqn.value_or(soa_proto.fqn()),
      .type_id = ClkTypeId::var_soa,
      .type_index = type_index,
      .size = static_cast<size_t>(soa_proto.size()),
      .alignment = static_cast<size_t>(soa_proto.alignment()),
      .element_type_index = element_type_index,
      .array_size = static_cast<size_t>(soa_proto.container_size()),
      .field_layouts = std::move(field_layouts),
      .size_field_offset = static_cast<size_t>(soa_proto.size_field_offset()),
      .size_field_type_index = field_size_type_index,
      .factory = factory,
    });
}

[[nodiscard]] const ClkType& ClkVarSoaType::get_element_type() const
{
  return *factory_->get_types().at(element_type_index_);
}

[[nodiscard]] ClkType& ClkVarSoaType::get_element_type()
{
  return *factory_->get_types().at(element_type_index_);
}

[[nodiscard]] size_t ClkVarSoaType::get_array_size() const noexcept
{
  return array_size_;
}

[[nodiscard]] const std::pmr::vector<FieldLayoutInfo>& ClkVarSoaType::get_field_layouts() const noexcept
{
  return field_layouts_;
}

[[nodiscard]] size_t ClkVarSoaType::get_size_field_offset() const noexcept
{
  return size_field_offset_;
}

[[nodiscard]] size_t ClkVarSoaType::get_size_field_length() const noexcept
{
  return get_size_field_type().get_size();
}

[[nodiscard]] const ClkType& ClkVarSoaType::get_size_field_type() const
{
  return *factory_->get_types().at(size_field_type_index_);
}

[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> ClkVarSoaType::make_upgrader(const ClkType& src_type)
{
  // Helper lambda to create SoA upgrader from source element type and field layouts
  auto make_soa_upgrader = [this, &src_type](
                             const ClkType& src_element_type,
                             const std::pmr::vector<FieldLayoutInfo>& src_field_layouts,
                             size_t src_size_field_offset,
                             size_t src_size_field_length,
                             size_t array_size) -> jewels::memory::ObjectPtr<const ClkTypeUpgrader>
  {
    const auto& src_schema = dynamic_cast<const ClkSchemaType&>(src_element_type);
    const auto& dest_schema = dynamic_cast<const ClkSchemaType&>(get_element_type());

    std::pmr::vector<SoaFieldMapping> field_mappings(get_memory_resource());
    std::pmr::vector<std::pair<FieldLayoutInfo, jewels::memory::ObjectPtr<const ClkValueInitializer>>>
      new_field_initializers(get_memory_resource());

    build_soa_to_soa_upgrader_components(
      get_memory_resource(),
      jewels::Out{field_mappings},
      jewels::Out{new_field_initializers},
      src_schema,
      dest_schema,
      src_field_layouts,
      field_layouts_);

    auto& upgrader_cache = get_upgrader_cache();
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkSoaToSoaUpgrader>(
             get_memory_resource(),
             ClkSoaToSoaUpgraderParams{
               .memory_resource = get_memory_resource(),
               .src_type_fqn = src_type.get_fqn(),
               .dest_type_fqn = get_fqn(),
               .src_size_field_offset = src_size_field_offset,
               .src_size_field_length = src_size_field_length,
               .dest_size_field_offset = size_field_offset_,
               .dest_size_field_length = get_size_field_length(),
               .array_size = array_size,
               .field_mappings = std::move(field_mappings),
               .new_field_initializers = std::move(new_field_initializers)}))
         .first->second);
  };

  if (src_type.get_type_id() == ClkTypeId::var_soa)
  {
    const auto& src_soa_type = dynamic_cast<const ClkVarSoaType&>(src_type);
    return make_soa_upgrader(
      src_soa_type.get_element_type(),
      src_soa_type.get_field_layouts(),
      src_soa_type.get_size_field_offset(),
      src_soa_type.get_size_field_length(),
      0U);
  }

  if (src_type.get_type_id() == ClkTypeId::fixed_soa)
  {
    const auto& src_soa_type = dynamic_cast<const ClkFixedSoaType&>(src_type);
    return make_soa_upgrader(
      src_soa_type.get_element_type(), src_soa_type.get_field_layouts(), 0U, 0U, src_soa_type.get_array_size());
  }

  // Helper lambda to create AoS-to-SoA upgrader
  auto make_aos_to_soa_upgrader = [this, &src_type](
                                    const ClkType& src_element_type,
                                    size_t src_size_field_offset,
                                    size_t src_fixed_array_size,
                                    bool is_optional) -> jewels::memory::ObjectPtr<const ClkTypeUpgrader>
  {
    const auto& src_schema = dynamic_cast<const ClkSchemaType&>(src_element_type);
    const auto& dest_schema = dynamic_cast<const ClkSchemaType&>(get_element_type());

    std::pmr::vector<AosToSoaFieldMapping> field_mappings(get_memory_resource());
    std::pmr::vector<std::pair<FieldLayoutInfo, jewels::memory::ObjectPtr<const ClkValueInitializer>>>
      new_field_initializers(get_memory_resource());

    build_aos_to_soa_upgrader_components(
      get_memory_resource(),
      jewels::Out{field_mappings},
      jewels::Out{new_field_initializers},
      src_schema,
      dest_schema,
      field_layouts_);

    auto& upgrader_cache = get_upgrader_cache();
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkArrayToSoaUpgrader>(
             get_memory_resource(),
             ClkArrayToSoaUpgraderParams{
               .memory_resource = get_memory_resource(),
               .src_type_fqn = src_type.get_fqn(),
               .dest_type_fqn = get_fqn(),
               .src_element_size = src_element_type.get_size(),
               .src_size_field_offset = src_size_field_offset,
               .src_fixed_array_size = src_fixed_array_size,
               .src_is_optional = is_optional,
               .dest_size_field_offset = size_field_offset_,
               .dest_size_field_length = get_size_field_length(),
               .dest_array_size = get_array_size(),
               .field_mappings = std::move(field_mappings),
               .new_field_initializers = std::move(new_field_initializers)}))
         .first->second);
  };

  if (src_type.get_type_id() == ClkTypeId::fixed_array)
  {
    const auto& src_array_type = dynamic_cast<const ClkFixedArrayType&>(src_type);
    return make_aos_to_soa_upgrader(src_array_type.get_element_type(), 0U, src_array_type.get_array_size(), false);
  }

  if (src_type.get_type_id() == ClkTypeId::var_array)
  {
    const auto& src_array_type = dynamic_cast<const ClkVarArrayType&>(src_type);
    return make_aos_to_soa_upgrader(
      src_array_type.get_element_type(),
      var_array_size_offset(src_array_type.get_array_size(), src_array_type.get_element_type().get_size()),
      0U,
      false);
  }

  if (src_type.get_type_id() == ClkTypeId::optional)
  {
    const auto& src_optional_type = dynamic_cast<const ClkOptionalType&>(src_type);
    // Optional to VarSoa: treat as 0 or 1 element array
    return make_aos_to_soa_upgrader(
      src_optional_type.get_element_type(),
      optional_has_value_offset(src_optional_type.get_element_type().get_size()),
      0U,
      true);
  }

  throw ClkTypeUpgradeError(fmt::format("Conversion from {} to VarSoa not yet implemented", src_type.get_type_id()));
}

[[nodiscard]] bool ClkVarSoaType::use_memcpy_for_array_upgrade(const ClkType& src_type)
{
  if (src_type.get_type_id() != ClkTypeId::var_soa || get_size() > max_memcpy_size)
  {
    return false;
  }
  const auto& src_soa_type = dynamic_cast<const ClkVarSoaType&>(src_type);
  return get_element_type().use_memcpy_for_array_upgrade(src_soa_type.get_element_type());
}

[[nodiscard]] bool ClkVarSoaType::is_legacy_wire_compatible(const ClkType& src_type) const
{
  if (!ClkBuiltInType::is_legacy_wire_compatible(src_type))
  {
    return false;
  }
  if (src_type.get_type_id() != ClkTypeId::var_soa)
  {
    return false;
  }
  const auto& src_soa_type = dynamic_cast<const ClkVarSoaType&>(src_type);
  return get_element_type().is_legacy_wire_compatible(src_soa_type.get_element_type()) &&
         get_array_size() == src_soa_type.get_array_size();
}

void ClkVarSoaType::check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name)
{
  if (src_type.get_type_id() == ClkTypeId::fixed_soa)
  {
    auto& src_soa_type = dynamic_cast<ClkFixedSoaType&>(src_type);
    get_element_type().check_for_unexpected_schema_changes(src_soa_type.get_element_type(), allow_changes, name);
    return;
  }

  // Allow transitions from AoS types to VarSoa (transposition)
  if (src_type.get_type_id() == ClkTypeId::fixed_array || src_type.get_type_id() == ClkTypeId::var_array)
  {
    check_for_unexpected_underlying_schema_changes(src_type, name);
    return;
  }

  if (src_type.get_type_id() != ClkTypeId::var_soa)
  {
    if (!allow_changes)
    {
      throw ClkTypeUpgradeError(fmt::format("{} changed from {} to VarSoa", name, src_type.get_type_id()));
    }
    return;
  }
  auto& src_soa_type = dynamic_cast<ClkVarSoaType&>(src_type);
  if (get_array_size() != src_soa_type.get_array_size())
  {
    throw ClkTypeUpgradeError(
      fmt::format("{} VarSoa max size changed from {} to {}", name, src_soa_type.get_array_size(), get_array_size()));
  }
  get_element_type().check_for_unexpected_schema_changes(src_soa_type.get_element_type(), allow_changes, name);
}

[[nodiscard]] ClkType& ClkVarSoaType::get_lowest_underlying_type()
{
  return get_element_type().get_lowest_underlying_type();
}

[[nodiscard]] bool ClkVarSoaType::is_same_type(const ClkType& src_type) const
{
  if (src_type.get_type_id() != ClkTypeId::var_soa)
  {
    return false;
  }
  const auto& src_soa_type = dynamic_cast<const ClkVarSoaType&>(src_type);
  return get_element_type().is_same_type(src_soa_type.get_element_type()) &&
         get_array_size() == src_soa_type.get_array_size();
}

[[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> ClkVarSoaType::make_lite_compressor()
{
  auto& cached_lite_compressor = get_cached_lite_compressor();
  if (!cached_lite_compressor)
  {
    const auto& schema_type = dynamic_cast<const ClkSchemaType&>(get_element_type());
    std::pmr::vector<ClkVarSoaFieldLiteCompressor> compressors{get_memory_resource()};
    compressors.reserve(field_layouts_.size());
    for (const auto& field_layout : field_layouts_)
    {
      const auto field_iter = schema_type.get_fields().find(field_layout.field_num);
      if (field_iter == schema_type.get_fields().end())
      {
        throw ClkTypeUpgradeError(
          fmt::format("Field {} not found in SOA schema {}", field_layout.field_num, schema_type.get_fqn()));
      }
      const auto& field = field_iter->second;
      std::shared_ptr<ClkTypeLiteCompressor> field_compressor;
      if (field->get_field_type().is_compressible())
      {
        field_compressor = field->get_field_type().make_lite_compressor();
      }
      compressors.emplace_back(array_size_, field_layout.offset, field_layout.size, std::move(field_compressor));
    }
    std::sort(compressors.begin(), compressors.end());
    cached_lite_compressor = jewels::memory::make_pmr_shared<ClkVarSoaLiteCompressor>(
      get_memory_resource(), size_field_offset_, get_size_field_length(), std::move(compressors));
  }
  return jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor>{cached_lite_compressor};
}

[[nodiscard]] bool ClkVarSoaType::is_compressible()
{
  auto& cached_is_compressible = get_cached_is_compressible();
  if (!cached_is_compressible.has_value())
  {
    cached_is_compressible = true;
  }
  return cached_is_compressible.value();
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

[[nodiscard]] size_t load_soa_size(std::span<const std::byte> soa_span, size_t size_offset, size_t size_length)
{
  switch (size_length)
  {
  case sizeof(uint8_t):
    return jewels::memory::bit_cast_to<uint8_t>(
      std::span<const std::byte, sizeof(uint8_t)>{soa_span.subspan(size_offset, sizeof(uint8_t))});
  case sizeof(uint16_t):
    return jewels::memory::bit_cast_to<uint16_t>(
      std::span<const std::byte, sizeof(uint16_t)>{soa_span.subspan(size_offset, sizeof(uint16_t))});
  case sizeof(uint32_t):
    return jewels::memory::bit_cast_to<uint32_t>(
      std::span<const std::byte, sizeof(uint32_t)>{soa_span.subspan(size_offset, sizeof(uint32_t))});
  case sizeof(uint64_t):
    return jewels::memory::bit_cast_to<uint64_t>(
      std::span<const std::byte, sizeof(uint64_t)>{soa_span.subspan(size_offset, sizeof(uint64_t))});
  default:
    throw ClkTypeUpgradeError(fmt::format("INTERNAL ERROR: Invalid SOA size field length: {}", size_length));
  }
}

void store_soa_size(std::span<std::byte> soa_span, size_t size_offset, size_t size_length, size_t size_value)
{
  switch (size_length)
  {
  case sizeof(uint8_t):
    jewels::memory::write_as_bytes(
      static_cast<uint8_t>(size_value),
      std::span<std::byte, sizeof(uint8_t)>{soa_span.subspan(size_offset, sizeof(uint8_t))});
    break;
  case sizeof(uint16_t):
    jewels::memory::write_as_bytes(
      static_cast<uint16_t>(size_value),
      std::span<std::byte, sizeof(uint16_t)>{soa_span.subspan(size_offset, sizeof(uint16_t))});
    break;
  case sizeof(uint32_t):
    jewels::memory::write_as_bytes(
      static_cast<uint32_t>(size_value),
      std::span<std::byte, sizeof(uint32_t)>{soa_span.subspan(size_offset, sizeof(uint32_t))});
    break;
  case sizeof(uint64_t):
    jewels::memory::write_as_bytes(
      static_cast<uint64_t>(size_value),
      std::span<std::byte, sizeof(uint64_t)>{soa_span.subspan(size_offset, sizeof(uint64_t))});
    break;
  default:
    throw ClkTypeUpgradeError(fmt::format("INTERNAL ERROR: Invalid SOA size field length: {}", size_length));
  }
}

[[nodiscard]] std::span<std::byte>
get_soa_field_array(std::span<std::byte> soa_buffer, const FieldLayoutInfo& field_layout, size_t array_length) noexcept
{
  const size_t field_array_size = array_length * field_layout.size;
  return soa_buffer.subspan(field_layout.offset, field_array_size);
}

[[nodiscard]] std::span<const std::byte> get_soa_field_array(
  std::span<const std::byte> soa_buffer, const FieldLayoutInfo& field_layout, size_t array_length) noexcept
{
  const size_t field_array_size = array_length * field_layout.size;
  return soa_buffer.subspan(field_layout.offset, field_array_size);
}

// NOLINTNEXTLINE(misc-no-recursion, readability-function-size) Recursive type deserialization needs one dispatcher.
[[nodiscard]] std::shared_ptr<ClkType> ClkBuiltInTypeFactoryPlugin::make_clk_type(
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  const metadata::TypeDesc& type_proto,
  size_t type_index,
  std::optional<std::string_view> maybe_strong_type_fqn) const
{
  if (type_proto.has_tag())
  {
    return jewels::memory::make_pmr_shared<ClkTagType>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(type_proto.tag().fqn()),
      ClkTypeId::tag,
      type_index,
      factory->get_metadata_version());
  }
  if (type_proto.has_soa_type())
  {
    const auto& soa_proto = type_proto.soa_type();
    const bool is_var_soa = soa_proto.has_size_field_offset();

    if (is_var_soa)
    {
      return ClkVarSoaType::from_proto(factory, soa_proto, type_index, maybe_strong_type_fqn);
    }
    return ClkFixedSoaType::from_proto(factory, soa_proto, type_index, maybe_strong_type_fqn);
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
    return jewels::memory::make_pmr_shared<ClkPrimitiveType<bool>>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::boolean,
      type_index,
      factory->get_metadata_version(),
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::uint8:
    return jewels::memory::make_pmr_shared<ClkIntegerType<uint8_t>>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::uint8,
      type_index,
      factory->get_metadata_version(),
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::uint16:
    return jewels::memory::make_pmr_shared<ClkIntegerType<uint16_t>>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::uint16,
      type_index,
      factory->get_metadata_version(),
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::uint32:
    return jewels::memory::make_pmr_shared<ClkIntegerType<uint32_t>>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::uint32,
      type_index,
      factory->get_metadata_version(),
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::uint64:
    return jewels::memory::make_pmr_shared<ClkIntegerType<uint64_t>>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::uint64,
      type_index,
      factory->get_metadata_version(),
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::int8:
    return jewels::memory::make_pmr_shared<ClkIntegerType<int8_t>>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::int8,
      type_index,
      factory->get_metadata_version(),
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::int16:
    return jewels::memory::make_pmr_shared<ClkIntegerType<int16_t>>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::int16,
      type_index,
      factory->get_metadata_version(),
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::int32:
    return jewels::memory::make_pmr_shared<ClkIntegerType<int32_t>>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::int32,
      type_index,
      factory->get_metadata_version(),
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::int64:
    return jewels::memory::make_pmr_shared<ClkIntegerType<int64_t>>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::int64,
      type_index,
      factory->get_metadata_version(),
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::float32:
    return jewels::memory::make_pmr_shared<ClkFloatingPointType<float>>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::float32,
      type_index,
      factory->get_metadata_version(),
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::float64:
    return jewels::memory::make_pmr_shared<ClkFloatingPointType<double>>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::float64,
      type_index,
      factory->get_metadata_version(),
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::synctime:
    return jewels::memory::make_pmr_shared<ClkTimeType>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::synctime,
      type_index,
      factory->get_metadata_version(),
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::duration:
    return jewels::memory::make_pmr_shared<ClkTimeType>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::duration,
      type_index,
      factory->get_metadata_version(),
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::byte:
    return jewels::memory::make_pmr_shared<ClkPrimitiveType<std::byte>>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::byte,
      type_index,
      factory->get_metadata_version(),
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::uuid:
    return jewels::memory::make_pmr_shared<ClkPrimitiveType<SchemaUuid>>(
      factory->get_memory_resource(),
      factory->get_memory_resource(),
      maybe_strong_type_fqn.value_or(built_in_proto.fqn()),
      ClkTypeId::uuid,
      type_index,
      factory->get_metadata_version(),
      static_cast<size_t>(built_in_proto.size()),
      static_cast<size_t>(built_in_proto.alignment()));
  case ClkTypeId::fixed_array:
    return ClkFixedArrayType::from_proto(factory, built_in_proto, type_index, maybe_strong_type_fqn);
  case ClkTypeId::var_array:
    return ClkVarArrayType::from_proto(factory, built_in_proto, type_index, maybe_strong_type_fqn);
  case ClkTypeId::var_string:
    return ClkVarStringType::from_proto(factory, built_in_proto, type_index, maybe_strong_type_fqn);
  case ClkTypeId::optional:
    return ClkOptionalType::from_proto(factory, built_in_proto, type_index, maybe_strong_type_fqn);
  case ClkTypeId::fixed_soa:
  case ClkTypeId::var_soa:
    // SoA types are handled via type_proto.has_soa_type() above
    // These cases should not be reached, but are kept for safety
    throw ClkTypeUpgradeError(
      fmt::format("SoA types should be handled via SoaType protobuf, not BuiltInType: {}", built_in_proto.fqn()));
  case ClkTypeId::tensor:
    return ClkTensorType::from_proto(factory, built_in_proto, type_index, maybe_strong_type_fqn);
  case ClkTypeId::bitset:
    return ClkBitsetType::from_proto(factory, built_in_proto, type_index, maybe_strong_type_fqn);
  default:
    throw ClkTypeUpgradeError(fmt::format("Unhandled built in type id: {}", type_id));
  }
}

} // namespace clockwork::serialization
