// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/interface.hh"

#include "jewels/callsig/outcome.hh"

namespace clockwork
{

CogConfigData::~CogConfigData() = default;

CogStateData::~CogStateData() = default;

jewels::BinaryOutcome CogStateData::set_from_bytes(std::span<const std::byte> /*data*/) noexcept
{
  // Default implementation for C++ states - they cannot be set from bytes
  return jewels::failure;
}

CogBase::~CogBase() = default;

} // namespace clockwork
