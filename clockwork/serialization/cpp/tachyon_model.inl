// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
// IWYU pragma: private, include "clockwork/serialization/cpp/tachyon_model.hh"

#pragma once

#include "clockwork/serialization/cpp/tachyon_model.hh"

#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/clk_type.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"

#include <memory>
#include <string>
#include <utility>

namespace clockwork::serialization
{

template <typename T>
[[nodiscard]] std::unique_ptr<TachyonModel> TachyonModel::from_type()
  requires(TachyonType<T> || TappyType<T>)
{
  auto metadata_proto = std::make_unique<metadata::TachyonMetadata>();
  if (!metadata_proto->ParseFromString(
        std::string{LoggingTraits<T>::schema_definition.data(), LoggingTraits<T>::schema_definition.size()}))
  {
    throw ClkTypeUpgradeError("Failed to parse schema definition");
  }
  return from_proto(std::move(metadata_proto));
}

} // namespace clockwork::serialization
