// IWYU pragma: private, include "clockwork/logging/realtime_playback/converter_writer.hh"
// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/realtime_playback/converter_writer.hh"

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/realtime_playback/converter_request.hh"
#include "clockwork/logging/realtime_playback/converter_setup.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/scope_guard/scope_guard.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"
#include "jewels/std/span.hh"
#include "jewels/time/sync_time.hh"

#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace clockwork_logging::realtime_playback
{

template <typename Target>
jewels::BinaryOutcome ConverterMessageWriter::write(
  jewels::InOut<ConverterWriter> writer, const Target& target, const ConverterMessageView& message)
{
  if (jewels::fails(upgrade_converter_message(
        jewels::Out{upgraded_},
        *target.upgrader,
        target.target_message_size,
        message.data,
        target.destination_channel_name)))
  {
    return jewels::failure;
  }
  const auto compressed_spans = compressor_.compress(upgraded_);
  return writer->log_message_wait(
           onboard::ZeroCopyMessage{
             .channel_name = target.destination_channel_name,
             .sequence_number = message.sequence_number,
             .log_time = message.log_time,
             .message_time = message.publish_time,
             .header = message.header,
             .data = compressed_spans,
           },
           true,
           jewels::time::SteadyClock::now())
           ? jewels::success
           : jewels::failure;
}

template <typename Target>
jewels::BinaryOutcome add_converter_channel(jewels::InOut<ConverterWriter> writer, const Target& target)
{
  const auto schema_chars = jewels::as_chars(std::span{target.target_schema_definition});
  return writer->add_channel(
           onboard::LoggedChannelMetadata{
             .channel_name = target.destination_channel_name,
             .compression_type = CompressionType::none,
             .message_encoding = MessageEncoding::tachyon,
             .channel_type = ChannelType::regular,
             .schema_name = target.target_schema_name,
             .schema_encoding = SchemaEncoding::clockwork_tachyon,
             .schema_definition = std::string{schema_chars.data(), schema_chars.size()},
           },
           jewels::time::SteadyClock::now())
           ? jewels::success
           : jewels::failure;
}

template <typename NodeActiveCallable, typename AssignmentActiveCallable>
jewels::BinaryOutcome open_converter_node_writers(
  jewels::Out<ConverterNodeWriters> writers_out,
  const ConverterRequest& request,
  const ConverterSetup& setup,
  const std::string_view stream_name,
  const NodeActiveCallable& node_active_callable,
  const AssignmentActiveCallable& assignment_active_callable)
{
  ConverterNodeWriters writers;
  jewels::ScopeGuard cleanup{[&writers]() noexcept { cleanup_converter_writers(jewels::InOut{writers}); }};
  for (const auto& node : setup.nodes)
  {
    if (!node_active_callable(node))
    {
      continue;
    }
    const auto relative_path = (std::filesystem::path{"nodes"} / node.simplelaunch_node_name / stream_name).string();
    const auto absolute_path = std::filesystem::path{request.output_path} / relative_path;
    std::error_code error;
    std::filesystem::create_directories(absolute_path, error);
    if (error)
    {
      jewels::log_cerr_error(
        "Failed to create {} log directory {}: {}", stream_name, absolute_path.string(), error.message());
      return jewels::failure;
    }
    std::unique_ptr<ConverterWriter> writer;
    if (jewels::fails(open_converter_writer(jewels::Out{writer}, absolute_path.string(), stream_name)))
    {
      return jewels::failure;
    }
    jewels::ScopeGuard writer_cleanup{[&writer]() noexcept
                                      {
                                        if (writer)
                                        {
                                          // Cleanup is best effort while unwinding a failed conversion.
                                          std::ignore = close_converter_writer(jewels::InOut{*writer});
                                        }
                                      }};
    for (const auto index : node.assignment_indices)
    {
      if (
        assignment_active_callable(index) &&
        jewels::fails(add_converter_channel(jewels::InOut{*writer}, setup.assignments.at(index))))
      {
        return jewels::failure;
      }
    }
    writers.emplace(
      node.simplelaunch_node_name, ConverterNodeWriter{.relative_path = relative_path, .writer = std::move(writer)});
    writer_cleanup.dismiss();
  }
  *writers_out = std::move(writers);
  cleanup.dismiss();
  return jewels::success;
}

template <typename Writers>
void cleanup_converter_writers(jewels::InOut<Writers> writers) noexcept
{
  for (auto& [_, item] : *writers)
  {
    if (item.writer)
    {
      // Cleanup is best effort while unwinding a failed conversion.
      std::ignore = close_converter_writer(jewels::InOut{*item.writer});
      item.writer.reset();
    }
  }
}

template <typename Writers>
jewels::BinaryOutcome finalize_converter_writers(jewels::InOut<Writers> writers, const std::string_view stream_name)
{
  bool all_succeeded = true;
  for (auto& [_, item] : *writers)
  {
    if (!item.writer || jewels::fails(close_converter_writer(jewels::InOut{*item.writer})))
    {
      jewels::log_cerr_error("Failed to close {} log: {}", stream_name, item.relative_path);
      all_succeeded = false;
    }
    item.writer.reset();
  }
  return all_succeeded ? jewels::success : jewels::failure;
}

} // namespace clockwork_logging::realtime_playback
