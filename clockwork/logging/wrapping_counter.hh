// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/meta/concepts.hh"

#include <limits>
#include <type_traits>

namespace clockwork_logging
{

/// Helper class for comparing sequence numbers that wrap when they reach their max value.
///
/// Uses the 'wrap_window' to value to detect when a sequence number is less than another
/// if it is within the wrap window below the max value and the other is within the wrap
/// above the min value.
template <jewels::meta::Integral Counter>
class __attribute__((packed)) WrappingCounter
{
public:
  using CounterType = Counter;
  static constexpr auto min_value = std::numeric_limits<CounterType>::min();
  static constexpr auto max_value = std::numeric_limits<CounterType>::max();
  static constexpr auto wrap_window = std::is_unsigned_v<CounterType> ? max_value / 4 : max_value / 2;

  /// Constructor
  /// @param[in] value Counter value
  explicit constexpr WrappingCounter(CounterType value) noexcept;

  /// Default constructor
  WrappingCounter() noexcept = default;

  ~WrappingCounter() noexcept = default;
  WrappingCounter(const WrappingCounter&) noexcept = default;
  WrappingCounter& operator=(const WrappingCounter&) noexcept = default;
  WrappingCounter(WrappingCounter&&) noexcept = default;
  WrappingCounter& operator=(WrappingCounter&&) noexcept = default;

  /// Value accessor
  /// @return Counter value
  [[nodiscard]] constexpr CounterType value() const noexcept;

  /// Equals operator
  friend constexpr bool operator==(const WrappingCounter&, const WrappingCounter&) noexcept = default;

  /// Less than operator
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return True iff lhs < rhs
  friend constexpr bool operator<(const WrappingCounter& lhs, const WrappingCounter& rhs) noexcept
  {
    if (lhs == rhs)
    {
      return false;
    }
    if ((lhs.value_ >= max_value - wrap_window) && (rhs.value_ < min_value + wrap_window))
    {
      return true;
    }
    if ((rhs.value_ >= max_value - wrap_window) && (lhs.value_ < min_value + wrap_window))
    {
      return false;
    }
    return lhs.value_ < rhs.value_;
  }

private:
  /// Counter value
  CounterType value_{};
};

} // namespace clockwork_logging

#include "clockwork/logging/wrapping_counter.inl"
