// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/writers/message_rate_counter.hh"

#include <cstddef>
#include <memory_resource>
#include <span>
#include <utility>

namespace clockwork_logging
{

MessageRateCounter::MessageRateCounter(
  jewels::memory::MemoryResource memory_resource,
  const clockwork::Tappy<ChannelMessageRatesConfig>& channel_rates_config)
  : memory_resource_(std::move(memory_resource)),
    window_size_sec_(channel_rates_config.get_window_size_sec()),
    min_channel_msg_rates_hz_(memory_resource_),
    channel_rate_map_(memory_resource_)
{
  for (const auto& channel_message_rate : channel_rates_config.get_channel_message_rates())
  {
    min_channel_msg_rates_hz_.emplace(
      std::pmr::string{channel_message_rate.get_channel_name()}, channel_message_rate.get_min_msg_rate_hz());
  }
}

void MessageRateCounter::add_channel(std::string_view channel_name, jewels::time::SteadyTime current_steady_time)
{
  const std::pmr::string channel_str{channel_name, memory_resource_};
  auto min_msg_rate_iter = min_channel_msg_rates_hz_.find(channel_str);
  if (min_msg_rate_iter == min_channel_msg_rates_hz_.end())
  {
    min_msg_rate_iter = min_channel_msg_rates_hz_.emplace(channel_str, 0.0).first;
  }
  channel_rate_map_.emplace(
    min_msg_rate_iter->first,
    MapEntry{
      .min_msg_rate_hz = min_msg_rate_iter->second,
      .rate_filter = RateFilter{memory_resource_, current_steady_time, window_size_sec_},
    });
}

void MessageRateCounter::update_channel(
  std::string_view channel_name, jewels::time::SteadyTime current_steady_time, size_t count)
{
  const auto iter = channel_rate_map_.find(channel_name);
  if (iter == channel_rate_map_.end())
  {
    return;
  }
  iter->second.rate_filter.update(current_steady_time, count);
}

[[nodiscard]] std::pmr::map<std::string_view, MessageRateCounter::MapEntry>& MessageRateCounter::get_channel_rate_map()
{
  return channel_rate_map_;
}

} // namespace clockwork_logging
