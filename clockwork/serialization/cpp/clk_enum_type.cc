// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/serialization/cpp/clk_enum_type.hh"

#include "clockwork/serialization/cpp/clk_type.hh"
#include "clockwork/serialization/cpp/metadata_versions.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "jewels/memory/bits.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/uuid/uuid.hh"

#include <fmt/format.h>
#include <google/protobuf/repeated_ptr_field.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace clockwork::serialization
{

namespace
{

/// Clockwork value enum upgrader
/// @tparam SrcValueType Source enum value type
/// @tparam DestValueType Destination enum value type
template <typename SrcValueType, typename DestValueType>
class ClkValueEnumUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] src_fqn Source enum FQN
  /// @param[in] value_map Map from source enum value to destination enum value
  ClkValueEnumUpgrader(
    const jewels::memory::MemoryResource& memory_resource,
    std::string_view src_fqn,
    std::pmr::unordered_map<SrcValueType, DestValueType> value_map) noexcept;

  ~ClkValueEnumUpgrader() noexcept override = default;

  ClkValueEnumUpgrader(const ClkValueEnumUpgrader&) = delete;
  ClkValueEnumUpgrader& operator=(const ClkValueEnumUpgrader&) = delete;
  ClkValueEnumUpgrader(ClkValueEnumUpgrader&&) = delete;
  ClkValueEnumUpgrader& operator=(ClkValueEnumUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source type FQN
  std::pmr::string src_fqn_;

  /// Map from source enum value to destination enum value
  std::pmr::unordered_map<SrcValueType, DestValueType> value_map_;
};

template <typename SrcValueType, typename DestValueType>
ClkValueEnumUpgrader<SrcValueType, DestValueType>::ClkValueEnumUpgrader(
  const jewels::memory::MemoryResource& memory_resource,
  std::string_view src_fqn,
  std::pmr::unordered_map<SrcValueType, DestValueType> value_map) noexcept
  : src_fqn_(src_fqn, memory_resource), value_map_(std::move(value_map))
{
}

template <typename SrcValueType, typename DestValueType>
void ClkValueEnumUpgrader<SrcValueType, DestValueType>::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const auto src_value = jewels::memory::bit_cast_to<SrcValueType>(
    std::span<const std::byte, sizeof(SrcValueType)>{src_span.first(sizeof(SrcValueType))});
  const auto value_iter = value_map_.find(src_value);
  if (value_iter == value_map_.end())
  {
    throw ClkTypeUpgradeError(fmt::format("Invalid enum value ({}) in upgrade from {}", src_value, src_fqn_));
  }
  jewels::memory::write_as_bytes(
    value_iter->second, std::span<std::byte, sizeof(DestValueType)>{dest_span.first(sizeof(DestValueType))});
}

/// Clockwork bit-flag enum to bit-flag enum upgrader
/// @tparam SrcValueType Source enum value type
/// @tparam DestValueType Destination enum value type
template <typename SrcValueType, typename DestValueType>
class ClkBitFlagToBitFlagEnumUpgrader : public ClkTypeUpgrader
{
  using UnsignedSrcValueType = std::make_unsigned_t<SrcValueType>;
  using UnsignedDestValueType = std::make_unsigned_t<DestValueType>;

public:
  /// Constructor
  /// @param[in] memory_resource
  /// @param[in] src_fqn Source enum FQN
  /// @param[in] value_map Map from source enum value to destination enum value
  ClkBitFlagToBitFlagEnumUpgrader(
    const jewels::memory::MemoryResource& memory_resource,
    std::string_view src_fqn,
    const std::pmr::unordered_map<SrcValueType, DestValueType>& value_map);

  ~ClkBitFlagToBitFlagEnumUpgrader() noexcept override = default;

  ClkBitFlagToBitFlagEnumUpgrader(const ClkBitFlagToBitFlagEnumUpgrader&) = delete;
  ClkBitFlagToBitFlagEnumUpgrader& operator=(const ClkBitFlagToBitFlagEnumUpgrader&) = delete;
  ClkBitFlagToBitFlagEnumUpgrader(ClkBitFlagToBitFlagEnumUpgrader&&) = delete;
  ClkBitFlagToBitFlagEnumUpgrader& operator=(ClkBitFlagToBitFlagEnumUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source type FQN
  std::pmr::string src_fqn_;

  /// Map from source enum value to destination enum value
  std::pmr::vector<std::pair<UnsignedSrcValueType, UnsignedDestValueType>> value_map_;

  /// Value source value bit mask
  UnsignedSrcValueType valid_src_value_mask_{0U};

  /// Value to set when source value is zero
  UnsignedDestValueType default_dest_value_{0};
};

template <typename SrcValueType, typename DestValueType>
ClkBitFlagToBitFlagEnumUpgrader<SrcValueType, DestValueType>::ClkBitFlagToBitFlagEnumUpgrader(
  const jewels::memory::MemoryResource& memory_resource,
  std::string_view src_fqn,
  const std::pmr::unordered_map<SrcValueType, DestValueType>& value_map)
  : src_fqn_(src_fqn, memory_resource), value_map_(memory_resource)
{
  value_map_.reserve(value_map.size());
  bool found_default = false;
  for (const auto& map_entry : value_map)
  {
    if (map_entry.first == 0)
    {
      default_dest_value_ = static_cast<UnsignedDestValueType>(map_entry.second);
      found_default = true;
    }
    else
    {
      value_map_.emplace_back(
        static_cast<UnsignedSrcValueType>(map_entry.first), static_cast<UnsignedDestValueType>(map_entry.second));
      valid_src_value_mask_ |= value_map_.back().first;
    }
  }
  if (!found_default)
  {
    throw ClkTypeUpgradeError("No default found in enum values");
  }
}

template <typename SrcValueType, typename DestValueType>
void ClkBitFlagToBitFlagEnumUpgrader<SrcValueType, DestValueType>::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const auto src_value = jewels::memory::bit_cast_to<UnsignedSrcValueType>(
    std::span<const std::byte, sizeof(UnsignedSrcValueType)>{src_span.first(sizeof(UnsignedSrcValueType))});
  UnsignedDestValueType dest_value = 0U;
  if (src_value == 0U)
  {
    dest_value = default_dest_value_;
  }
  else
  {
    if ((src_value & valid_src_value_mask_) != src_value)
    {
      throw ClkTypeUpgradeError(fmt::format("Invalid enum bit flags (0x{:x}) in upgrade from {}", src_value, src_fqn_));
    }
    UnsignedSrcValueType matched_src_bits = 0U;
    for (const auto& map_entry : value_map_)
    {
      if ((src_value & map_entry.first) == map_entry.first)
      {
        matched_src_bits |= map_entry.first;
        dest_value |= map_entry.second;
      }
    }
    if (matched_src_bits != src_value)
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Unmatched enum bit flags (0x{:x}) for value (0x{:x}) in upgrade from {}",
          src_value & static_cast<UnsignedSrcValueType>(~matched_src_bits),
          src_value,
          src_fqn_));
    }
  }
  jewels::memory::write_as_bytes(
    dest_value, std::span<std::byte, sizeof(DestValueType)>{dest_span.first(sizeof(DestValueType))});
}

/// Clockwork bit-flag enum to value enum upgrader
/// @tparam SrcValueType Source enum value type
/// @tparam DestValueType Destination enum value type
template <typename SrcValueType, typename DestValueType>
class ClkBitFlagToValueEnumUpgrader : public ClkTypeUpgrader
{
  using UnsignedSrcValueType = std::make_unsigned_t<SrcValueType>;

public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] src_fqn Source enum FQN
  /// @param[in] value_map Map from source enum value to destination enum value
  ClkBitFlagToValueEnumUpgrader(
    const jewels::memory::MemoryResource& memory_resource,
    std::string_view src_fqn,
    const std::pmr::unordered_map<SrcValueType, DestValueType>& value_map);

  ~ClkBitFlagToValueEnumUpgrader() noexcept override = default;

  ClkBitFlagToValueEnumUpgrader(const ClkBitFlagToValueEnumUpgrader&) = delete;
  ClkBitFlagToValueEnumUpgrader& operator=(const ClkBitFlagToValueEnumUpgrader&) = delete;
  ClkBitFlagToValueEnumUpgrader(ClkBitFlagToValueEnumUpgrader&&) = delete;
  ClkBitFlagToValueEnumUpgrader& operator=(ClkBitFlagToValueEnumUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source type FQN
  std::pmr::string src_fqn_;

  /// Map from source enum value to destination enum value
  std::pmr::vector<std::pair<UnsignedSrcValueType, DestValueType>> value_map_;

  /// Value source value bit mask
  UnsignedSrcValueType valid_src_value_mask_{0U};

  /// Value to set when source value is zero
  DestValueType default_dest_value_{0};
};

template <typename SrcValueType, typename DestValueType>
ClkBitFlagToValueEnumUpgrader<SrcValueType, DestValueType>::ClkBitFlagToValueEnumUpgrader(
  const jewels::memory::MemoryResource& memory_resource,
  std::string_view src_fqn,
  const std::pmr::unordered_map<SrcValueType, DestValueType>& value_map)
  : src_fqn_(src_fqn, memory_resource), value_map_(memory_resource)
{
  value_map_.reserve(value_map.size());
  bool found_default = false;
  for (const auto& map_entry : value_map)
  {
    if (map_entry.first == 0U)
    {
      default_dest_value_ = map_entry.second;
      found_default = true;
    }
    else
    {
      value_map_.emplace_back(static_cast<UnsignedSrcValueType>(map_entry.first), map_entry.second);
      valid_src_value_mask_ |= value_map_.back().first;
    }
  }
  if (!found_default)
  {
    throw ClkTypeUpgradeError("No default found in enum values");
  }
}

template <typename SrcValueType, typename DestValueType>
void ClkBitFlagToValueEnumUpgrader<SrcValueType, DestValueType>::upgrade(
  std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const auto src_value = jewels::memory::bit_cast_to<UnsignedSrcValueType>(
    std::span<const std::byte, sizeof(UnsignedSrcValueType)>{src_span.first(sizeof(UnsignedSrcValueType))});
  DestValueType dest_value = 0;
  if (src_value == 0U)
  {
    dest_value = default_dest_value_;
  }
  else
  {
    if ((src_value & valid_src_value_mask_) != src_value)
    {
      throw ClkTypeUpgradeError(fmt::format("Invalid enum bit flags (0x{:x}) in upgrade from {}", src_value, src_fqn_));
    }
    bool found_match = false;
    for (const auto& map_entry : value_map_)
    {
      if ((src_value & map_entry.first) == map_entry.first)
      {
        if (found_match)
        {
          throw ClkTypeUpgradeError(
            fmt::format("Ambiguous enum bit flags (0x{:x}) in upgrade to value enum from {}", src_value, src_fqn_));
        }
        dest_value = map_entry.second;
        found_match = true;
      }
    }
    if (!found_match)
    {
      throw ClkTypeUpgradeError(
        fmt::format("No match for enum bit flags (0x{:x}) in upgrade to value enum from {}", src_value, src_fqn_));
    }
  }
  jewels::memory::write_as_bytes(
    dest_value, std::span<std::byte, sizeof(DestValueType)>{dest_span.first(sizeof(DestValueType))});
}

/// Clockwork enum value
class ClkEnumValue
{
public:
  /// Constructor
  ClkEnumValue(int32_t num, int64_t value, std::pmr::string name);

  ~ClkEnumValue() = default;

  ClkEnumValue(const ClkEnumValue&) noexcept = default;
  ClkEnumValue& operator=(const ClkEnumValue&) noexcept = default;
  ClkEnumValue(ClkEnumValue&&) noexcept = default;
  ClkEnumValue& operator=(ClkEnumValue&&) noexcept = default;

  /// @return Enum value number
  [[nodiscard]] int32_t get_num() const noexcept;

  /// @return Enum value
  [[nodiscard]] int64_t get_value() const noexcept;

  /// @return Enum value name
  [[nodiscard]] std::string_view get_name() const noexcept;

  /// Equality operator
  [[nodiscard]] friend bool operator==(const ClkEnumValue& lhs, const ClkEnumValue& rhs) noexcept = default;

private:
  /// Enum value number
  int32_t num_;

  /// Enum value
  int64_t value_;

  /// Enum value name
  std::pmr::string name_;
};

ClkEnumValue::ClkEnumValue(int32_t num, int64_t value, std::pmr::string name)
  : num_(num), value_(value), name_(std::move(name))
{
}

[[nodiscard]] int32_t ClkEnumValue::get_num() const noexcept
{
  return num_;
}

[[nodiscard]] int64_t ClkEnumValue::get_value() const noexcept
{
  return value_;
}

[[nodiscard]] std::string_view ClkEnumValue::get_name() const noexcept
{
  return name_;
}

/// Initializer for a clockwork enum value
/// @tparam ValueType Value type
template <typename ValueType>
class ClkEnumInitializer : public ClkValueInitializer
{
public:
  /// Constructor
  /// @param[in] value Initial value
  explicit ClkEnumInitializer(ValueType value);

  ~ClkEnumInitializer() override = default;

  ClkEnumInitializer(const ClkEnumInitializer&) = delete;
  ClkEnumInitializer& operator=(const ClkEnumInitializer&) = delete;
  ClkEnumInitializer(ClkEnumInitializer&&) = delete;
  ClkEnumInitializer& operator=(ClkEnumInitializer&&) = delete;

  /// @see ClkValueInitializer::initialize
  void initialize(std::span<std::byte> dest_span) const override;

private:
  /// Initial value
  ValueType value_;
};

template <typename ValueType>
ClkEnumInitializer<ValueType>::ClkEnumInitializer(ValueType value)
  : value_(value)
{
}

template <typename ValueType>
void ClkEnumInitializer<ValueType>::initialize(std::span<std::byte> dest_span) const
{
  std::memcpy(dest_span.data(), &value_, sizeof(ValueType));
}

/// Clockwork enum type
class ClkEnumType : public ClkType
{
public:
  /// Constructor, use from_proto to create an instance
  /// @param[in] factory Clockwork type factory
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_index Clockwork type index
  /// @param[in] value_type_index Value type index
  /// @param[in] version Enum version
  /// @param[in] uuid Enum UUID
  ClkEnumType(
    jewels::memory::ObjectPtr<ClkTypeFactory> factory,
    std::string_view fqn,
    size_t type_index,
    size_t value_type_index,
    int32_t version,
    const EnumUuid& uuid);

  ~ClkEnumType() override = default;

  ClkEnumType(const ClkEnumType&) = delete;
  ClkEnumType& operator=(const ClkEnumType&) = delete;
  ClkEnumType(ClkEnumType&&) = delete;
  ClkEnumType& operator=(ClkEnumType&&) = delete;

  /// @return Type size in bytes
  [[nodiscard]] size_t get_size() const noexcept override;

  /// @return Type alignment in bytes
  [[nodiscard]] size_t get_alignment() const noexcept override;

  /// @return Value type
  [[nodiscard]] const ClkType& get_value_type() const;

  /// @return Value type
  [[nodiscard]] ClkType& get_value_type();

  /// @return Enum version
  [[nodiscard]] int32_t get_version() const noexcept;

  /// @return Enum UUID
  [[nodiscard]] const EnumUuid& get_uuid() const noexcept;

  /// @return Enum value map
  [[nodiscard]] const std::pmr::unordered_map<int32_t, ClkEnumValue>& get_values() const noexcept;

  /// @return Enum value map
  [[nodiscard]] std::pmr::unordered_map<int32_t, ClkEnumValue>& get_values() noexcept;

  /// @return Set of values that have been removed from the current enum
  [[nodiscard]] const std::pmr::set<int32_t>& get_removed() const noexcept;

  /// @return Set of values that have been removed from the current enum
  [[nodiscard]] std::pmr::set<int32_t>& get_removed() noexcept;

  /// @return Map from old to new value numbers for values modified in the current enum
  [[nodiscard]] const std::pmr::map<int32_t, int32_t>& get_became() const noexcept;

  /// @return Map from old to new value numbers for values modified in the current enum
  [[nodiscard]] std::pmr::map<int32_t, int32_t>& get_became() noexcept;

  /// @return Enum options
  [[nodiscard]] const std::pmr::set<ClkEnumOption>& get_options() const noexcept;

  /// @return Enum options
  [[nodiscard]] std::pmr::set<ClkEnumOption>& get_options() noexcept;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;

  /// @see ClkType::is_legacy_wire_compatible
  [[nodiscard]] bool is_legacy_wire_compatible(const ClkType& src_type) const override;

  /// @see ClkType::check_for_unexpected_schema_changes
  void check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name) override;

  /// @see ClkType::is_same_type
  [[nodiscard]] bool is_same_type(const ClkType& src_type) const override;

private:
  /// Check for unexpected changes to the enum values
  /// @param[in] src_enum Source enum type
  /// @throws runtime_error on unexpected value changes
  void check_for_unexpected_value_changes(const ClkEnumType& src_enum);

  /// Representations for the types in the protobuf schema
  jewels::memory::ObjectPtr<ClkTypeFactory> factory_;

  /// Value type index
  size_t value_type_index_;

  /// Value type
  jewels::memory::ObjectPtr<ClkType> value_type_;

  /// Enum version
  int32_t version_;

  /// Enum UUID
  EnumUuid uuid_;

  /// Map from value number to value definition
  std::pmr::unordered_map<int32_t, ClkEnumValue> values_;

  /// Enum options
  std::pmr::set<ClkEnumOption> options_;

  /// Set of values that have been removed from the current enum
  std::pmr::set<int32_t> removed_;

  /// Map from old to new field numbers for fields modified in the current schema
  std::pmr::map<int32_t, int32_t> became_;

  /// Cached results of checks whether to use memcpy for upgrade
  std::pmr::unordered_map<size_t, bool> use_memcpy_cache_;

  /// Cache of checks for unexpected schema changes
  std::pmr::unordered_set<size_t> unexpected_schema_changes_cache_;
};

ClkEnumType::ClkEnumType(
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  std::string_view fqn,
  size_t type_index,
  size_t value_type_index,
  int32_t version,
  const EnumUuid& uuid)
  : ClkType(factory->get_memory_resource(), fqn, ClkTypeId::clk_enum, type_index, factory->get_metadata_version()),
    factory_(factory),
    value_type_index_(value_type_index),
    value_type_(factory_->get_clk_type(value_type_index_)),
    version_(version),
    uuid_(uuid),
    values_(factory->get_memory_resource()),
    options_(factory->get_memory_resource()),
    became_(factory->get_memory_resource()),
    use_memcpy_cache_(factory->get_memory_resource()),
    unexpected_schema_changes_cache_(factory->get_memory_resource())
{
}

[[nodiscard]] size_t ClkEnumType::get_size() const noexcept
{
  return get_value_type().get_size();
}

[[nodiscard]] size_t ClkEnumType::get_alignment() const noexcept
{
  return get_value_type().get_alignment();
}

[[nodiscard]] const ClkType& ClkEnumType::get_value_type() const
{
  return *value_type_;
}

[[nodiscard]] ClkType& ClkEnumType::get_value_type()
{
  return *value_type_;
}

[[nodiscard]] int32_t ClkEnumType::get_version() const noexcept
{
  return version_;
}

[[nodiscard]] const EnumUuid& ClkEnumType::get_uuid() const noexcept
{
  return uuid_;
}

[[nodiscard]] const std::pmr::unordered_map<int32_t, ClkEnumValue>& ClkEnumType::get_values() const noexcept
{
  return values_;
}

[[nodiscard]] std::pmr::unordered_map<int32_t, ClkEnumValue>& ClkEnumType::get_values() noexcept
{
  return values_;
}

[[nodiscard]] const std::pmr::set<int32_t>& ClkEnumType::get_removed() const noexcept
{
  return removed_;
}

[[nodiscard]] std::pmr::set<int32_t>& ClkEnumType::get_removed() noexcept
{
  return removed_;
}

[[nodiscard]] const std::pmr::map<int32_t, int32_t>& ClkEnumType::get_became() const noexcept
{
  return became_;
}

[[nodiscard]] std::pmr::map<int32_t, int32_t>& ClkEnumType::get_became() noexcept
{
  return became_;
}

[[nodiscard]] const std::pmr::set<ClkEnumOption>& ClkEnumType::get_options() const noexcept
{
  return options_;
}

[[nodiscard]] std::pmr::set<ClkEnumOption>& ClkEnumType::get_options() noexcept
{
  return options_;
}

[[nodiscard]] bool ClkEnumType::use_memcpy_for_array_upgrade(const ClkType& src_type)
{
  if (const auto cache_iter = use_memcpy_cache_.find(src_type.get_type_index()); cache_iter != use_memcpy_cache_.end())
  {
    return cache_iter->second;
  }
  if (src_type.get_type_id() != ClkTypeId::clk_enum)
  {
    use_memcpy_cache_.emplace(src_type.get_type_index(), false);
    return false;
  }
  const auto& src_enum = dynamic_cast<const ClkEnumType&>(src_type);
  if (
    src_enum.get_uuid() != get_uuid() || src_enum.get_version() != get_version() ||
    src_enum.get_values().size() != values_.size())
  {
    use_memcpy_cache_.emplace(src_type.get_type_index(), false);
    return false;
  }
  for (const auto& src_value : std::ranges::views::values(src_enum.get_values()))
  {
    const auto values_iter = values_.find(src_value.get_num());
    if (values_iter == values_.end())
    {
      use_memcpy_cache_.emplace(src_type.get_type_index(), false);
      return false;
    }
    if (src_value != values_iter->second)
    {
      use_memcpy_cache_.emplace(src_type.get_type_index(), false);
      return false;
    }
  }
  use_memcpy_cache_.emplace(src_type.get_type_index(), true);
  return true;
}

[[nodiscard]] bool ClkEnumType::is_legacy_wire_compatible(const ClkType& src_type) const
{
  if (src_type.get_type_id() != ClkTypeId::clk_enum)
  {
    return false;
  }
  const auto& src_enum_type = dynamic_cast<const ClkEnumType&>(src_type);
  return options_ == src_enum_type.options_ && value_type_index_ == src_enum_type.value_type_index_ &&
         uuid_ == src_enum_type.uuid_ && version_ == src_enum_type.version_ && values_ == src_enum_type.values_;
}

void ClkEnumType::check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name)
{
  if (const auto inserted = unexpected_schema_changes_cache_.insert(src_type.get_type_index()).second; !inserted)
  {
    return;
  }
  if (src_type.get_type_id() != ClkTypeId::clk_enum)
  {
    if (!allow_changes)
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Cannot upgrade {} from non-enum type {} ({}) to {}",
          name,
          src_type.get_fqn(),
          src_type.get_type_id(),
          get_fqn()));
    }
    return;
  }
  auto& src_enum = dynamic_cast<ClkEnumType&>(src_type);
  if (src_enum.get_uuid() != get_uuid())
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Cannot upgrade {} from {} with UUID {} to {} with UUID {}",
        name,
        src_type.get_fqn(),
        src_enum.get_uuid(),
        get_fqn(),
        get_uuid()));
  }
  if (src_enum.get_version() > get_version())
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Cannot upgrade {} from {} with version {} to {} with version {}",
        name,
        src_enum.get_fqn(),
        src_enum.get_version(),
        get_fqn(),
        get_version()));
  }
  get_value_type().check_for_unexpected_schema_changes(
    src_enum.get_value_type(), src_enum.get_version() != get_version(), get_fqn());
  check_for_unexpected_value_changes(src_enum);
  check_for_unexpected_history_changes(
    get_memory_resource(),
    src_enum.get_became(),
    src_enum.get_removed(),
    became_,
    removed_,
    src_enum.get_version() != version_,
    get_fqn());
  if (src_enum.get_version() == version_ && src_enum.get_options() != options_)
  {
    throw ClkTypeUpgradeError(fmt::format("Unsupported change to enum options for {} without changing version", name));
  }
}

void ClkEnumType::check_for_unexpected_value_changes(const ClkEnumType& src_enum)
{
  const auto values_view = std::ranges::views::keys(values_);
  std::pmr::unordered_set<int32_t> added_values(get_memory_resource());
  added_values.insert(values_view.begin(), values_view.end());
  for (const auto& src_value : std::ranges::views::values(src_enum.get_values()))
  {
    int32_t dest_value_num = src_value.get_num();
    while (became_.contains(dest_value_num))
    {
      dest_value_num = became_.at(dest_value_num);
      if (src_enum.get_version() == version_)
      {
        throw ClkTypeUpgradeError(
          fmt::format(
            "Value {} ({}) modified without changing schema version",
            src_value.get_num(),
            src_value.get_name(),
            get_fqn()));
      }
    }
    if (removed_.contains(dest_value_num))
    {
      if (src_enum.get_version() == version_)
      {
        throw ClkTypeUpgradeError(
          fmt::format(
            "Value {} ({}) removed from {} without changing schema version",
            src_value.get_num(),
            src_value.get_name(),
            get_fqn()));
      }
      continue;
    }
    if (!values_.contains(dest_value_num))
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Value {} ({}) removed from {} without updating history",
          src_value.get_num(),
          src_value.get_name(),
          get_fqn()));
    }
    added_values.erase(dest_value_num);
    const auto& dest_value = values_.at(dest_value_num);
    if (src_value.get_num() == dest_value.get_num())
    {
      if (src_enum.get_version() == version_ && src_value.get_name() != dest_value.get_name())
      {
        throw ClkTypeUpgradeError(
          fmt::format(
            "Value {} renamed to {} in {} without changing schema version",
            src_value.get_name(),
            dest_value.get_name(),
            get_fqn()));
      }
      if (src_enum.get_version() == version_ && src_value.get_value() != dest_value.get_value())
      {
        throw ClkTypeUpgradeError(
          fmt::format(
            "Value for {} changed from {} to {} in {} without changing schema version",
            dest_value.get_name(),
            src_value.get_value(),
            dest_value.get_value(),
            get_fqn()));
      }
    }
  }
  if (
    !added_values.empty() && src_enum.get_version() == version_ &&
    get_metadata_version() >= enforce_version_change_when_adding_fields_and_values_version)
  {
    const auto& dest_value = values_.at(*added_values.begin());
    throw ClkTypeUpgradeError(
      fmt::format(
        "Value {} ({}) added to {} without changing schema version",
        dest_value.get_num(),
        dest_value.get_name(),
        get_fqn()));
  }
}

[[nodiscard]] bool ClkEnumType::is_same_type(const ClkType& src_type) const
{
  if (src_type.get_type_id() != ClkTypeId::clk_enum)
  {
    return false;
  }
  const auto& src_enum_type = dynamic_cast<const ClkEnumType&>(src_type);
  return src_enum_type.get_uuid() == get_uuid();
}

/// Clockwork enum type implementation
/// @tparam ValueType Enum value type
template <typename ValueType>
class ClkEnumTypeImpl : public ClkEnumType
{
public:
  using ClkEnumType::ClkEnumType;

  ~ClkEnumTypeImpl() override = default;

  ClkEnumTypeImpl(const ClkEnumTypeImpl&) = delete;
  ClkEnumTypeImpl& operator=(const ClkEnumTypeImpl&) = delete;
  ClkEnumTypeImpl(ClkEnumTypeImpl&&) = delete;
  ClkEnumTypeImpl& operator=(ClkEnumTypeImpl&&) = delete;

  /// Make an initializer function to initialize this type to the specified value
  /// @param[in] value Initial enum value
  /// @return Initializer function
  [[nodiscard]] std::shared_ptr<ClkValueInitializer>
  make_value_initializer(const metadata::InitialValue& value) const override;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

private:
  /// Create an upgrader to upgrade an enum from another enum type
  /// @tparam SrcValueType Source enum value type
  /// @param[in] src_type Source type
  /// @return Upgrader from src_type to this type
  /// @throws runtime_error on failure
  template <typename SrcValueType>
  [[nodiscard]] std::shared_ptr<ClkTypeUpgrader>
  make_enum_type_upgrader(const ClkEnumTypeImpl<SrcValueType>& src_type) const;

  /// Create an upgrader to upgrade an enum from a primitive integer type
  /// @tparam SrcValueType Source integer value type
  /// @param src_fqn Source type fully qualied name
  /// @return Upgrader from a primitive integer value to this type
  /// @throws runtime_error on failure
  template <typename SrcValueType>
  [[nodiscard]] std::shared_ptr<ClkTypeUpgrader> make_integer_type_upgrader(std::string_view src_fqn) const;
};

template <typename ValueType>
[[nodiscard]] std::shared_ptr<ClkValueInitializer>
ClkEnumTypeImpl<ValueType>::make_value_initializer(const metadata::InitialValue& value) const
{
  if constexpr (std::is_unsigned_v<ValueType>)
  {
    return jewels::memory::make_pmr_shared<ClkEnumInitializer<ValueType>>(
      get_memory_resource(), static_cast<ValueType>(value.unsigned_value()));
  }
  return jewels::memory::make_pmr_shared<ClkEnumInitializer<ValueType>>(
    get_memory_resource(), static_cast<ValueType>(value.signed_value()));
}

template <typename ValueType>
[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader>
ClkEnumTypeImpl<ValueType>::make_upgrader(const ClkType& src_type)
{
  auto& upgrader_cache = get_upgrader_cache();
  if (const auto cache_iter = upgrader_cache.find(src_type.get_type_index()); cache_iter != upgrader_cache.end())
  {
    return jewels::memory::make_non_null_from_ref(*cache_iter->second);
  }
  if (use_memcpy_for_array_upgrade(src_type))
  {
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkMemcpyUpgrader>(get_memory_resource(), get_size()))
         .first->second);
  }
  if (src_type.get_type_id() != ClkTypeId::clk_enum)
  {
    switch (src_type.get_type_id())
    {
    case ClkTypeId::int8:
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache.emplace(src_type.get_type_index(), make_integer_type_upgrader<int8_t>(src_type.get_fqn()))
           .first->second);
    case ClkTypeId::int16:
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache.emplace(src_type.get_type_index(), make_integer_type_upgrader<int16_t>(src_type.get_fqn()))
           .first->second);
    case ClkTypeId::int32:
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache.emplace(src_type.get_type_index(), make_integer_type_upgrader<int32_t>(src_type.get_fqn()))
           .first->second);
    case ClkTypeId::int64:
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache.emplace(src_type.get_type_index(), make_integer_type_upgrader<int64_t>(src_type.get_fqn()))
           .first->second);
    case ClkTypeId::uint8:
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache.emplace(src_type.get_type_index(), make_integer_type_upgrader<uint8_t>(src_type.get_fqn()))
           .first->second);
    case ClkTypeId::uint16:
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache.emplace(src_type.get_type_index(), make_integer_type_upgrader<uint16_t>(src_type.get_fqn()))
           .first->second);
    case ClkTypeId::uint32:
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache.emplace(src_type.get_type_index(), make_integer_type_upgrader<uint32_t>(src_type.get_fqn()))
           .first->second);
    case ClkTypeId::uint64:
      return jewels::memory::make_non_null_from_ref(
        *upgrader_cache.emplace(src_type.get_type_index(), make_integer_type_upgrader<uint64_t>(src_type.get_fqn()))
           .first->second);
    default:
      throw ClkTypeUpgradeError(
        fmt::format(
          "Cannot upgrade from non-enum type {} ({}) to {}", src_type.get_fqn(), src_type.get_type_id(), get_fqn()));
    }
  }
  const auto& src_enum = dynamic_cast<const ClkEnumType&>(src_type);
  if (src_enum.get_uuid() != get_uuid())
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Cannot upgrade from {} with UUID {} to {} with UUID {}",
        src_type.get_fqn(),
        src_enum.get_uuid(),
        get_fqn(),
        get_uuid()));
  }
  if (src_enum.get_version() > get_version())
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Cannot upgrade from {} with version {} to {} with version {}",
        src_enum.get_fqn(),
        src_enum.get_version(),
        get_fqn(),
        get_version()));
  }
  switch (src_enum.get_value_type().get_type_id())
  {
  case ClkTypeId::int8:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(), make_enum_type_upgrader(dynamic_cast<const ClkEnumTypeImpl<int8_t>&>(src_enum)))
         .first->second);
  case ClkTypeId::int16:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(), make_enum_type_upgrader(dynamic_cast<const ClkEnumTypeImpl<int16_t>&>(src_enum)))
         .first->second);
  case ClkTypeId::int32:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(), make_enum_type_upgrader(dynamic_cast<const ClkEnumTypeImpl<int32_t>&>(src_enum)))
         .first->second);
  case ClkTypeId::int64:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(), make_enum_type_upgrader(dynamic_cast<const ClkEnumTypeImpl<int64_t>&>(src_enum)))
         .first->second);
  case ClkTypeId::uint8:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(), make_enum_type_upgrader(dynamic_cast<const ClkEnumTypeImpl<uint8_t>&>(src_enum)))
         .first->second);
  case ClkTypeId::uint16:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(), make_enum_type_upgrader(dynamic_cast<const ClkEnumTypeImpl<uint16_t>&>(src_enum)))
         .first->second);
  case ClkTypeId::uint32:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(), make_enum_type_upgrader(dynamic_cast<const ClkEnumTypeImpl<uint32_t>&>(src_enum)))
         .first->second);
  case ClkTypeId::uint64:
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache
         .emplace(
           src_type.get_type_index(), make_enum_type_upgrader(dynamic_cast<const ClkEnumTypeImpl<uint64_t>&>(src_enum)))
         .first->second);
  default:
    throw ClkTypeUpgradeError(fmt::format("Invalid enum value type ({})", src_enum.get_value_type().get_type_id()));
  }
}

template <typename ValueType>
template <typename SrcValueType>
[[nodiscard]] std::shared_ptr<ClkTypeUpgrader>
ClkEnumTypeImpl<ValueType>::make_enum_type_upgrader(const ClkEnumTypeImpl<SrcValueType>& src_type) const
{
  std::pmr::unordered_map<SrcValueType, ValueType> value_map(get_memory_resource());
  for (const auto& src_value : std::ranges::views::values(src_type.get_values()))
  {
    int32_t dest_value_num = src_value.get_num();
    while (get_became().contains(dest_value_num))
    {
      dest_value_num = get_became().at(dest_value_num);
    }
    if (get_removed().contains(dest_value_num))
    {
      continue;
    }
    if (!get_values().contains(dest_value_num))
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Value {} ({}) removed from {} without updating history",
          src_value.get_num(),
          src_value.get_name(),
          get_fqn()));
    }
    const auto& dest_value = get_values().at(dest_value_num);
    value_map.emplace(static_cast<SrcValueType>(src_value.get_value()), static_cast<ValueType>(dest_value.get_value()));
  }
  if (src_type.get_options().contains(ClkEnumOption::flag))
  {
    if (get_options().contains(ClkEnumOption::flag))
    {
      return jewels::memory::make_pmr_shared<ClkBitFlagToBitFlagEnumUpgrader<SrcValueType, ValueType>>(
        get_memory_resource(), get_memory_resource(), src_type.get_fqn(), value_map);
    }
    return jewels::memory::make_pmr_shared<ClkBitFlagToValueEnumUpgrader<SrcValueType, ValueType>>(
      get_memory_resource(), get_memory_resource(), src_type.get_fqn(), value_map);
  }
  return jewels::memory::make_pmr_shared<ClkValueEnumUpgrader<SrcValueType, ValueType>>(
    get_memory_resource(), get_memory_resource(), src_type.get_fqn(), std::move(value_map));
}

template <typename ValueType>
template <typename SrcValueType>
[[nodiscard]] std::shared_ptr<ClkTypeUpgrader>
ClkEnumTypeImpl<ValueType>::make_integer_type_upgrader(std::string_view src_fqn) const
{
  std::pmr::unordered_map<SrcValueType, ValueType> value_map(get_memory_resource());
  for (const auto& value : std::ranges::views::values(get_values()))
  {
    value_map.emplace(static_cast<SrcValueType>(value.get_value()), static_cast<ValueType>(value.get_value()));
  }
  if (get_options().contains(ClkEnumOption::flag))
  {
    return jewels::memory::make_pmr_shared<ClkBitFlagToBitFlagEnumUpgrader<SrcValueType, ValueType>>(
      get_memory_resource(), get_memory_resource(), src_fqn, value_map);
  }
  return jewels::memory::make_pmr_shared<ClkValueEnumUpgrader<SrcValueType, ValueType>>(
    get_memory_resource(), get_memory_resource(), src_fqn, std::move(value_map));
}

/// Create a clockwork enum type from an enum protobuf
template <typename ValueType>
std::shared_ptr<ClkType> make_clk_enum_type_from_proto(
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  const metadata::ClkEnumType& enum_proto,
  size_t type_index,
  std::optional<std::string_view> maybe_strong_type_fqn)
{
  if (enum_proto.enum_uuid().size() != sizeof(EnumUuid))
  {
    throw ClkTypeUpgradeError(
      fmt::format("Invalid UUID size: got {} bytes expected {}", enum_proto.enum_uuid().size(), sizeof(EnumUuid)));
  }
  EnumUuid uuid{};
  std::memcpy(uuid.uuid.data(), enum_proto.enum_uuid().data(), sizeof(EnumUuid));
  const auto value_type_index = static_cast<size_t>(enum_proto.underlying_type_id());
  auto clk_enum = jewels::memory::make_pmr_shared<ClkEnumTypeImpl<ValueType>>(
    factory->get_memory_resource(),
    factory,
    maybe_strong_type_fqn.value_or(enum_proto.fqn()),
    type_index,
    value_type_index,
    enum_proto.version(),
    uuid);
  for (const auto& value_proto : enum_proto.values())
  {
    clk_enum->get_values().emplace(
      std::piecewise_construct,
      std::forward_as_tuple(value_proto.num()),
      std::forward_as_tuple(
        value_proto.num(), value_proto.value(), std::pmr::string{value_proto.name(), factory->get_memory_resource()}));
  }
  if ((static_cast<std::make_unsigned_t<int32_t>>(enum_proto.options()) & 1U) != 0U)
  {
    clk_enum->get_options().emplace(ClkEnumOption::flag);
  }
  if (enum_proto.has_history())
  {
    for (const auto [from_num, to_num] : enum_proto.history().became())
    {
      clk_enum->get_became()[from_num] = to_num;
    }
    for (const auto removed_num : enum_proto.history().removed())
    {
      clk_enum->get_removed().insert(removed_num);
    }
  }
  return clk_enum;
}

} // namespace

// NOLINTNEXTLINE(misc-no-recursion) Types are defined recursively
[[nodiscard]] std::shared_ptr<ClkType> ClkEnumTypeFactoryPlugin::make_clk_type(
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  const metadata::TypeDesc& type_proto,
  size_t type_index,
  std::optional<std::string_view> maybe_strong_type_fqn) const
{
  if (!type_proto.has_clk_enum())
  {
    return nullptr;
  }
  const auto& enum_proto = type_proto.clk_enum();
  const auto value_type_index = static_cast<size_t>(enum_proto.underlying_type_id());
  const auto& value_type = *factory->get_clk_type(value_type_index);
  switch (value_type.get_type_id())
  {
  case ClkTypeId::int8:
    return make_clk_enum_type_from_proto<int8_t>(factory, enum_proto, type_index, maybe_strong_type_fqn);
  case ClkTypeId::int16:
    return make_clk_enum_type_from_proto<int16_t>(factory, enum_proto, type_index, maybe_strong_type_fqn);
  case ClkTypeId::int32:
    return make_clk_enum_type_from_proto<int32_t>(factory, enum_proto, type_index, maybe_strong_type_fqn);
  case ClkTypeId::int64:
    return make_clk_enum_type_from_proto<int64_t>(factory, enum_proto, type_index, maybe_strong_type_fqn);
  case ClkTypeId::uint8:
    return make_clk_enum_type_from_proto<uint8_t>(factory, enum_proto, type_index, maybe_strong_type_fqn);
  case ClkTypeId::uint16:
    return make_clk_enum_type_from_proto<uint16_t>(factory, enum_proto, type_index, maybe_strong_type_fqn);
  case ClkTypeId::uint32:
    return make_clk_enum_type_from_proto<uint32_t>(factory, enum_proto, type_index, maybe_strong_type_fqn);
  case ClkTypeId::uint64:
    return make_clk_enum_type_from_proto<uint64_t>(factory, enum_proto, type_index, maybe_strong_type_fqn);
  default:
    throw ClkTypeUpgradeError(fmt::format("Invalid enum value type ({})", value_type.get_type_id()));
  }
}

} // namespace clockwork::serialization
