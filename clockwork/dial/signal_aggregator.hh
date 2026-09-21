// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace clockwork
{

//==============================================================================
// MinAggregator - Primary template (with metadata)
//==============================================================================
template <typename T, typename Metadata = void>
class MinAggregator
{
public:
  using value_type = T;
  using metadata_type = Metadata;

  constexpr MinAggregator() noexcept = default;

  /// Accumulate a new value with metadata, updating the minimum if needed.
  template <typename MetadataFwd>
  constexpr void accumulate(T value, MetadataFwd&& metadata) noexcept;

  /// Get the current minimum value.
  [[nodiscard]] constexpr T get_value() const noexcept;

  /// Get the metadata associated with the minimum value.
  [[nodiscard]] constexpr Metadata get_metadata() const noexcept;

  /// Check if any values have been accumulated.
  [[nodiscard]] constexpr bool has_value() const noexcept;

  /// Reset the aggregator to its initial state.
  constexpr void reset() noexcept;

private:
  T value_{};
  Metadata metadata_{};
  bool has_value_{false};
};

// Specialization for void metadata
template <typename T>
class MinAggregator<T, void>
{
public:
  using value_type = T;
  using metadata_type = void;

  constexpr MinAggregator() noexcept = default;

  /// Accumulate a new value, updating the minimum if needed.
  constexpr void accumulate(T value) noexcept;

  /// Get the current minimum value.
  [[nodiscard]] constexpr T get_value() const noexcept;

  /// Check if any values have been accumulated.
  [[nodiscard]] constexpr bool has_value() const noexcept;

  /// Reset the aggregator to its initial state.
  constexpr void reset() noexcept;

private:
  T value_{};
  bool has_value_{false};
};

//==============================================================================
// MaxAggregator - Primary template (with metadata)
//==============================================================================
template <typename T, typename Metadata = void>
class MaxAggregator
{
public:
  using value_type = T;
  using metadata_type = Metadata;

  constexpr MaxAggregator() noexcept = default;

  /// Accumulate a new value with metadata, updating the maximum if needed.
  template <typename MetadataFwd>
  constexpr void accumulate(T value, MetadataFwd&& metadata) noexcept;

  /// Get the current maximum value.
  [[nodiscard]] constexpr T get_value() const noexcept;

  /// Get the metadata associated with the maximum value.
  [[nodiscard]] constexpr Metadata get_metadata() const noexcept;

  /// Check if any values have been accumulated.
  [[nodiscard]] constexpr bool has_value() const noexcept;

  /// Reset the aggregator to its initial state.
  constexpr void reset() noexcept;

private:
  T value_{};
  Metadata metadata_{};
  bool has_value_{false};
};

// Specialization for void metadata
template <typename T>
class MaxAggregator<T, void>
{
public:
  using value_type = T;
  using metadata_type = void;

  constexpr MaxAggregator() noexcept = default;

  /// Accumulate a new value, updating the maximum if needed.
  constexpr void accumulate(T value) noexcept;

  /// Get the current maximum value.
  [[nodiscard]] constexpr T get_value() const noexcept;

  /// Check if any values have been accumulated.
  [[nodiscard]] constexpr bool has_value() const noexcept;

  /// Reset the aggregator to its initial state.
  constexpr void reset() noexcept;

private:
  T value_{};
  bool has_value_{false};
};

//==============================================================================
// SumAggregator - No metadata support
//==============================================================================
template <typename T>
class SumAggregator
{
public:
  using value_type = T;

  constexpr SumAggregator() noexcept = default;

  /// Accumulate a new value into the sum.
  constexpr void accumulate(T value) noexcept;

  /// Get the current sum.
  [[nodiscard]] constexpr T get_value() const noexcept;

  /// Check if any values have been accumulated.
  [[nodiscard]] constexpr bool has_value() const noexcept;

  /// Reset the aggregator to its initial state.
  constexpr void reset() noexcept;

private:
  T sum_{};
  bool has_value_{false};
};

//==============================================================================
// CountAggregator - No metadata support
//==============================================================================
template <typename T = uint64_t>
class CountAggregator
{
public:
  using value_type = T;

  constexpr CountAggregator() noexcept = default;

  /// Accumulate (increments count regardless of value).
  template <typename U>
  constexpr void accumulate(U /*unused*/) noexcept;

  /// Get the current count.
  [[nodiscard]] constexpr T get_value() const noexcept;

  /// Check if any values have been accumulated.
  [[nodiscard]] constexpr bool has_value() const noexcept;

  /// Reset the aggregator to its initial state.
  constexpr void reset() noexcept;

private:
  T count_{};
};

//==============================================================================
// MeanAggregator - Tracks sum and count for mean computation
//==============================================================================
template <typename T>
class MeanAggregator
{
public:
  using value_type = T;

  constexpr MeanAggregator() noexcept = default;

  /// Accumulate a new value into the mean calculation.
  constexpr void accumulate(T value) noexcept;

  /// Get the current mean as a double.
  [[nodiscard]] constexpr double get_mean() const noexcept;

  /// Get the current sum.
  [[nodiscard]] constexpr T get_sum() const noexcept;

  /// Get the current count.
  [[nodiscard]] constexpr uint64_t get_count() const noexcept;

  /// Check if any values have been accumulated.
  [[nodiscard]] constexpr bool has_value() const noexcept;

  /// Reset the aggregator to its initial state.
  constexpr void reset() noexcept;

private:
  T sum_{};
  uint64_t count_{0};
};

//==============================================================================
// FirstValueAggregator - Primary template (with metadata)
//==============================================================================
template <typename T, typename Metadata = void>
class FirstValueAggregator
{
public:
  using value_type = T;
  using metadata_type = Metadata;

  constexpr FirstValueAggregator() noexcept = default;

  /// Accumulate a new value with metadata, only storing if this is the first value.
  template <typename MetadataFwd>
  constexpr void accumulate(T value, MetadataFwd&& metadata) noexcept;

  /// Get the first value.
  [[nodiscard]] constexpr T get_value() const noexcept;

  /// Get the metadata associated with the first value.
  [[nodiscard]] constexpr Metadata get_metadata() const noexcept;

  /// Check if any values have been accumulated.
  [[nodiscard]] constexpr bool has_value() const noexcept;

  /// Reset the aggregator to its initial state.
  constexpr void reset() noexcept;

private:
  T value_{};
  Metadata metadata_{};
  bool has_value_{false};
};

// Specialization for void metadata
template <typename T>
class FirstValueAggregator<T, void>
{
public:
  using value_type = T;
  using metadata_type = void;

  constexpr FirstValueAggregator() noexcept = default;

  /// Accumulate a new value, only storing if this is the first value.
  constexpr void accumulate(T value) noexcept;

  /// Get the first value.
  [[nodiscard]] constexpr T get_value() const noexcept;

  /// Check if any values have been accumulated.
  [[nodiscard]] constexpr bool has_value() const noexcept;

  /// Reset the aggregator to its initial state.
  constexpr void reset() noexcept;

private:
  T value_{};
  bool has_value_{false};
};

//==============================================================================
// FinalValueAggregator - Primary template (with metadata)
//==============================================================================
template <typename T, typename Metadata = void>
class FinalValueAggregator
{
public:
  using value_type = T;
  using metadata_type = Metadata;

  constexpr FinalValueAggregator() noexcept = default;

  /// Accumulate a new value with metadata, always replacing the previous value.
  template <typename MetadataFwd>
  constexpr void accumulate(T value, MetadataFwd&& metadata) noexcept;

  /// Get the final value.
  [[nodiscard]] constexpr T get_value() const noexcept;

  /// Get the metadata associated with the final value.
  [[nodiscard]] constexpr Metadata get_metadata() const noexcept;

  /// Check if any values have been accumulated.
  [[nodiscard]] constexpr bool has_value() const noexcept;

  /// Reset the aggregator to its initial state.
  constexpr void reset() noexcept;

private:
  T value_{};
  Metadata metadata_{};
  bool has_value_{false};
};

// Specialization for void metadata
template <typename T>
class FinalValueAggregator<T, void>
{
public:
  using value_type = T;
  using metadata_type = void;

  constexpr FinalValueAggregator() noexcept = default;

  /// Accumulate a new value, always replacing the previous value.
  constexpr void accumulate(T value) noexcept;

  /// Get the final value.
  [[nodiscard]] constexpr T get_value() const noexcept;

  /// Check if any values have been accumulated.
  [[nodiscard]] constexpr bool has_value() const noexcept;

  /// Reset the aggregator to its initial state.
  constexpr void reset() noexcept;

private:
  T value_{};
  bool has_value_{false};
};

//==============================================================================
// ValueAggregator - Alias for FinalValueAggregator
//==============================================================================
template <typename T, typename Metadata = void>
using ValueAggregator = FinalValueAggregator<T, Metadata>;

} // namespace clockwork

#include "clockwork/dial/signal_aggregator.inl"
