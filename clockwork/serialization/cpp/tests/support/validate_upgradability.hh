// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace clockwork::serialization
{

/// Validate that the source schema can be upgraded to the destination schema
/// @tparam SrcType Source schema type
/// @tparam DestType Destination schema type
template <typename SrcType, typename DestType>
void validate_upgradability();

/// Validate that the source schema cannot be upgraded to the destination schema
/// @tparam SrcType Source schema type
/// @tparam DestType Destination schema type
template <typename SrcType, typename DestType>
void validate_no_upgradability();

} // namespace clockwork::serialization

#include "clockwork/serialization/cpp/tests/support/validate_upgradability.inl"
