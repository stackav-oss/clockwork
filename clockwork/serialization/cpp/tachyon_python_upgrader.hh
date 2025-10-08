// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clockwork/serialization/cpp/tachyon_upgrader.hh"

#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace clockwork::serialization
{

/// Create a python upgrader from a logged schema to the current schema
/// @param[in] current_module_name Current tachyon schema module name
/// @param[in] current_source_file_name Current tachyon schema source file name
/// @param[in] current_class_name Current tachyon schema class name
/// @param[in] current_metadata Serialized metadata for the current schema
/// @param[in] incoming_metadata Serialized metadata for the incoming message
/// @param[in] incoming_schema_name Schema name for the incoming message
/// @returns Tachyon upgrader instance
/// @throws runtime_error on failure
[[nodiscard]] std::unique_ptr<TachyonUpgrader> make_tachyon_python_upgrader(
  std::string_view current_module_name,
  std::string_view current_source_file_name,
  std::string_view current_class_name,
  std::span<const std::byte> current_metadata,
  std::span<const std::byte> incoming_metadata,
  std::string_view incoming_schema_name);

/// Create an instance of a python upgrader.
/// @tparam CurrentSchemaT Current schema type
/// @param[in] incoming_metadata Serialized metadata for the incoming message
/// @param[in] incoming_schema_name Schema name for the incoming message
/// @returns Pointer to the schema upgrader or a nullptr on error
template <typename CurrentSchemaT>
[[nodiscard]] std::unique_ptr<TachyonUpgrader>
make_tachyon_python_upgrader(std::span<const std::byte> incoming_metadata, std::string_view incoming_schema_name)
  requires(TachyonType<CurrentSchemaT> || TappyType<CurrentSchemaT>);

} // namespace clockwork::serialization

#include "clockwork/serialization/cpp/tachyon_python_upgrader.inl"
