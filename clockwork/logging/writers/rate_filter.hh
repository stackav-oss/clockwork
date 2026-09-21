// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/memory_resource.hh"
#include "jewels/time/sync_time.hh"

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <vector>

namespace clockwork_logging
{

/// Rate filter for log message rate diagnostics
///
/// Uses a circular queue of counters to compute a rolling average over a time range. The time range is broken up into 1
/// second epochs, and a separate counter holds the count for each epoch. An extra counter is used to count the messages
/// in the current epoch, which is not used in the rate calculation. As the time advances the current epoch index is
/// incremented and wrapped around using modulo arithmetic, and the counter for the new current epoch is reset to zero
/// to hold the counters for the next epoch. The rest of the epoch counters are averaged to get the filtered rate.
class RateFilter
{
public:
  /// Construct a rate filter to average the rates over the specified number of seconds
  /// @param[in] memory_resource Memory resource
  /// @param[in] current_steady_time Current time
  /// @param[in] window_sec Rate window size in seconds
  RateFilter(
    jewels::memory::MemoryResource memory_resource,
    jewels::time::SteadyTime current_steady_time,
    size_t window_size_sec);

  ~RateFilter() noexcept = default;

  RateFilter(const RateFilter&) noexcept = delete;
  RateFilter& operator=(const RateFilter&) noexcept = delete;
  RateFilter(RateFilter&&) noexcept = default;
  RateFilter& operator=(RateFilter&&) noexcept = default;

  /// Update the rate window for a received message
  /// @param[in] current_steady_time Current time
  /// @param[in] count Number of messages to record
  void update(jewels::time::SteadyTime current_steady_time, size_t count = 1U);

  /// Get the average rate from the window
  /// @param[in] current_steady_time Current time
  /// @return Average rate
  [[nodiscard]] double get_rate(jewels::time::SteadyTime current_steady_time);

private:
  /// Update the current epoch from the current log time
  /// @param[in] current_steady_time Current time
  void update_epoch(jewels::time::SteadyTime current_steady_time);

  /// Storage for the counters for each second in the window
  std::pmr::vector<size_t> epoch_counters_;

  /// Total of the counters for the current epoch
  size_t total_counter_{};

  /// Index of the counter for the current epoch
  size_t current_epoch_index_{};

  /// Current epoch time in seconds
  int64_t current_epoch_sec_;
};

} // namespace clockwork_logging
