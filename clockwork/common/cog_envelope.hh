// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/forward.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/time/sync_time.hh"

namespace clockwork
{

///
/// Wrapper around a Cog to provide additional metadata and
/// metrics from the runner.
///
struct CogEnvelope
{
  ///
  /// The timestamp that the cog became ready.
  ///
  jewels::time::SyncTime ready_time = {};

  /// The earliest time at which publisher credit permits another preparation attempt.
  jewels::time::SyncTime throttled_until = jewels::time::SyncTime::min();

  ///
  /// The cog.
  ///
  jewels::memory::ObjectPtr<AbstractCog> cog;
};

///
/// Equality comparator for CogEnvelope.
///
bool operator==(const CogEnvelope& lhs, const CogEnvelope& rhs);

///
/// Inequality comparator for CogEnvelope.
///
bool operator!=(const CogEnvelope& lhs, const CogEnvelope& rhs);

} // namespace clockwork
