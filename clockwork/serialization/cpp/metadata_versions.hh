// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace clockwork::serialization
{

/// Metadata version when the built-in type UUID was added to the metadata.
static constexpr int32_t built_in_uuid_added_in_version = 2;

/// Metadata version when the underlying type was added to the metadata.
static constexpr int32_t enum_underlying_type_added_in_version = 3;

/// Metadata version with added checks to require increasing version when adding fields and enum values.
static constexpr int32_t enforce_version_change_when_adding_fields_and_values_version = 4;

/// Latest metadata protobuf version
static constexpr int32_t latest_metadata_protobuf_version =
  enforce_version_change_when_adding_fields_and_values_version;

} // namespace clockwork::serialization
