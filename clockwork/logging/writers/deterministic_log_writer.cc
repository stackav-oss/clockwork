// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/writers/deterministic_log_writer.hh"

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/runners/channel_publisher.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <span>
#include <utility>

namespace clockwork_logging
{

LogMessageWriter::LogMessageWriter(
  jewels::memory::MemoryResource memory_resource,
  jewels::memory::ObjectPtr<const clockwork::Tappy<LogWriterConfig<>>> log_writer_config,
  std::pmr::vector<PersistentLogEntry> persistent_entries,
  ChannelMap channels,
  std::string_view log_uri,
  jewels::time::SyncTime init_time)
  : memory_resource_(std::move(memory_resource)),
    log_uri_(log_uri, memory_resource_),
    log_writer_config_(log_writer_config),
    persistent_entries_(std::move(persistent_entries)),
    writer_(memory_resource_),
    channels_(std::move(channels)),
    init_time_(init_time)
{
}

void LogMessageWriter::message_received_callback(::clockwork::MessageInfoView message_info)
{
  auto logged_msg = offboard::LoggedMessage{
    .channel_name = std::move(message_info.channel),
    .sequence_number = message_info.sequence_number,
    .log_time = LogTimestamp(message_info.time_to_publish),
    .transmit_time = LogTimestamp(message_info.time_to_publish),
    .data = message_info.data};

  if (auto write_status = writer_.write(logged_msg); !write_status)
  {
    jewels::log_cerr_error("Error writing to log: {}", write_status.error());
  }
}

jewels::expected<void, jewels::MonoError> LogMessageWriter::initialize()
{
  if (auto open_status = writer_.open(log_uri_); !open_status)
  {
    jewels::log_cerr_error("Error opening log file: {}", open_status.error());
    return jewels::unexpected(jewels::MonoError{});
  }

  for (const auto& channel_config : log_writer_config_->get_channels())
  {
    auto writer_status = writer_.create_channel(
      offboard::LoggedChannelMetadata{
        .channel_name = channel_config.get_channel_name(),
        .message_encoding = MessageEncoding::tachyon,
        .channel_type = channel_config.get_channel_type(),
        .schema_name = channel_config.get_schema_name(),
        .schema_encoding = channel_config.get_schema_encoding(),
        .schema_definition = nolint_helper::byte_span_to_string_view(channel_config.get_schema_definition())});
    // There may be multiple entries for the same channel if this is a multiple publisher channel, so just ignore the
    // channel already exists error.
    if (!writer_status && (writer_status.error() != LogError::channel_already_exists))
    {
      jewels::log_cerr_error("Error initializing log writer {}", writer_status.error());
      return jewels::unexpected(jewels::MonoError{});
    }
  }

  return write_persistent_entries();
}

jewels::expected<void, jewels::MonoError> LogMessageWriter::write_persistent_entries()
{
  for (const auto& entry : persistent_entries_)
  {
    auto channel_status = writer_.create_channel(
      offboard::LoggedChannelMetadata{
        .channel_name = entry.channel_name,
        .message_encoding = MessageEncoding::tachyon,
        .channel_type = ChannelType::persistent,
        .schema_name = entry.schema_name,
        .schema_encoding = entry.schema_encoding,
        .schema_definition = entry.schema_definition});
    if (!channel_status)
    {
      jewels::log_cerr_error("Error creating persistent channel '{}': {}", entry.channel_name, channel_status.error());
      return jewels::unexpected(jewels::MonoError{});
    }

    auto write_status = writer_.write(
      offboard::LoggedMessage{
        .channel_name = entry.channel_name,
        .sequence_number = 0,
        .log_time = LogTimestamp{init_time_},
        .transmit_time = LogTimestamp{init_time_},
        .data = entry.data});
    if (!write_status)
    {
      jewels::log_cerr_error("Error writing persistent entry '{}': {}", entry.channel_name, write_status.error());
      return jewels::unexpected(jewels::MonoError{});
    }
  }

  return {};
}

LogMessageWriter::~LogMessageWriter()
{
  auto close_status = writer_.close();
  if (!close_status)
  {
    jewels::log_cerr_error("Error closing log writer in destructor: {}", close_status.error());
  }
}
} // namespace clockwork_logging
