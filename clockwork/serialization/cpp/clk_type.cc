// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/serialization/cpp/clk_type.hh"

#include "jewels/memory/pointers.hh"

#include <fmt/format.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace clockwork::serialization
{

ClkType::ClkType(std::string_view fqn, ClkTypeId type_id, size_t type_index)
  : fqn_(fqn), type_id_(type_id), type_index_(type_index)
{
}

[[nodiscard]] std::string_view ClkType::get_fqn() const noexcept
{
  return fqn_;
}

[[nodiscard]] ClkTypeId ClkType::get_type_id() const noexcept
{
  return type_id_;
}

[[nodiscard]] size_t ClkType::get_type_index() const noexcept
{
  return type_index_;
}

[[nodiscard]] std::unique_ptr<ClkValueInitializer>
ClkType::make_value_initializer(const metadata::InitialValue& /*value*/) const
{
  throw ClkTypeUpgradeError(fmt::format("Value initializers are not supported for type {} ({})", fqn_, type_id_));
}

[[nodiscard]] std::optional<jewels::memory::ObjectPtr<const ClkValueInitializer>> ClkType::make_initializer()
{
  return std::nullopt;
}

[[nodiscard]] bool ClkType::use_memcpy_for_array_upgrade(const ClkType& /*src_type*/)
{
  return false;
}

void validate_array_size(size_t array_size, size_t min_array_size, size_t max_array_size)
{
  if (array_size < min_array_size)
  {
    throw ClkTypeUpgradeError(
      fmt::format("Array size ({}) is less than the minimum array size ({})", array_size, min_array_size));
  }
  if (array_size > max_array_size)
  {
    throw ClkTypeUpgradeError(fmt::format("Array size ({}) exceeds max array size ({})", array_size, max_array_size));
  }
}

ClkMemcpyUpgrader::ClkMemcpyUpgrader(size_t size) noexcept
  : size_(size)
{
}

void ClkMemcpyUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  std::memcpy(dest_span.data(), src_span.data(), size_);
}

ClkMemcpyArrayUpgrader::ClkMemcpyArrayUpgrader(size_t min_array_size, size_t max_array_size, size_t element_size)
  : min_array_size_(min_array_size), max_array_size_(max_array_size), element_size_(element_size)
{
}

void ClkMemcpyArrayUpgrader::upgrade(
  size_t src_array_size, std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  validate_array_size(src_array_size, min_array_size_, max_array_size_);
  std::memcpy(dest_span.data(), src_span.data(), element_size_ * src_array_size);
}

ClkTypeArrayUpgrader::ClkTypeArrayUpgrader(
  size_t min_array_size,
  size_t max_array_size,
  size_t src_element_size,
  size_t dest_element_size,
  jewels::memory::ObjectPtr<const ClkTypeUpgrader> upgrader)
  : min_array_size_(min_array_size),
    max_array_size_(max_array_size),
    src_element_size_(src_element_size),
    dest_element_size_(dest_element_size),
    upgrader_(upgrader)
{
}

void ClkTypeArrayUpgrader::upgrade(
  size_t src_array_size, std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  validate_array_size(src_array_size, min_array_size_, max_array_size_);
  size_t src_offset = 0U;
  size_t dest_offset = 0U;
  for (size_t i = 0U; i < src_array_size; ++i)
  {
    upgrader_->upgrade(
      src_span.subspan(src_offset, src_element_size_), dest_span.subspan(dest_offset, dest_element_size_));
    src_offset += src_element_size_;
    dest_offset += dest_element_size_;
  }
}

[[nodiscard]] std::unique_ptr<ClkArrayUpgrader>
ClkType::make_array_upgrader(const ClkType& src_type, size_t min_array_size, size_t max_array_size)
{
  if (use_memcpy_for_array_upgrade(src_type))
  {
    return std::make_unique<ClkMemcpyArrayUpgrader>(min_array_size, max_array_size, get_size());
  }
  auto element_upgrader = make_upgrader(src_type);
  return std::make_unique<ClkTypeArrayUpgrader>(
    min_array_size, max_array_size, src_type.get_size(), get_size(), element_upgrader);
}

[[nodiscard]] std::unordered_map<size_t, jewels::memory::NonNullSharedPtr<const ClkTypeUpgrader>>&
ClkType::get_upgrader_cache()
{
  return upgrader_cache_;
}

[[nodiscard]] ClkType& ClkType::get_lowest_underlying_type()
{
  return *this;
}

[[nodiscard]] bool ClkType::is_same_type(const ClkType& src_type) const
{
  return src_type.type_id_ == type_id_;
}

ClkTypeFactory::ClkTypeFactory(
  std::unique_ptr<const metadata::TachyonMetadata> metadata_proto,
  std::vector<std::unique_ptr<ClkTypeFactoryPlugin>> plugins)
  : metadata_proto_(std::move(metadata_proto)),
    plugins_(std::move(plugins)),
    types_(static_cast<size_t>(metadata_proto_->types_size()))
{
}

[[nodiscard]] jewels::memory::ObjectPtr<ClkType> ClkTypeFactory::get_clk_type(size_t type_index)
{
  if (types_.at(type_index) != nullptr)
  {
    return jewels::memory::make_non_null_from_ref(*types_.at(type_index));
  }
  auto type_proto_ptr =
    jewels::memory::make_non_null_from_ref(metadata_proto_->types(static_cast<int32_t>(type_index)));
  std::optional<std::string_view> maybe_strong_type_fqn = std::nullopt;
  if (type_proto_ptr->has_strong_type())
  {
    maybe_strong_type_fqn = type_proto_ptr->strong_type().fqn();
    type_proto_ptr = jewels::memory::make_non_null_from_ref(
      metadata_proto_->types(type_proto_ptr->strong_type().underlying_type_id()));
  }
  for (const auto& plugin : plugins_)
  {
    if (auto clk_type = plugin->make_clk_type(
          jewels::memory::make_non_null_from_ref(*this), *type_proto_ptr, type_index, maybe_strong_type_fqn);
        clk_type != nullptr)
    {
      types_.at(type_index) = std::move(clk_type);
      return jewels::memory::make_non_null_from_ref(*types_.at(type_index));
    }
  }
  throw ClkTypeUpgradeError(fmt::format("Invalid clockwork schema protobuf at index {}", type_index));
}

[[nodiscard]] const metadata::TachyonMetadata& ClkTypeFactory::get_metadata_proto() const noexcept
{
  return *metadata_proto_;
}

[[nodiscard]] std::vector<std::unique_ptr<ClkType>>& ClkTypeFactory::get_types() noexcept
{
  return types_;
}

void check_for_unexpected_history_changes(
  const std::map<int32_t, int32_t>& src_became,
  const std::set<int32_t>& src_removed,
  const std::map<int32_t, int32_t>& dst_became,
  const std::set<int32_t>& dst_removed,
  bool allow_changes,
  std::string_view name)
{
  for (const auto [old_number, new_number] : src_became)
  {
    if (!dst_became.contains(old_number))
    {
      throw ClkTypeUpgradeError(
        fmt::format("Unsupported deletion of legacy_became entry for field {} in {}", old_number, name));
    }
    if (dst_became.at(old_number) != new_number)
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Unsupported modification of legacy_became entry in {}, {}->{} to {}->{}",
          name,
          old_number,
          new_number,
          old_number,
          dst_became.at(old_number)));
    }
  }
  const auto dst_became_values = std::ranges::views::values(dst_became);
  std::unordered_set<int32_t> dst_became_targets(dst_became_values.begin(), dst_became_values.end());
  for (const auto old_number : src_removed)
  {
    // Special case: allow deleting a removed entry field when that field is the source or target of a legacy_became
    //
    // This happens when someone deletes a field and then creates a new field when they meant to change the
    // type and want to recover while remaining compatible with both new and old logs.
    //
    // This also happen with someone deletes a field and then wants to recover by putting the field back
    // with a new number and adding a became from the old number to the new number.
    if (
      !dst_removed.contains(old_number) && !dst_became_targets.contains(old_number) && !dst_became.contains(old_number))
    {
      throw ClkTypeUpgradeError(
        fmt::format("Unsupported deletion of removed entry for field {} in {}", old_number, name));
    }
  }
  if (!allow_changes && src_removed != dst_removed)
  {
    throw ClkTypeUpgradeError(fmt::format("Unexpected change to removed in history for {}", name));
  }
  if (!allow_changes && src_became != dst_became)
  {
    throw ClkTypeUpgradeError(fmt::format("Unexpected change to legacy_became in history for {}", name));
  }
}

} // namespace clockwork::serialization
