// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/dial/exec_condition.hh"

namespace clockwork
{

/// Dial condition for a dynamic one-shot timer (used by aligner cogs).
///
/// This is a read-only snapshot of the timer's fired state, analogous to
/// TimeSinceLastExecCondition but without a fixed threshold or elapsed-time
/// tracking. The timer is dynamically armed/disarmed at runtime by the
/// generated aligner code.
class DynamicTimerCondition : public ExecCondition
{
public:
  /// Default constructor — condition is inactive.
  constexpr DynamicTimerCondition() noexcept = default;

  /// Construct with explicit fired state.
  constexpr explicit DynamicTimerCondition(bool is_active) noexcept;
};

} // namespace clockwork

#include "clockwork/dial/cond_dynamic_timer.inl"
