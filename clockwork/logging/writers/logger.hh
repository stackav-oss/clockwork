// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_writer_config.hh"
#include "clockwork/logging/writers/channel_message_rates.hh"
#include "clockwork/logging/writers/channel_message_rates_config.hh"
#include "clockwork/logging/writers/logger_config.hh"
#include "clockwork/logging/writers/logger_status.hh"
#include "clockwork/logging/writers/message_writer.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/time/sync_time.hh"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory_resource>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace clockwork_logging
{

/// State for a logger cog
class Logger
{
public:
  /// Message writer run interval
  static constexpr std::chrono::milliseconds writer_run_interval{100};

  /// Channel message rates publish interval
  static constexpr std::chrono::seconds channel_rates_publish_interval{5};

  /// Construct a logger
  /// @param[in] memory_resource Memory resource
  /// @param[in] log_writer_config Log writer configuration
  /// @param[in] logger_config Logger configuration
  /// @param[in] channel_rates_config Channel message rates config
  Logger(
    jewels::memory::MemoryResource memory_resource,
    const LogWriterConfigTap& log_writer_config,
    const LoggerConfigTap& logger_config,
    const clockwork::Tappy<ChannelMessageRatesConfig>& channel_rates_config);

  /// Destructor shuts down the writer thread
  ~Logger();

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;
  Logger(Logger&&) = delete;
  Logger& operator=(Logger&&) = delete;

  /// Get the log directory name
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @return Directory name
  [[nodiscard]] std::string_view get_log_directory_name() const;

  /// Generate a logger status message from the current state
  /// @param[out] message Logger status message
  void get_logger_status_message(LoggerStatusTap& message);

  /// Generate a channel rate message from the current state
  /// @param[out] message Channel rate message
  /// @return True if the message should be published
  [[nodiscard]] bool get_channel_rates_message(ChannelMessageRatesTap& message);

  /// Get the message counts by channel
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @return Message counts by channel
  [[nodiscard]] std::pmr::unordered_map<std::pmr::string, size_t> get_message_counts() const;

  /// Clear the message counts by channel
  /// @note This method *MAY* be called by the thread that reports the writer state
  void clear_message_counts();

private:
  /// Writer thread main function
  void writer_thread_main();

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Log directory name
  std::pmr::string log_directory_name_;

  /// Message writer
  MessageWriter message_writer_;

  /// Stop requested flag
  std::atomic<bool> stop_requested_{false};

  /// Message writer thread
  std::thread writer_thread_;

  /// Last channel rates message publish time
  jewels::time::SyncTime last_channel_rates_publish_time_;
};

} // namespace clockwork_logging
