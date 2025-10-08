// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/writers/channel_message_rates_config.hh"
#include "clockwork/logging/writers/rate_filter.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <memory_resource>
#include <string>
#include <string_view>
#include <unordered_map>

namespace clockwork_logging
{

/// Rate counter for logged message rates.
///
/// Keeps a map of rate counters for channels logged by an event writer. This class only keeps counters
/// for channel names that match the name in a diagnostics descriptor under the logged_message_rates signal
/// group.
class MessageRateCounter
{
  /// Entry in the map that stores the counters for each channel
  struct MapEntry
  {
    /// Minimumm message rate in hz.
    double min_msg_rate_hz{};

    /// Message rate filter
    RateFilter rate_filter;
  };

public:
  /// Default time to allow for the rate counter to warm up at startup
  static constexpr auto default_warmup_interval = std::chrono::minutes(2);

  /// Construct a message rate counter
  /// @param[in] memory_resource Memory resource
  /// @param[in] channel_rate_config Channel message rates configuration
  /// @param[in] warmup_interval Time to allow for the rate counter to warm up at startup
  /// @param[in] current_steady_time Current steady time
  MessageRateCounter(
    jewels::memory::MemoryResource memory_resource,
    const clockwork::Tappy<ChannelMessageRatesConfig>& channel_rates_config,
    std::chrono::nanoseconds warmup_interval = default_warmup_interval,
    jewels::time::SteadyTime current_steady_time = jewels::time::SteadyClock::now());

  ~MessageRateCounter() noexcept = default;

  MessageRateCounter(const MessageRateCounter&) noexcept = delete;
  MessageRateCounter& operator=(const MessageRateCounter&) noexcept = delete;
  MessageRateCounter(MessageRateCounter&&) noexcept = default;
  MessageRateCounter& operator=(MessageRateCounter&&) noexcept = default;

  /// Add a logged channel to the rate counter.
  /// in the logged_message_rates signal group.
  /// @param[in] channel_name
  /// @param[in] current_steady_time Current steady time
  void add_channel(
    std::string_view channel_name, jewels::time::SteadyTime current_steady_time = jewels::time::SteadyClock::now());

  /// Increment the message count for a logged channel
  /// @param[in] channel_name
  /// @param[in] current_steady_time Current steady time
  /// @param[in] count Number of messages
  void update_channel(
    std::string_view channel_name,
    jewels::time::SteadyTime current_steady_time = jewels::time::SteadyClock::now(),
    size_t count = 1U);

  /// Accessor for the channel rate map
  /// @return Channel rate map
  [[nodiscard]] std::pmr::map<std::string_view, MapEntry>& get_channel_rate_map();

  /// @return True if the rate counter has warmed up
  /// @param curr_steady_time Current steady time
  [[nodiscard]] bool
  is_warmed_up(jewels::time::SteadyTime current_steady_time = jewels::time::SteadyClock::now()) const;

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Number of seconds to average values for the message rates
  size_t window_size_sec_;

  /// Map from channel name to minimum message rate in hz.
  std::pmr::unordered_map<std::pmr::string, double> min_channel_msg_rates_hz_;

  /// Channel rate map
  std::pmr::map<std::string_view, MapEntry> channel_rate_map_;

  /// Time that the rate counter will have warmed up
  jewels::time::SteadyTime warmup_time_;
};

} // namespace clockwork_logging
