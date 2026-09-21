// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace clockwork
{

/// Base class for execution condition members of Dials.
///
/// This is intended to be used as a member of a Cog Dial, specifically within
/// the Conditions sub-struct, to provide information to the Cog function about
/// which conditions triggered the current execution.  For a condition type
/// which only needs to provide active/inactive information, this can be used
/// directly.  It can also be sub-classed to add additional information.
class ExecCondition
{
public:
  /// Default constructor (leaves is_active = false)
  constexpr ExecCondition() noexcept = default;

  /// Construct with given is_active flag.
  constexpr explicit ExecCondition(bool is_active) noexcept;

  /// True if this condition is/was active at time of this execution.
  [[nodiscard]] constexpr bool is_active() const noexcept;

  /// True if this condition is/was active at time of this execution.
  [[nodiscard]] constexpr explicit operator bool() const noexcept;

private:
  bool is_active_{false};
};

} // namespace clockwork

#include "clockwork/dial/exec_condition.inl"
