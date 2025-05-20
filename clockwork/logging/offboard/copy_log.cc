// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/copy_log.hh"

#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

namespace
{

/// Open the source log reader
/// @param[in] memory_resource Memory resource
/// @param[in] source_uri Source log URI
/// @param[in] maybe_desired_channels Optional set of desired channels
/// @param[in] maybe_log_interval Optional relative log interval
/// @return Reader pointer or LogError on failure
[[nodiscard]] LogExpected<std::unique_ptr<AbstractLogReader>> open_reader(
  jewels::memory::MemoryResource memory_resource,
  std::string_view source_uri,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  const std::optional<RelativeInterval>& maybe_log_interval)
{
  const auto topic_filter = [memory_resource, maybe_desired_channels](std::string_view topic)
  { return !maybe_desired_channels || maybe_desired_channels->contains(std::pmr::string{topic, memory_resource}); };
  try
  {
    auto reader_ptr = make_reader(source_uri, {}, maybe_log_interval, DecompressOption::dont_decompress);
    if (const auto open_result = reader_ptr->open(topic_filter); !open_result)
    {
      return jewels::unexpected(open_result.error());
    }
    return {std::move(reader_ptr)};
  }
  catch (const std::invalid_argument& exc)
  {
    jewels::log_cerr_error("{}", exc.what());
    return jewels::unexpected(LogError::failed_to_open_log_file);
  }
  __builtin_unreachable();
}

/// Open the destination log writer
/// @param[in] memory_resource Memory resource
/// @param[in] dest_uri Destination log URI
/// @param[in] write_config_str Write configuration protobuf string
/// @return Writer pointer or LogError on faulure
[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<Writer>> open_writer(
  jewels::memory::MemoryResource memory_resource, std::string_view dest_uri, std::string_view writer_config_str)
{
  auto writer_ptr = jewels::memory::make_shared<Writer>(memory_resource);
  if (const auto open_result = writer_ptr->open(dest_uri, writer_config_str); !open_result)
  {
    return jewels::unexpected(open_result.error());
  }
  return {std::move(writer_ptr)};
}

/// Copy the logged messages from the source log to the destination log
/// @param[in] reader Source log reader
/// @param[in] writer Destination log writer
/// @return LogError on failure
[[nodiscard]] LogExpected<void> copy_log(AbstractLogReader& reader, Writer& writer)
{
  const auto log_metadata = reader.get_metadata();
  std::unordered_map<std::string_view, jewels::memory::ObjectPtr<const TopicMetadata>> topic_map;
  for (const auto& metadata : log_metadata)
  {
    topic_map.emplace(metadata.name, jewels::memory::make_non_null_from_ref(metadata));
  }
  while (true)
  {
    const auto maybe_message = reader.next_message();
    if (!maybe_message)
    {
      break;
    }
    const auto topic_iter = topic_map.find(maybe_message->topic);
    if (topic_iter != topic_map.end())
    {
      const auto& topic_metadata = *(topic_iter->second);
      if (const auto create_result = writer.create_channel(LoggedChannelMetadata{
            .channel_name = topic_metadata.name,
            .message_encoding = topic_metadata.message_encoding,
            .channel_type = topic_metadata.channel_type,
            .schema_name = topic_metadata.type,
            .schema_encoding = topic_metadata.schema_encoding,
            .schema_definition = topic_metadata.schema_definition,
          });
          !create_result)
      {
        return jewels::unexpected(create_result.error());
      }
      topic_map.erase(topic_iter);
    }
    if (const auto write_result = writer.write(LoggedMessage{
          .channel_name = maybe_message->topic,
          .sequence_number = maybe_message->sequence_number,
          .log_time = maybe_message->log_time,
          .transmit_time = maybe_message->publish_time,
          .header = maybe_message->header,
          .data = maybe_message->data,
          .is_lite_compressed = maybe_message->is_lite_compressed,
        });
        !write_result)
    {
      return jewels::unexpected(write_result.error());
    }
  }
  if (const auto close_result = writer.close(); !close_result)
  {
    return jewels::unexpected(close_result.error());
  }
  return {};
}

} // namespace

[[nodiscard]] LogExpected<void> copy_log(
  jewels::memory::MemoryResource memory_resource,
  std::string_view source_uri,
  std::string_view dest_uri,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  const std::optional<RelativeInterval>& maybe_log_interval,
  std::string_view writer_config_str)
{
  const auto reader_result = open_reader(memory_resource, source_uri, maybe_desired_channels, maybe_log_interval);
  if (!reader_result)
  {
    jewels::log_cerr_error("Failed to open {} for read: {}", source_uri, reader_result.error());
    return jewels::unexpected(reader_result.error());
  }
  const auto writer_result = open_writer(memory_resource, dest_uri, writer_config_str);
  if (!writer_result)
  {
    jewels::log_cerr_error("Failed to open {} for write: {}", dest_uri, writer_result.error());
    return jewels::unexpected(reader_result.error());
  }
  if (const auto copy_result = copy_log(*reader_result.value(), *writer_result.value()); !copy_result)
  {
    jewels::log_cerr_error("Failed to copy log: {}", copy_result.error());
    return jewels::unexpected(copy_result.error());
  }
  return {};
}

} // namespace clockwork_logging::offboard
