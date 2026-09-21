// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/clk_type.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "jewels/hash/md5.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace clockwork::serialization
{

/// Class to represent the serialized tachyon metadata in C++
class TachyonModel
{
public:
  /// Constructor, use from_proto to make a new instance
  /// @param[in] outer_type_id Index of the schema's outer type
  /// @param[in] types_size Number of types in the metadata
  TachyonModel(size_t outer_type_id, std::shared_ptr<ClkTypeFactory> factory);

  ~TachyonModel() noexcept = default;

  TachyonModel(const TachyonModel&) = delete;
  TachyonModel& operator=(const TachyonModel&) = delete;
  TachyonModel(TachyonModel&&) = delete;
  TachyonModel& operator=(TachyonModel&&) = delete;

  /// Create an instance from a tachyon metadata protobuf
  /// @param[in] memory_resource Memory resource
  /// @param[in] metadata_proto Tachyon metadata protobuf
  /// @returns Tachyon model instance
  /// @throws runtime_error on failure.
  [[nodiscard]] static std::shared_ptr<TachyonModel> from_proto(
    const jewels::memory::MemoryResource& memory_resource,
    const std::shared_ptr<metadata::TachyonMetadata>& metadata_proto);

  /// Create an instance from a tachyon metadata protobuf
  /// @param[in] metadata_proto Tachyon metadata protobuf
  /// @returns Tachyon model instance
  /// @throws runtime_error on failure.
  [[nodiscard]] static std::shared_ptr<TachyonModel>
  from_proto(const jewels::memory::MemoryResource& memory_resource, const metadata::TachyonMetadata& metadata_proto);

  /// Create an instance from a tachyon type
  /// @tparam T Clockwork type
  /// @param[in] memory_resource Memory resource
  /// @returns Tachyon model instance
  /// @throws runtime_error on failure.
  template <typename T>
  [[nodiscard]] static std::shared_ptr<TachyonModel> from_type(const jewels::memory::MemoryResource& memory_resource)
    requires(TachyonType<T> || TappyType<T>);

  /// Outer type accessor
  [[nodiscard]] const ClkType& get_outer_type() const;

  /// Outer type accessor
  [[nodiscard]] ClkType& get_outer_type();

  /// Get the metadata protobuf version
  [[nodiscard]] int32_t get_metadata_version() const;

  /// Get the metadata protobuf hash
  [[nodiscard]] std::span<const std::byte> get_metadata_hash() const;

  /// Compute the metadata protobuf hash for comparing two protobufs with different versions
  [[nodiscard]] std::span<const std::byte> compute_metadata_hash();

  /// Test if this instance is wire compatible with another instance
  ///
  /// The schema hashes are used to check for compatibility.
  ///
  /// @param[in] other Instance to check for compatibility
  /// @return True if this instance is wire compatible with the other
  [[nodiscard]] bool is_wire_compatible(TachyonModel& other);

  /// Make an upgrader from the other schema to this schema
  /// @param[in] other Schema to upgrade from
  /// @return Function to upgrade to this schema from the other type
  /// @throws runtime_error if unexpected schema changes are found
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(TachyonModel& other);

  /// Check for unexpected schema changes between this instance and another instance
  /// @param[in] other Schema to check
  /// @throws runtime_error if unexpected schema changes are found
  void check_for_unexpected_schema_changes(TachyonModel& other);

  /// Make a lite compressor for this schema
  /// @return Instance to find the chunks of zeros in the schema for lite-compression
  [[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> make_lite_compressor();

private:
  /// Index of the schema's outer type
  size_t outer_type_id_;

  /// Clockwork type factory for the types in the schema protobuf
  std::shared_ptr<ClkTypeFactory> factory_;

  /// Cached computed metadata hash value
  std::optional<jewels::hash::MD5HashValue> maybe_cached_metadata_hash_;
};

/// Validate that the logged channel schemas can be upgraded from a previous version to the current version
/// @param[in] prev_metadata Previous logged channel metadata
/// @param[in] curr_metadata Current logged channel metadata
/// @returns True if all metadata can up upgraded
[[nodiscard]] bool validate_logged_channel_metadata(
  const metadata::LoggedChannelMetadata& prev_metadata, const metadata::LoggedChannelMetadata& curr_metadata);

} // namespace clockwork::serialization

#include "clockwork/serialization/cpp/tachyon_model.inl"
