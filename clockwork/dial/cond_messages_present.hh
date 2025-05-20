// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/dial/exec_condition.hh"

#include <cstdint>

namespace clockwork
{

/// Dial member for any_message and new_message execution conditions.
///
/// This is meant to be a member of the Conditions part of a Cog Dial if (and only if) the Cog has one or more
/// any_message or new_message conditions.  It provides information to the Cog function about whether the condition is
/// active and how many messages are available for consumption.  The number of messages will be bounded by the min/max
/// bounds specified for the condition.
///
/// @tparam bounds_min Lower bound for number of messages
/// @tparam bounds_max Upper bound for number of messages
template <uint32_t bounds_min, uint32_t bounds_max>
class MessagePresentCondition : public ExecCondition
{
public:
  /// Default constructor
  /// The default constructor leaves the condition inactive with 0 message count
  constexpr MessagePresentCondition() noexcept = default;

  /// Construct with explicit fields
  constexpr explicit MessagePresentCondition(bool is_active, uint32_t num_messages) noexcept;

  /// Get the number of messages
  ///
  /// Note: value should be between min/max bounds, inclusive
  [[nodiscard]] constexpr uint32_t get_num_messages() const noexcept;

  /// Get the lower bound
  [[nodiscard]] consteval uint32_t get_bounds_min() const noexcept;

  /// Get the upper bound
  [[nodiscard]] consteval uint32_t get_bounds_max() const noexcept;

private:
  uint32_t num_messages_{};
};

} // namespace clockwork

#include "clockwork/dial/cond_messages_present.inl"
