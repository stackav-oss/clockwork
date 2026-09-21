
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/serialization/cpp/clk_type.hh"

#include "clockwork/logging/nolint_helper.hh"
#include "jewels/aligner/aligner.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
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

namespace
{

/// Find the first zero in a span of uint64_t
/// @param[in] data Span of uint64_t
/// @param[in] start_index Search start index
/// @return Index of the first zero or the size of the span if no zero is found
[[nodiscard]] size_t find_first_zero(std::span<const uint64_t> data, size_t start_index)
{
  for (auto i = start_index; i < data.size(); ++i)
  {
    if (data[i] == 0U)
    {
      return i;
    }
  }
  return data.size();
}

/// Find the first non-zero in a span of uint64_t
/// @param[in] data Span of uint64_t
/// @param[in] start_index Search start index
/// @return Index of the first non-zero or the size of the span if no zero is found
[[nodiscard]] size_t find_first_non_zero(std::span<const uint64_t> data, size_t start_index)
{
  for (auto i = start_index; i < data.size(); ++i)
  {
    if (data[i] != 0U)
    {
      return i;
    }
  }
  return data.size();
}

} // namespace

using jewels::failure;
using jewels::InOut;
using jewels::ok;
using jewels::success;

jewels::BinaryOutcome ClkTypeLiteCompressor::compress(
  std::span<const std::byte> /*data_span*/,
  size_t /*offset*/,
  InOut<std::pmr::vector<ClkZeroChunk>> /*zero_chunks*/) const
{
  return success;
}

void ClkTypeLiteCompressor::compress_opaque_data(
  std::span<const std::byte> data_span, size_t offset, InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks)
{
  auto start_offset = static_cast<size_t>(jewels::Aligner<sizeof(uint64_t)>::ptr_aligned_remainder(data_span.data()));
  if (start_offset >= data_span.size())
  {
    return;
  }
  const auto aligned_data = clockwork_logging::nolint_helper::byte_span_to_value_span<uint64_t>(
    data_span.subspan(start_offset, (data_span.size() - start_offset) / sizeof(uint64_t) * sizeof(uint64_t)));
  auto aligned_offset = find_first_zero(aligned_data, 0U);
  while (aligned_offset < aligned_data.size())
  {
    const auto end_offset = find_first_non_zero(aligned_data, aligned_offset + 1U);
    zero_chunks->emplace_back(
      offset + start_offset + (aligned_offset * sizeof(uint64_t)), (end_offset - aligned_offset) * sizeof(uint64_t));
    aligned_offset = end_offset + 1U;
    if (aligned_offset < aligned_data.size())
    {
      aligned_offset = find_first_zero(aligned_data, aligned_offset);
    }
  }
}

ClkArrayLiteCompressor::ClkArrayLiteCompressor(
  size_t max_size, size_t element_size, std::shared_ptr<ClkTypeLiteCompressor> compressor)
  : max_size_(max_size), element_size_(element_size), compressor_(std::move(compressor))
{
}

jewels::BinaryOutcome ClkArrayLiteCompressor::compress(
  size_t size,
  std::span<const std::byte> data_span,
  size_t offset,
  InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const
{
  if (size > max_size_)
  {
    return failure;
  }
  if (!compressor_)
  {
    ClkTypeLiteCompressor::compress_opaque_data(
      data_span.subspan(0U, size * element_size_), offset, InOut{*zero_chunks});
  }
  else
  {
    size_t element_offset = 0U;
    for (size_t i = 0U; i < size; ++i)
    {
      if (!ok(compressor_->compress(
            data_span.subspan(element_offset, element_size_), offset + element_offset, InOut{*zero_chunks})))
      {
        return failure;
      }
      element_offset += element_size_;
    }
  }
  return success;
}

ClkType::ClkType(
  jewels::memory::MemoryResource memory_resource,
  std::string_view fqn,
  ClkTypeId type_id,
  size_t type_index,
  int32_t metadata_version)
  : memory_resource_(std::move(memory_resource)),
    fqn_(fqn, memory_resource_),
    type_id_(type_id),
    type_index_(type_index),
    metadata_version_(metadata_version),
    upgrader_cache_(memory_resource_)
{
}

[[nodiscard]] const jewels::memory::MemoryResource& ClkType::get_memory_resource() const noexcept
{
  return memory_resource_;
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

[[nodiscard]] int32_t ClkType::get_metadata_version() const noexcept
{
  return metadata_version_;
}

[[nodiscard]] std::shared_ptr<ClkValueInitializer>
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

[[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> ClkType::make_lite_compressor()
{
  if (!cached_lite_compressor_)
  {
    cached_lite_compressor_ = jewels::memory::make_pmr_shared<ClkTypeLiteCompressor>(memory_resource_);
  }
  return jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor>{cached_lite_compressor_};
}

[[nodiscard]] bool ClkType::is_compressible()
{
  if (!cached_is_compressible_.has_value())
  {
    cached_is_compressible_ = false;
  }
  return cached_is_compressible_.value();
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

[[nodiscard]] std::shared_ptr<ClkArrayUpgrader>
ClkType::make_array_upgrader(const ClkType& src_type, size_t min_array_size, size_t max_array_size)
{
  if (use_memcpy_for_array_upgrade(src_type))
  {
    return jewels::memory::make_pmr_shared<ClkMemcpyArrayUpgrader>(
      memory_resource_, min_array_size, max_array_size, get_size());
  }
  auto element_upgrader = make_upgrader(src_type);
  return jewels::memory::make_pmr_shared<ClkTypeArrayUpgrader>(
    memory_resource_, min_array_size, max_array_size, src_type.get_size(), get_size(), element_upgrader);
}

[[nodiscard]] std::pmr::unordered_map<size_t, jewels::memory::NonNullSharedPtr<const ClkTypeUpgrader>>&
ClkType::get_upgrader_cache()
{
  return upgrader_cache_;
}

[[nodiscard]] std::shared_ptr<ClkTypeLiteCompressor>& ClkType::get_cached_lite_compressor()
{
  return cached_lite_compressor_;
}

[[nodiscard]] std::optional<bool>& ClkType::get_cached_is_compressible()
{
  return cached_is_compressible_;
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
  jewels::memory::MemoryResource memory_resource,
  std::shared_ptr<const metadata::TachyonMetadata> metadata_proto,
  std::pmr::vector<std::shared_ptr<ClkTypeFactoryPlugin>> plugins)
  : memory_resource_(std::move(memory_resource)),
    metadata_proto_(std::move(metadata_proto)),
    plugins_(std::move(plugins)),
    types_(static_cast<size_t>(metadata_proto_->types_size()), memory_resource_)
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

[[nodiscard]] int32_t ClkTypeFactory::get_metadata_version() const noexcept
{
  return metadata_proto_->version();
}

[[nodiscard]] std::pmr::vector<std::shared_ptr<ClkType>>& ClkTypeFactory::get_types() noexcept
{
  return types_;
}

[[nodiscard]] const jewels::memory::MemoryResource& ClkTypeFactory::get_memory_resource() const noexcept
{
  return memory_resource_;
}

void check_for_unexpected_history_changes(
  const jewels::memory::MemoryResource& memory_resource,
  const std::pmr::map<int32_t, int32_t>& src_became,
  const std::pmr::set<int32_t>& src_removed,
  const std::pmr::map<int32_t, int32_t>& dst_became,
  const std::pmr::set<int32_t>& dst_removed,
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
  std::pmr::unordered_set<int32_t> dst_became_targets(memory_resource);
  dst_became_targets.insert(dst_became_values.begin(), dst_became_values.end());
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
