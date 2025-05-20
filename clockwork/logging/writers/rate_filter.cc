// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/writers/rate_filter.hh"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace clockwork_logging
{

namespace
{

/// Get the epoch for the current time
/// @param[in] current_steady_time Current steady time
/// @return Epoch containing the current time
[[nodiscard]] int64_t get_epoch_sec(jewels::time::SteadyTime current_steady_time)
{
  return std::chrono::duration_cast<std::chrono::seconds>(current_steady_time.time_since_epoch()).count();
}

} // namespace

RateFilter::RateFilter(
  jewels::memory::MemoryResource memory_resource, jewels::time::SteadyTime current_steady_time, size_t window_size_sec)
  : epoch_counters_(window_size_sec + 1U, 0U, memory_resource), current_epoch_sec_(get_epoch_sec(current_steady_time))
{
  if (window_size_sec == 0U)
  {
    throw std::invalid_argument("Window size must be non-zero");
  }
}

void RateFilter::update(jewels::time::SteadyTime current_steady_time, size_t count)
{
  update_epoch(current_steady_time);
  epoch_counters_.at(current_epoch_index_) += count;
}

[[nodiscard]] double RateFilter::get_rate(jewels::time::SteadyTime current_steady_time)
{
  update_epoch(current_steady_time);
  return static_cast<double>(total_counter_) / static_cast<double>(epoch_counters_.size() - 1U);
}

void RateFilter::update_epoch(jewels::time::SteadyTime current_steady_time)
{
  const auto epoch_sec = get_epoch_sec(current_steady_time);
  /// If time went backwards or jumped by more than the window then zero everything
  if (
    (epoch_sec < current_epoch_sec_) ||
    (epoch_sec - current_epoch_sec_ >= static_cast<int64_t>(epoch_counters_.size())))
  {
    std::ranges::for_each(epoch_counters_, [](auto& counter) { counter = 0U; });
    total_counter_ = 0U;
    current_epoch_sec_ = epoch_sec;
    return;
  }
  /// Advance the current epoch and zero its counters until the current epoch catches up
  /// to the epoch from the current time
  while (current_epoch_sec_ < epoch_sec)
  {
    ++current_epoch_sec_;
    total_counter_ += epoch_counters_[current_epoch_index_];
    current_epoch_index_ = (current_epoch_index_ + 1U) % epoch_counters_.size();
    total_counter_ -= epoch_counters_[current_epoch_index_];
    epoch_counters_[current_epoch_index_] = 0U;
  }
}

} // namespace clockwork_logging
