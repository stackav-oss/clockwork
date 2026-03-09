// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/writers/logger.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_writer_config_clk_cc.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/writers/channel_message_rates_clk_cc.hh"
#include "clockwork/logging/writers/channel_message_rates_config_clk_cc.hh"
#include "clockwork/logging/writers/log_writer_state_clk_cc.hh"
#include "clockwork/logging/writers/logger_status_clk_cc.hh"
#include "clockwork/logging/writers/message_writer.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/time/sync_time.hh"

#include <fmt/base.h>

#include <compare>
#include <cstddef>
#include <functional>
#include <iterator>
#include <map>
#include <ranges>
#include <string_view>
#include <thread>
#include <utility>

namespace clockwork_logging
{

Logger::Logger(
  jewels::memory::MemoryResource memory_resource,
  const clockwork::Tappy<LogWriterConfig<>>& log_writer_config,
  const clockwork::Tappy<LoggerConfig>& logger_config,
  const clockwork::Tappy<ChannelMessageRatesConfig>& channel_rates_config)
  : memory_resource_(std::move(memory_resource)),
    log_directory_name_(memory_resource_),
    message_writer_(memory_resource_, log_writer_config, logger_config, channel_rates_config)
{
  fmt::format_to(
    std::back_inserter(log_directory_name_), "{}", jewels::time::SyncClock::now().time_since_epoch().count());
  if (const auto init_result = message_writer_.initialize(log_writer_config); !init_result)
  {
    jewels::log_cerr_error("Failed to initialize the message writer: {}", init_result.error());
    return;
  }
  writer_thread_ = std::thread([this]() { writer_thread_main(); });
}

Logger::~Logger()
{
  if (writer_thread_.joinable())
  {
    stop_requested_.store(true, std::memory_order_release);
    writer_thread_.join();
  }
}

[[nodiscard]] std::string_view Logger::get_log_directory_name() const
{
  return log_directory_name_;
}

void Logger::get_logger_status_message(clockwork::Tappy<LoggerStatus>& message)
{
  const auto state = message_writer_.get_state();
  const auto writer_status = message_writer_.get_status();
  message.set_state(state);
  message.set_transmit_time(jewels::time::SyncClock::now());
  message.set_drop_count(message_writer_.get_and_reset_drop_count());
  message.set_max_backlog(message_writer_.get_and_reset_max_write_backlog());
  message.set_msgs_per_sec(writer_status.msgs_per_sec);
  message.set_bytes_per_sec(writer_status.bytes_per_sec);
  if (state == LogWriterState::degraded || state == LogWriterState::failed)
  {
    message.get_underlying_status_string().set_truncate(writer_status.status_string);
  }
  message.set_num_low_rate_channels(message_writer_.get_num_low_rate_channels());
  message.get_underlying_low_rate_channel_name().set_truncate(message_writer_.get_low_rate_channel_name());
}

[[nodiscard]] bool Logger::get_channel_rates_message(clockwork::Tappy<ChannelMessageRates<>>& message)
{
  const auto current_time = jewels::time::SyncClock::now();
  if (last_channel_rates_publish_time_ + channel_rates_publish_interval > current_time)
  {
    return false;
  }
  message.set_transmit_time(current_time);
  const auto rate_map = message_writer_.get_channel_message_rates();
  for (const auto& [channel_name, entry] :
       rate_map | std::views::take(message.get_underlying_message_rates().capacity()))
  {
    auto& message_rate = message.get_underlying_message_rates().emplace_back();
    message_rate.get_underlying_channel_name().set_truncate(channel_name);
    message_rate.set_msg_rate_hz(entry.msg_rate_hz);
    message_rate.set_rate_status(entry.rate_status);
  }
  last_channel_rates_publish_time_ = current_time;
  return true;
}

[[nodiscard]] std::pmr::unordered_map<std::pmr::string, size_t> Logger::get_message_counts() const
{
  return message_writer_.get_message_counts();
}

void Logger::clear_message_counts()
{
  message_writer_.clear_message_counts();
}

void Logger::writer_thread_main()
{
  if (const auto start_result = message_writer_.start_logging(log_directory_name_); !start_result)
  {
    jewels::log_cerr_error("Failed to start logging in '{}': {}", log_directory_name_, start_result.error());
    return;
  }
  while (!stop_requested_.load(std::memory_order_acquire))
  {
    message_writer_.run_for(writer_run_interval);
  }
}

} // namespace clockwork_logging
