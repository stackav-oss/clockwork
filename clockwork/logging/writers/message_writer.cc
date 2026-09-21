// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/writers/message_writer.hh"

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/log_writer_config_clk_cc.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/writers/log_writer_state_clk_cc.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/shared_pool/shared_buffer_pool.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <fmt/base.h>

#include <chrono>
#include <iterator>
#include <memory_resource>
#include <string>
#include <utility>

namespace clockwork_logging
{

MessageWriter::MessageWriter(
  jewels::memory::MemoryResource memory_resource,
  const clockwork::Tappy<LogWriterConfig<>>& log_writer_config,
  const clockwork::Tappy<LoggerConfig>& logger_config,
  const clockwork::Tappy<ChannelMessageRatesConfig>& channel_rates_config)
  : LogWriterBase<MessageWriter>(
      memory_resource,
      log_writer_config,
      logger_config.get_pinion_shm_root(),
      logger_config.get_pinion_namespace(),
      calculate_write_buffer_pool_size(
        logger_config.get_max_write_mib_per_sec(), OnboardWriterPolicy::max_write_backlog),
      std::chrono::seconds(logger_config.get_max_log_file_duration_sec()),
      channel_rates_config),
    memory_resource_(std::move(memory_resource)),
    log_root_dir_(logger_config.get_log_root_dir(), memory_resource_)
{
}

[[nodiscard]] LogExpected<void> MessageWriter::start_logging(std::string_view log_directory_name)
{
  const auto state = get_state();
  switch (state)
  {
  case LogWriterState::stopped:
  {
    jewels::filesystem::Filesystem filesys{memory_resource_};
    if (const auto mkdir_result = filesys.create_directories(log_root_dir_); !mkdir_result)
    {
      std::pmr::string status_string{memory_resource_};
      fmt::format_to(
        std::back_inserter(status_string), "Failed to create the log root directory: {}", mkdir_result.error());
      jewels::log_cerr_error("{}", status_string);
      set_state_to_failed(std::move(status_string));
      return jewels::unexpected(to_log_error(mkdir_result.error()));
    }
    const auto log_dir = log_root_dir_ / log_directory_name;
    if (const auto start_result = LogWriterBase<MessageWriter>::start_logging(log_dir.string_view()); !start_result)
    {
      return jewels::unexpected(start_result.error());
    }
  }
  break;
  case LogWriterState::paused:
    return jewels::unexpected(LogError::paused);
  case LogWriterState::logging:
  case LogWriterState::degraded:
    return jewels::unexpected(LogError::already_open);
  case LogWriterState::unspecified:
  case LogWriterState::failed:
    return jewels::unexpected(LogError::failed);
  }
  return {};
}

[[nodiscard]] LogExpected<void> MessageWriter::stop_logging()
{
  const auto state = get_state();
  switch (state)
  {
  case LogWriterState::stopped:
    return jewels::unexpected(LogError::not_open);
  case LogWriterState::logging:
  case LogWriterState::degraded:
  case LogWriterState::paused:
  {
    const auto stop_result = LogWriterBase<MessageWriter>::stop_logging();
    return stop_result;
  }
  case LogWriterState::unspecified:
  case LogWriterState::failed:
    return jewels::unexpected(LogError::failed);
  }
}

void MessageWriter::message_handler(std::string_view channel_name, const ::clockwork::pinion::SlotRef& message_handle)
{
  const auto state = get_state();
  if (state != LogWriterState::failed)
  {
    if (state == LogWriterState::degraded || state == LogWriterState::logging)
    {
      // Errors are handled by LogWriterBase
      if (const auto log_result = log_message(
            channel_name,
            message_handle,
            LogTimestamp{jewels::time::SyncClock::now()},
            jewels::time::SteadyClock::now());
          !log_result)
      {
        jewels::log_cerr_error("Unexpected log message failure: {}", log_result.error());
      }
    }
    else if (is_persistent_channel(channel_name))
    {
      // Errors are handled by LogWriterBase
      if (const auto log_result =
            save_persistent_message(channel_name, message_handle, LogTimestamp{jewels::time::SyncClock::now()});
          !log_result)
      {
        jewels::log_cerr_error("Unexpected log message failure: {}", log_result.error());
      }
    }
  }
}

[[nodiscard]] size_t MessageWriter::calculate_write_buffer_pool_size(
  size_t max_write_mib_per_sec, std::chrono::nanoseconds max_write_backlog) noexcept
{
  return onboard::Writer<OnboardWriterPolicy>::calculate_write_buffer_pool_size(
    max_write_mib_per_sec, max_write_backlog);
}

} // namespace clockwork_logging
