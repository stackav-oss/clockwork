// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <type_traits>

namespace jewels
{

/// Concept for enum types
template <typename T>
concept EnumType = std::is_enum_v<T>;

/// Forward declarations
class BinaryOutcome;
template <EnumType T, T... success_values>
class Outcome; // IWYU pragma: keep

///
/// Type traits for outcome types
///

template <typename T>
struct IsOutcomeType : std::false_type
{
};

template <>
struct IsOutcomeType<BinaryOutcome> : std::true_type
{
};

template <typename T, T... success_values>
struct IsOutcomeType<Outcome<T, success_values...>> : std::true_type
{
};

template <typename T>
constexpr bool is_outcome_type_v = IsOutcomeType<T>::value;

template <typename T>
concept OutcomeType = is_outcome_type_v<T>;

/// Outcome type for success/failure outcomes without other distinct outcomes.
class [[nodiscard]] BinaryOutcome
{
public:
  // Copy and move constructors and assignment operators
  constexpr BinaryOutcome(const BinaryOutcome&) noexcept = default;
  constexpr BinaryOutcome(BinaryOutcome&&) noexcept = default;
  constexpr BinaryOutcome& operator=(const BinaryOutcome&) noexcept = default;
  constexpr BinaryOutcome& operator=(BinaryOutcome&&) noexcept = default;
  constexpr ~BinaryOutcome() = default;

  // No other public constructors!
  // No default constructor!

  /// Determine if the outcome is a success.
  [[nodiscard]] constexpr bool ok() const noexcept
  {
    return is_success_;
  }

  /// Determine if the outcome is a failure.
  [[nodiscard]] constexpr bool fails() const noexcept
  {
    return !is_success_;
  }

  /// Construct a success outcome.
  [[nodiscard]] static constexpr BinaryOutcome make_success() noexcept
  {
    return BinaryOutcome{true};
  }

  /// Construct a failure outcome.
  [[nodiscard]] static constexpr BinaryOutcome make_failure() noexcept
  {
    return BinaryOutcome{false};
  }

private:
  /// Private constructor
  explicit constexpr BinaryOutcome(const bool is_success) noexcept
    : is_success_{is_success}
  {
  }

  bool is_success_;
};

/// Outcome instance for returning success
constexpr BinaryOutcome success = BinaryOutcome::make_success();

/// Outcome instance for returning failure
constexpr BinaryOutcome failure = BinaryOutcome::make_failure();

/// Generic outcome type for enumerated outcomes
template <EnumType T, T... success_values>
class [[nodiscard]] Outcome
{
public:
  using EnumType = T;

  // Copy and move constructors and assignment operators
  constexpr Outcome(const Outcome&) noexcept = default;
  constexpr Outcome(Outcome&&) noexcept = default;
  constexpr Outcome& operator=(const Outcome&) noexcept = default;
  constexpr Outcome& operator=(Outcome&&) noexcept = default;
  constexpr ~Outcome() = default;

  // No default constructor

  /// Implicit construction from the enum
  constexpr Outcome(T value) noexcept // NOLINT(google-explicit-constructor) We want implicit conversion from the enum
    : value_{value}
  {
  }

  /// Get the outcome value
  [[nodiscard]] constexpr T get() const noexcept
  {
    return value_;
  }

  /// Check if the outcome represents a success state - only enabled when success values are provided
  [[nodiscard]] constexpr bool ok() const noexcept
    requires(sizeof...(success_values) > 0)
  {
    return ((value_ == success_values) || ...);
  }

  /// Check if the outcome represents a failure state - only enabled when success values are provided
  [[nodiscard]] constexpr bool fails() const noexcept
    requires(sizeof...(success_values) > 0)
  {
    return !ok();
  }

private:
  T value_;
};

// Free function versions of ok/fails

/// Determine if the outcome is a success.
template <OutcomeType T>
[[nodiscard]] constexpr bool ok(const T& outcome) noexcept
{
  return outcome.ok();
}

/// Determine if the outcome is a failure.
template <OutcomeType T>
[[nodiscard]] constexpr bool fails(const T& outcome) noexcept
{
  return outcome.fails();
}

/// Check if an outcome's value matches any of the provided enum values
/// @param outcome The outcome to check
/// @returns true if the outcome's value matches any of the provided enum values
template <auto... values, typename E = std::common_type_t<decltype(values)...>>
  requires EnumType<E>
[[nodiscard]] constexpr bool outcome_in(const Outcome<E>& outcome) noexcept
{
  return ((outcome.get() == values) || ...);
}

} // namespace jewels
