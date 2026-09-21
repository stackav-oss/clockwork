// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
// IWYU pragma: private, include "clockwork/serialization/cpp/tachyon_model.hh"

#pragma once

#include "clockwork/serialization/cpp/tachyon_model.hh"

#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/clk_type.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"

#include <memory>
#include <string>

namespace clockwork::serialization
{

template <typename T>
[[nodiscard]] std::shared_ptr<TachyonModel>
TachyonModel::from_type(const jewels::memory::MemoryResource& memory_resource)
  requires(TachyonType<T> || TappyType<T>)
{
  auto metadata_proto = jewels::memory::make_pmr_shared<metadata::TachyonMetadata>(memory_resource);
  if (!metadata_proto->ParseFromString(
        std::string{LoggingTraits<T>::schema_definition.data(), LoggingTraits<T>::schema_definition.size()}))
  {
    throw ClkTypeUpgradeError("Failed to parse schema definition");
  }
  return from_proto(memory_resource, metadata_proto);
}

} // namespace clockwork::serialization
