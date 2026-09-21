// IWYU pragma: private, include "clockwork/serialization/cpp/tachyon_lite_compressor.hh"
#pragma once
// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/serialization/cpp/tachyon_lite_compressor.hh"

#include "clockwork/repr_iface.hh"
#include "jewels/memory/memory_resource.hh"

#include <memory>
#include <span>
#include <string_view>

namespace clockwork::serialization
{

template <typename SchemaType>
[[nodiscard]] std::shared_ptr<TachyonLiteCompressor>
TachyonLiteCompressor::make_compressor(const jewels::memory::MemoryResource& memory_resource, std::string_view name)
  requires(TachyonType<SchemaType> || TappyType<SchemaType>)
{
  if (name.empty())
  {
    name = LoggingTraits<SchemaType>::schema_name;
  }
  return make_compressor(memory_resource, name, std::as_bytes(std::span{LoggingTraits<SchemaType>::schema_definition}));
}

} // namespace clockwork::serialization
