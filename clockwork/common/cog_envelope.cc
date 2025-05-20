// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/cog_envelope.hh"

#include <chrono>
#include <tuple>

namespace clockwork
{

bool operator==(const CogEnvelope& lhs, const CogEnvelope& rhs)
{
  return std::tie(lhs.ready_time, lhs.cog) == std::tie(rhs.ready_time, rhs.cog);
}

bool operator!=(const CogEnvelope& lhs, const CogEnvelope& rhs)
{
  return !(lhs == rhs);
}

} // namespace clockwork
