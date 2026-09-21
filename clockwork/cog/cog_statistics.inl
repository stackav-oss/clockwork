// IWYU pragma: private, include "clockwork/cog/cog_statistics.hh"

#pragma once

#include "clockwork/cog/cog_statistics.hh"

#include "clockwork/dsl/cog/ten_nanosecond_type.hh"

#include <algorithm>
#include <chrono>
#include <optional>

namespace clockwork
{
template <typename T>
MinMaxMean<T>& MinMaxMean<T>::update(T value)
{
  if (!set_)
  {
    min_ = value;
    max_ = value;
    sum_ = value;
    set_ = true;
    count_ = 1;
    return *this;
  }

  min_ = std::min(min_, value);
  max_ = std::max(max_, value);

  sum_ += value;
  ++count_;

  return *this;
}

template <typename T>
void MinMaxMean<T>::clear()
{
  sum_ = T{};
  count_ = 0;
  set_ = false;
}

template <typename T>
std::optional<T> MinMaxMean<T>::min() const
{
  if (!set_)
  {
    return std::nullopt;
  }
  return min_;
}

template <typename T>
std::optional<T> MinMaxMean<T>::max() const
{
  if (!set_)
  {
    return std::nullopt;
  }
  return max_;
}

template <typename T>
std::optional<double> MinMaxMean<T>::mean() const
{
  if (!set_)
  {
    return std::nullopt;
  }

  return static_cast<double>(sum_) / static_cast<double>(count_);
}

template <typename T>
std::optional<T> MinMaxMean<T>::sum() const
{
  if (!set_)
  {
    return std::nullopt;
  }
  return sum_;
}

template <>
std::optional<double> MinMaxMean<TenNanoseconds>::mean() const
{
  if (!set_)
  {
    return std::nullopt;
  }

  return static_cast<double>(sum_.count()) / static_cast<double>(count_);
}
} // namespace clockwork
