// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
// IWYU pragma: private, include "clockwork/dial/signal_aggregator.hh"

#pragma once

#include "clockwork/dial/signal_aggregator.hh"

#include <cstdint>
#include <utility>

namespace clockwork
{

//==============================================================================
// MinAggregator - Primary template implementations
//==============================================================================
template <typename T, typename Metadata>
template <typename MetadataFwd>
constexpr void MinAggregator<T, Metadata>::accumulate(T value, MetadataFwd&& metadata) noexcept
{
  if (!has_value_ || value < value_)
  {
    value_ = value;
    metadata_ = std::forward<MetadataFwd>(metadata);
    has_value_ = true;
  }
}

template <typename T, typename Metadata>
constexpr T MinAggregator<T, Metadata>::get_value() const noexcept
{
  return value_;
}

template <typename T, typename Metadata>
constexpr Metadata MinAggregator<T, Metadata>::get_metadata() const noexcept
{
  return metadata_;
}

template <typename T, typename Metadata>
constexpr bool MinAggregator<T, Metadata>::has_value() const noexcept
{
  return has_value_;
}

template <typename T, typename Metadata>
constexpr void MinAggregator<T, Metadata>::reset() noexcept
{
  has_value_ = false;
}

// MinAggregator<T, void> specialization implementations
template <typename T>
constexpr void MinAggregator<T, void>::accumulate(T value) noexcept
{
  if (!has_value_ || value < value_)
  {
    value_ = value;
    has_value_ = true;
  }
}

template <typename T>
constexpr T MinAggregator<T, void>::get_value() const noexcept
{
  return value_;
}

template <typename T>
constexpr bool MinAggregator<T, void>::has_value() const noexcept
{
  return has_value_;
}

template <typename T>
constexpr void MinAggregator<T, void>::reset() noexcept
{
  has_value_ = false;
}

//==============================================================================
// MaxAggregator - Primary template implementations
//==============================================================================
template <typename T, typename Metadata>
template <typename MetadataFwd>
constexpr void MaxAggregator<T, Metadata>::accumulate(T value, MetadataFwd&& metadata) noexcept
{
  if (!has_value_ || value > value_)
  {
    value_ = value;
    metadata_ = std::forward<MetadataFwd>(metadata);
    has_value_ = true;
  }
}

template <typename T, typename Metadata>
constexpr T MaxAggregator<T, Metadata>::get_value() const noexcept
{
  return value_;
}

template <typename T, typename Metadata>
constexpr Metadata MaxAggregator<T, Metadata>::get_metadata() const noexcept
{
  return metadata_;
}

template <typename T, typename Metadata>
constexpr bool MaxAggregator<T, Metadata>::has_value() const noexcept
{
  return has_value_;
}

template <typename T, typename Metadata>
constexpr void MaxAggregator<T, Metadata>::reset() noexcept
{
  has_value_ = false;
}

// MaxAggregator<T, void> specialization implementations
template <typename T>
constexpr void MaxAggregator<T, void>::accumulate(T value) noexcept
{
  if (!has_value_ || value > value_)
  {
    value_ = value;
    has_value_ = true;
  }
}

template <typename T>
constexpr T MaxAggregator<T, void>::get_value() const noexcept
{
  return value_;
}

template <typename T>
constexpr bool MaxAggregator<T, void>::has_value() const noexcept
{
  return has_value_;
}

template <typename T>
constexpr void MaxAggregator<T, void>::reset() noexcept
{
  has_value_ = false;
}

//==============================================================================
// SumAggregator implementations
//==============================================================================
template <typename T>
constexpr void SumAggregator<T>::accumulate(T value) noexcept
{
  sum_ += value;
  has_value_ = true;
}

template <typename T>
constexpr T SumAggregator<T>::get_value() const noexcept
{
  return sum_;
}

template <typename T>
constexpr bool SumAggregator<T>::has_value() const noexcept
{
  return has_value_;
}

template <typename T>
constexpr void SumAggregator<T>::reset() noexcept
{
  sum_ = T{};
  has_value_ = false;
}

//==============================================================================
// CountAggregator implementations
//==============================================================================
//  Value passed is ignored. Only the count is tracked. Parameter exists for interface consistency.
template <typename T>
template <typename U>
constexpr void CountAggregator<T>::accumulate(U /*unused*/) noexcept
{
  ++count_;
}

template <typename T>
constexpr T CountAggregator<T>::get_value() const noexcept
{
  return count_;
}

template <typename T>
constexpr bool CountAggregator<T>::has_value() const noexcept
{
  return count_ > 0;
}

template <typename T>
constexpr void CountAggregator<T>::reset() noexcept
{
  count_ = T{};
}

//==============================================================================
// MeanAggregator implementations
//==============================================================================
template <typename T>
constexpr void MeanAggregator<T>::accumulate(T value) noexcept
{
  sum_ += value;
  ++count_;
}

template <typename T>
constexpr double MeanAggregator<T>::get_mean() const noexcept
{
  if (count_ == 0)
  {
    return 0.0;
  }
  // Use .count() for chrono duration types to extract the underlying numeric representation.
  if constexpr (requires { sum_.count(); })
  {
    return static_cast<double>(sum_.count()) / static_cast<double>(count_);
  }
  else
  {
    return static_cast<double>(sum_) / static_cast<double>(count_);
  }
}

template <typename T>
constexpr T MeanAggregator<T>::get_sum() const noexcept
{
  return sum_;
}

template <typename T>
constexpr uint64_t MeanAggregator<T>::get_count() const noexcept
{
  return count_;
}

template <typename T>
constexpr bool MeanAggregator<T>::has_value() const noexcept
{
  return count_ > 0;
}

template <typename T>
constexpr void MeanAggregator<T>::reset() noexcept
{
  sum_ = T{};
  count_ = 0;
}

//==============================================================================
// FirstValueAggregator - Primary template implementations
//==============================================================================
template <typename T, typename Metadata>
template <typename MetadataFwd>
constexpr void FirstValueAggregator<T, Metadata>::accumulate(T value, MetadataFwd&& metadata) noexcept
{
  if (!has_value_)
  {
    value_ = value;
    metadata_ = std::forward<MetadataFwd>(metadata);
    has_value_ = true;
  }
}

template <typename T, typename Metadata>
constexpr T FirstValueAggregator<T, Metadata>::get_value() const noexcept
{
  return value_;
}

template <typename T, typename Metadata>
constexpr Metadata FirstValueAggregator<T, Metadata>::get_metadata() const noexcept
{
  return metadata_;
}

template <typename T, typename Metadata>
constexpr bool FirstValueAggregator<T, Metadata>::has_value() const noexcept
{
  return has_value_;
}

template <typename T, typename Metadata>
constexpr void FirstValueAggregator<T, Metadata>::reset() noexcept
{
  has_value_ = false;
}

// FirstValueAggregator<T, void> specialization implementations
template <typename T>
constexpr void FirstValueAggregator<T, void>::accumulate(T value) noexcept
{
  if (!has_value_)
  {
    value_ = value;
    has_value_ = true;
  }
}

template <typename T>
constexpr T FirstValueAggregator<T, void>::get_value() const noexcept
{
  return value_;
}

template <typename T>
constexpr bool FirstValueAggregator<T, void>::has_value() const noexcept
{
  return has_value_;
}

template <typename T>
constexpr void FirstValueAggregator<T, void>::reset() noexcept
{
  has_value_ = false;
}

//==============================================================================
// FinalValueAggregator - Primary template implementations
//==============================================================================
template <typename T, typename Metadata>
template <typename MetadataFwd>
constexpr void FinalValueAggregator<T, Metadata>::accumulate(T value, MetadataFwd&& metadata) noexcept
{
  value_ = value;
  metadata_ = std::forward<MetadataFwd>(metadata);
  has_value_ = true;
}

template <typename T, typename Metadata>
constexpr T FinalValueAggregator<T, Metadata>::get_value() const noexcept
{
  return value_;
}

template <typename T, typename Metadata>
constexpr Metadata FinalValueAggregator<T, Metadata>::get_metadata() const noexcept
{
  return metadata_;
}

template <typename T, typename Metadata>
constexpr bool FinalValueAggregator<T, Metadata>::has_value() const noexcept
{
  return has_value_;
}

template <typename T, typename Metadata>
constexpr void FinalValueAggregator<T, Metadata>::reset() noexcept
{
  has_value_ = false;
}

// FinalValueAggregator<T, void> specialization implementations
template <typename T>
constexpr void FinalValueAggregator<T, void>::accumulate(T value) noexcept
{
  value_ = value;
  has_value_ = true;
}

template <typename T>
constexpr T FinalValueAggregator<T, void>::get_value() const noexcept
{
  return value_;
}

template <typename T>
constexpr bool FinalValueAggregator<T, void>::has_value() const noexcept
{
  return has_value_;
}

template <typename T>
constexpr void FinalValueAggregator<T, void>::reset() noexcept
{
  has_value_ = false;
}

} // namespace clockwork
