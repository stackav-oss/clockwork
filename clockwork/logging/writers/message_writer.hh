// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_writer_config_clk_cc.hh"
#include "clockwork/logging/writers/channel_message_rates_config_clk_cc.hh"
#include "clockwork/logging/writers/log_writer_base.hh"
#include "clockwork/logging/writers/logger_config_clk_cc.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"

#include <chrono>
#include <cstddef>
#include <memory_resource>
#include <string_view>

namespace clockwork_logging
{

/// Message writer that uses the log writer base to write messages to a log
///
/// This class is designed to run in a multithreaded environment where one thread
/// is writing messages to the log and another thread is reporting the status.
/// Unless otherwise indicated, methods on this class *SHALL ONLY* be called
/// from the thread doing the writing.
class MessageWriter : public LogWriterBase<MessageWriter>
{
public:
  /// Onboard writer policy
  using OnboardWriterPolicy = LogWriterBase<MessageWriter>::OnboardWriterPolicy;

  /// Construct a MessageWriter
  /// @param[in] memory_resource Memory resource
  /// @param[in] log_writer_config Generated clockwork log writer configuration
  /// @param[in] logger_config Logger configuration
  /// @param[in] channel_rates_config Channel message rates configuration
  MessageWriter(
    jewels::memory::MemoryResource memory_resource,
    const clockwork::Tappy<LogWriterConfig<>>& log_writer_config,
    const clockwork::Tappy<LoggerConfig>& logger_config,
    const clockwork::Tappy<ChannelMessageRatesConfig>& channel_rates_config);

  ~MessageWriter() noexcept override = default;
  MessageWriter(const MessageWriter&) = delete;
  MessageWriter& operator=(const MessageWriter&) = delete;
  MessageWriter(MessageWriter&&) = delete;
  MessageWriter& operator=(MessageWriter&&) = delete;

  /// Start logging
  /// @param[in] log_directory_name Log directory name
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> start_logging(std::string_view log_directory_name);

  /// Stop logging
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> stop_logging();

  /// Received message handler
  /// @param[in] channel_name Channel name
  /// @param[in] message_handle Clockwork message handle
  void message_handler(std::string_view channel_name, const ::clockwork::pinion::SlotRef& message_handle);

  /// Calculate the size of the message buffer pool from the maximum write rate, and max write backlog
  /// @param[in] max_write_mib_per_sec Maximum write rate in MiB per second
  /// @param[in] max_write_backlog Maximum write backlog
  [[nodiscard]] static size_t
  calculate_write_buffer_pool_size(size_t max_write_mib_per_sec, std::chrono::nanoseconds max_write_backlog) noexcept;

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Log root directory
  jewels::filesystem::Path log_root_dir_;
};

} // namespace clockwork_logging
