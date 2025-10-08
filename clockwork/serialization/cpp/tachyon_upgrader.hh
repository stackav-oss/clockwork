// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clockwork/repr_iface.hh"

#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace clockwork::serialization
{

/// Tachyon upgrader types
WISE_ENUM_CLASS(
  (TachyonUpgraderType, uint8_t),
  // Memcpy upgrader
  memcpy,
  // C++ upgrader
  cpp,
  // Python upgrader
  python)

/// Class to upgrade a tachyon type from a the source schema to the destination schema
class TachyonUpgrader
{
public:
  /// Constructor
  TachyonUpgrader() noexcept = default;

  virtual ~TachyonUpgrader() noexcept = default;

  TachyonUpgrader(const TachyonUpgrader&) = delete;
  TachyonUpgrader& operator=(const TachyonUpgrader&) = delete;
  TachyonUpgrader(TachyonUpgrader&&) noexcept = default;
  TachyonUpgrader& operator=(TachyonUpgrader&&) noexcept = default;

  /// @return True if upgrade is requred, if false the upgrader uses memcpy to upgrade
  [[nodiscard]] virtual bool upgrade_required() const = 0;

  /// @return Tachyon upgrader type
  [[nodiscard]] virtual TachyonUpgraderType upgrader_type() const = 0;

  /// Upgrade an instance of the source schema to an instance of the destination schema
  /// @param[in] src_span Source instance data span
  /// @param[in] dest_span Destination instance data span
  /// @throws runtime_error on failure
  virtual void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const = 0;
};

/// Create a C++ upgrader from a logged schema to the current schema
/// @param[in] current_class_name Current tachyon schema class name
/// @param[in] current_metadata Serialized metadata for the current schema
/// @param[in] incoming_metadata Serialized metadata for the incoming message
/// @returns Tachyon upgrader instance
/// @throws runtime_error on failure
[[nodiscard]] std::unique_ptr<TachyonUpgrader> make_tachyon_cpp_upgrader(
  std::string_view current_class_name,
  std::span<const std::byte> current_metadata,
  std::span<const std::byte> incoming_metadata);

/// Create an instance of a tachyon C++ upgrader.
/// @tparam CurrentSchemaT Current schema type
/// @param[in] incoming_metadata Serialized metadata for the incoming message
/// @returns Pointer to the schema upgrader or a nullptr on error
template <typename CurrentSchemaT>
[[nodiscard]] std::unique_ptr<TachyonUpgrader> make_tachyon_cpp_upgrader(std::span<const std::byte> incoming_metadata)
  requires(TachyonType<CurrentSchemaT> || TappyType<CurrentSchemaT>);

/// Create aninstance of a tachyon memcpy upgrader for upgrading types that are wire compatibile
/// @param[in] schema_size Schema size
/// @param[in] src_fqn Source schema FQN
/// @param[in] dest_fqn Destination schema FQN
/// @returns Pointer to the schema upgrader
[[nodiscard]] std::unique_ptr<TachyonUpgrader>
make_tachyon_memcpy_upgrader(size_t schema_size, std::string_view src_fqn, std::string_view dest_fqn);

} // namespace clockwork::serialization

#include "clockwork/serialization/cpp/tachyon_upgrader.inl"
