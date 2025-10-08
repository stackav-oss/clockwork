// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
// IWYU pragma: private, include "clockwork/serialization/cpp/tachyon_upgrader.hh"

#pragma once

#include "clockwork/serialization/cpp/tachyon_upgrader.hh"

#include "clockwork/repr_iface.hh"

#include <cstddef>
#include <memory>
#include <span>

namespace clockwork::serialization
{

template <typename CurrentSchemaT>
[[nodiscard]] std::unique_ptr<TachyonUpgrader> make_tachyon_cpp_upgrader(std::span<const std::byte> incoming_metadata)
  requires(TachyonType<CurrentSchemaT> || TappyType<CurrentSchemaT>)
{
  return make_tachyon_cpp_upgrader(
    LoggingTraits<CurrentSchemaT>::class_name,
    std::as_bytes(std::span{LoggingTraits<CurrentSchemaT>::schema_definition}),
    incoming_metadata);
}

} // namespace clockwork::serialization
