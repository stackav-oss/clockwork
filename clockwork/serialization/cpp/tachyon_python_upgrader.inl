// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
// IWYU pragma: private, include "clockwork/serialization/cpp/tachyon_python_upgrader.hh"

#pragma once

#include "clockwork/serialization/cpp/tachyon_python_upgrader.hh"

#include "clockwork/repr_iface.hh"

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

namespace clockwork::serialization
{

template <typename CurrentSchemaT>
[[nodiscard]] std::unique_ptr<TachyonUpgrader>
make_tachyon_python_upgrader(std::span<const std::byte> incoming_metadata, std::string_view incoming_schema_name)
  requires(TachyonType<CurrentSchemaT> || TappyType<CurrentSchemaT>)
{
  return make_tachyon_python_upgrader(
    LoggingTraits<CurrentSchemaT>::module_name,
    LoggingTraits<CurrentSchemaT>::source_file_name,
    LoggingTraits<CurrentSchemaT>::class_name,
    std::as_bytes(std::span{LoggingTraits<CurrentSchemaT>::schema_definition}),
    incoming_metadata,
    incoming_schema_name);
}

} // namespace clockwork::serialization
