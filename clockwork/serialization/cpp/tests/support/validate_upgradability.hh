// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <wise_enum.h>

namespace clockwork::serialization
{

/// Upgrade validation options
WISE_ENUM_CLASS(
  (UpgradeValidationOption, uint8_t),
  // Upgrade should succeed, downgrade should fail
  upgrade_only,
  // Upgrade and downgrade should both succeed
  upgrade_and_downgrade)

/// Validate that the source schema can be upgraded to the destination schema
/// @tparam SrcType Source schema type
/// @tparam DestType Destination schema type
/// @param[in] validation_option Validation option
template <typename SrcType, typename DestType>
void validate_upgradability(UpgradeValidationOption validation_option = UpgradeValidationOption::upgrade_only);

/// Validate that the source schema cannot be upgraded to the destination schema
/// @tparam SrcType Source schema type
/// @tparam DestType Destination schema type
template <typename SrcType, typename DestType>
void validate_no_upgradability();

} // namespace clockwork::serialization

#include "clockwork/serialization/cpp/tests/support/validate_upgradability.inl"
