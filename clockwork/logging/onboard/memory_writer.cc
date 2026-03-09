// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/onboard/memory_writer.hh"

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer_state.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "clockwork/logging/zstd_helper.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory_resource>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::onboard
{

MemoryWriter::MemoryWriter(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource))
{
}

[[nodiscard]] LogExpected<void> MemoryWriter::open_log(std::span<std::byte> log_buffer)
{
  if (state_ != WriterState::closed)
  {
    jewels::log_cerr_error("Log is already open");
    return jewels::unexpected(LogError::already_open);
  }
  log_buffer_size_ = 0U;
  log_buffer_ = log_buffer;
  LogHeader log_header{};
  std::span<const std::byte> header_span = std::as_bytes(std::span{&log_header, 1U});
  if (const auto copy_result = copy_log_data({&header_span, 1U}); !copy_result)
  {
    return jewels::unexpected(copy_result.error());
  }
  maybe_min_log_timestamp_ = std::nullopt;
  maybe_max_log_timestamp_ = std::nullopt;
  maybe_min_message_timestamp_ = std::nullopt;
  maybe_max_message_timestamp_ = std::nullopt;
  schema_metadata_list_ = std::pmr::list<SchemaMetadata>{memory_resource_};
  channel_metadata_list_ = std::pmr::list<ChannelMetadata>{memory_resource_};
  schema_map_ =
    std::pmr::unordered_map<std::string_view, jewels::memory::ObjectPtr<const SchemaMetadata>>{memory_resource_};
  channel_map_ =
    std::pmr::unordered_map<std::string_view, jewels::memory::ObjectPtr<const ChannelMetadata>>{memory_resource_};
  schema_count_ = 0U;
  channel_count_ = 0U;
  state_ = WriterState::logging;
  return {};
}

[[nodiscard]] LogExpected<std::span<const std::byte>> MemoryWriter::close_log()
{
  if (state_ != WriterState::logging)
  {
    jewels::log_cerr_error("Log is not open");
    return jewels::unexpected(LogError::not_open);
  }
  write_end_log_file_record();
  const auto log_buffer_span = log_buffer_.first(log_buffer_size_);
  log_buffer_size_ = 0U;
  log_buffer_ = {};
  schema_metadata_list_.clear();
  channel_metadata_list_.clear();
  schema_map_.clear();
  channel_map_.clear();
  state_ = WriterState::closed;
  return log_buffer_span;
}

[[nodiscard]] LogExpected<void> MemoryWriter::add_channel_impl(const LoggedChannelMetadata& channel_metadata)
{
  if (channel_metadata.compression_type != CompressionType::none)
  {
    jewels::log_cerr_error("Unsupported compression type");
    return jewels::unexpected(LogError::unsupported_compression_type);
  }
  auto schema_encoding = channel_metadata.schema_encoding;
  auto schema_definition = channel_metadata.schema_definition;
  LogExpected<std::pmr::vector<std::byte>> compress_result;
  if (schema_encoding == SchemaEncoding::clockwork_tachyon)
  {
    compress_result =
      zstd_compress(std::as_bytes(std::span{schema_definition.data(), schema_definition.size()}), memory_resource_);
    if (!compress_result)
    {
      jewels::log_cerr_error(
        "Failed to compress schema definition for {}: {}", channel_metadata.channel_name, compress_result.error());
      return jewels::unexpected(compress_result.error());
    }
    schema_definition = nolint_helper::byte_span_to_string_view(compress_result.value()),
    schema_encoding = SchemaEncoding::clockwork_tachyon_zstd;
  }
  LogExpected<void> add_result{};
  uint16_t schema_id{0U};
  if (!channel_metadata.schema_name.empty())
  {
    if (const auto schema_map_iter = schema_map_.find(channel_metadata.schema_name);
        schema_map_iter != schema_map_.end())
    {
      schema_id = schema_map_iter->second->schema_id;
    }
    else
    {
      auto schema_ptr = add_schema_metadata(channel_metadata.schema_name, schema_encoding, schema_definition);
      schema_id = schema_ptr->schema_id;
      if (const auto write_result = write_schema_metadata(*schema_ptr); !write_result && add_result)
      {
        add_result = write_result;
      }
    }
  }
  auto channel_ptr = add_channel_metadata(
    channel_metadata.channel_name,
    channel_metadata.compression_type,
    channel_metadata.message_encoding,
    channel_metadata.channel_type,
    schema_id);
  if (const auto write_result = write_channel_metadata(*channel_ptr); !write_result && add_result)
  {
    add_result = write_result;
  }
  return add_result;
}

[[nodiscard]] LogExpected<void> MemoryWriter::add_channel(const LoggedChannelMetadata& channel_metadata)
{
  if (state_ != WriterState::logging)
  {
    return jewels::unexpected{LogError::not_open};
  }
  if (channel_map_.contains(channel_metadata.channel_name))
  {
    return {};
  }
  if (channel_metadata.schema_name.size() > max_name_string_size)
  {
    jewels::log_cerr_error("Schema name exceeds max size ({})", max_name_string_size);
    return jewels::unexpected(LogError::schema_name_exceeds_max_name_size);
  }
  if (channel_metadata.channel_name.size() > max_name_string_size)
  {
    jewels::log_cerr_error("Channel name exceeds max size ({})", max_name_string_size);
    return jewels::unexpected(LogError::channel_name_exceeds_max_name_size);
  }
  if (channel_metadata.schema_definition.size() > max_schema_definition_string_size)
  {
    jewels::log_cerr_error("Schema definition exceeds max size ({})", max_schema_definition_string_size);
    return jewels::unexpected(LogError::schema_definition_exceeds_max_size);
  }
  return add_channel_impl(channel_metadata);
}

[[nodiscard]] LogExpected<void>
MemoryWriter::log_message(const Message& message, bool is_lite_compressed, bool is_repeated_persistent)
{
  return log_message(
    ZeroCopyMessage{
      .channel_name = message.channel_name,
      .sequence_number = message.sequence_number,
      .log_time = message.log_time,
      .message_time = message.message_time,
      .header = message.header,
      .data = std::span{&message.data, 1U},
    },
    is_lite_compressed,
    is_repeated_persistent);
}

[[nodiscard]] LogExpected<void>
MemoryWriter::log_message(const ZeroCopyMessage& message, bool is_lite_compressed, bool is_repeated_persistent)
{
  MessageRecordHeader record_header{};
  if (const auto header_result =
        fill_message_record_header(message, is_lite_compressed, is_repeated_persistent, record_header);
      !header_result)
  {
    return jewels::unexpected(header_result.error());
  }
  RecordTrailer record_trailer{};
  std::pmr::vector<std::span<const std::byte>> record_spans{memory_resource_};
  record_spans.reserve(message.data.size() + 3U);
  record_spans.emplace_back(std::as_bytes(std::span{&record_header, 1U}));
  record_spans.emplace_back(message.header);
  record_spans.insert(record_spans.end(), message.data.begin(), message.data.end());
  record_spans.emplace_back(std::as_bytes(std::span{&record_trailer, 1U}));
  record_trailer.xxh3_checksum =
    compute_xxh3_checksum(std::span{record_spans.data(), record_spans.size()}.first(record_spans.size() - 1U));
  if (const auto copy_result = copy_log_data({record_spans}); !copy_result)
  {
    return jewels::unexpected(copy_result.error());
  }
  if (!record_header.flags.is_repeated)
  {
    update_log_time_range(message.message_time, message.log_time);
  }
  return {};
}

[[nodiscard]] LogExpected<void> MemoryWriter::fill_message_record_header(
  const ZeroCopyMessage& message,
  bool is_lite_compressed,
  bool is_repeated_persistent,
  MessageRecordHeader& record_header)
{
  const auto data_size = std::accumulate(
    message.data.begin(), message.data.end(), size_t{0U}, [](size_t lhs, auto& rhs) { return lhs + rhs.size(); });
  if (message.header.size() > max_message_header_size)
  {
    jewels::log_cerr_error("Message header exceeds max size");
    return jewels::unexpected(LogError::message_header_exceeds_max_size);
  }
  const auto record_size = message_record_header_size + message.header.size() + data_size + sizeof(RecordTrailer);
  if (record_size > max_log_record_size)
  {
    jewels::log_cerr_error("Message record length exceeds max size");
    return jewels::unexpected(LogError::record_length_exceeds_max_record_size);
  }
  const auto channel_map_iter = channel_map_.find(message.channel_name);
  if (channel_map_iter == channel_map_.end())
  {
    jewels::log_cerr_error("Channel metadata for {} is not configured", message.channel_name);
    return jewels::unexpected(LogError::missing_channel_metadata);
  }
  if (is_repeated_persistent && channel_map_iter->second->channel_type != ChannelType::persistent)
  {
    jewels::log_cerr_error("Messages on non-persistent channels cannot be repeated persistent");
    return jewels::unexpected(LogError::not_persistent);
  }
  const auto channel_id = channel_map_iter->second->channel_id;
  record_header.header.record_type = RecordType::message;
  record_header.header.record_size = static_cast<uint32_t>(record_size);
  record_header.channel_id = channel_id;
  record_header.sequence_number = message.sequence_number;
  record_header.log_time_ns = message.log_time.get_nanoseconds();
  record_header.message_time_ns = message.message_time.get_nanoseconds();
  const auto is_persistent = channel_map_iter->second->channel_type == ChannelType::persistent;
  record_header.flags.is_persistent = is_persistent ? 1U : 0U;
  record_header.flags.is_repeated = is_repeated_persistent ? 1U : 0U;
  record_header.flags.is_lite_compressed = is_lite_compressed ? 1U : 0U;
  record_header.header_length = static_cast<uint16_t>(message.header.size());
  return {};
}

[[nodiscard]] jewels::memory::ObjectPtr<const typename MemoryWriter::SchemaMetadata> MemoryWriter::add_schema_metadata(
  std::string_view schema_name, SchemaEncoding schema_encoding, std::string_view schema_definition)
{
  if (const auto map_iter = schema_map_.find(schema_name); map_iter != schema_map_.end())
  {
    return map_iter->second;
  }
  ++schema_count_;
  const auto& schema_metadata = schema_metadata_list_.emplace_back(
    SchemaMetadata{
      .schema_id = schema_count_,
      .schema_name = std::pmr::string{schema_name, memory_resource_},
      .schema_encoding = schema_encoding,
      .schema_definition = std::pmr::string{schema_definition, memory_resource_}});
  const auto schema_ptr = jewels::memory::make_non_null_from_ref(schema_metadata);
  schema_map_.emplace(schema_ptr->schema_name, schema_ptr);
  return schema_ptr;
}

[[nodiscard]] LogExpected<void> MemoryWriter::write_schema_metadata(const SchemaMetadata& schema_metadata)
{
  const auto record_size = sizeof(SchemaRecordHeader) + schema_metadata.schema_name.size() +
                           schema_metadata.schema_definition.size() + sizeof(RecordTrailer);
  if (record_size > max_log_record_size)
  {
    jewels::log_cerr_error("Schema record exceeds max size");
    return jewels::unexpected(LogError::record_length_exceeds_max_record_size);
  }
  SchemaRecordHeader header{};
  header.header.record_type = RecordType::schema;
  header.header.record_size = static_cast<uint32_t>(record_size);
  header.schema_id = schema_metadata.schema_id;
  header.schema_encoding = schema_metadata.schema_encoding;
  header.schema_name_length = static_cast<uint16_t>(schema_metadata.schema_name.size());
  RecordTrailer trailer{};
  std::array data_spans = {
    std::as_bytes(std::span{&header, 1U}),
    std::as_bytes(std::span{schema_metadata.schema_name.data(), schema_metadata.schema_name.size()}),
    std::as_bytes(std::span{schema_metadata.schema_definition.data(), schema_metadata.schema_definition.size()}),
    std::as_bytes(std::span{&trailer, 1U})};
  trailer.xxh3_checksum = compute_xxh3_checksum(std::span{data_spans}.first(data_spans.size() - 1U));
  return copy_log_data({data_spans});
}

[[nodiscard]] jewels::memory::ObjectPtr<const typename MemoryWriter::ChannelMetadata>
MemoryWriter::add_channel_metadata(
  std::string_view channel_name,
  CompressionType compression_type,
  MessageEncoding message_encoding,
  ChannelType channel_type,
  uint16_t schema_id)
{
  if (const auto map_iter = channel_map_.find(channel_name); map_iter != channel_map_.end())
  {
    return map_iter->second;
  }
  ++channel_count_;
  const auto& channel_metadata = channel_metadata_list_.emplace_back(
    ChannelMetadata{
      .channel_id = channel_count_,
      .schema_id = schema_id,
      .channel_name = std::pmr::string{channel_name, memory_resource_},
      .compression_type = compression_type,
      .message_encoding = message_encoding,
      .channel_type = channel_type});
  const auto channel_ptr = jewels::memory::make_non_null_from_ref(channel_metadata);
  channel_map_.emplace(channel_ptr->channel_name, channel_ptr);
  return channel_ptr;
}

[[nodiscard]] LogExpected<void> MemoryWriter::write_channel_metadata(const ChannelMetadata& channel_metadata)
{
  const auto record_size = sizeof(ChannelRecordHeader) + channel_metadata.channel_name.size() + sizeof(RecordTrailer);
  if (record_size > max_log_record_size)
  {
    jewels::log_cerr_error("Record length exceeds max record size");
    return jewels::unexpected(LogError::record_length_exceeds_max_record_size);
  }
  ChannelRecordHeader header{};
  header.header.record_type = RecordType::channel;
  header.header.record_size = static_cast<uint32_t>(record_size);
  header.channel_id = channel_metadata.channel_id;
  header.schema_id = channel_metadata.schema_id;
  header.compression_type = channel_metadata.compression_type;
  header.message_encoding = channel_metadata.message_encoding;
  header.flags.is_persistent = channel_metadata.channel_type == ChannelType::persistent ? 1U : 0U;
  RecordTrailer trailer{};
  std::array data_spans{
    std::as_bytes(std::span{&header, 1U}),
    std::as_bytes(std::span{channel_metadata.channel_name.data(), channel_metadata.channel_name.size()}),
    std::as_bytes(std::span{&trailer, 1U})};
  trailer.xxh3_checksum = compute_xxh3_checksum(std::span{data_spans}.first(data_spans.size() - 1U));
  return copy_log_data({data_spans});
}

void MemoryWriter::write_end_log_file_record()
{
  EndLogFileRecordHeader header{};
  header.header.record_type = RecordType::end_log_file;
  header.header.record_size = static_cast<uint32_t>(sizeof(EndLogFileRecordHeader) + sizeof(RecordTrailer));
  header.min_log_time_ns = maybe_min_log_timestamp_.value_or(LogTimestamp{0}).get_nanoseconds();
  header.max_log_time_ns = maybe_max_log_timestamp_.value_or(LogTimestamp{0}).get_nanoseconds();
  header.min_message_time_ns = maybe_min_message_timestamp_.value_or(LogTimestamp{0}).get_nanoseconds();
  header.max_message_time_ns = maybe_max_message_timestamp_.value_or(LogTimestamp{0}).get_nanoseconds();
  header.has_messages = static_cast<bool>(maybe_min_log_timestamp_);
  RecordTrailer trailer{};
  std::array data_spans{std::as_bytes(std::span{&header, 1U}), std::as_bytes(std::span{&trailer, 1U})};
  trailer.xxh3_checksum = compute_xxh3_checksum(std::as_bytes(jewels::as_single_item_span(header)));
  if (const auto copy_result = copy_log_data({data_spans}); !copy_result)
  {
    // TODO(OI-3032): Replace with better observability/contract mechanism when one is available
    jewels::log_cerr_error("Failed to write the end log file record");
  }
}

LogExpected<void> MemoryWriter::copy_log_data(std::span<std::span<const std::byte>> data_spans)
{
  for (const auto data : data_spans)
  {
    if (const auto copy_result = copy_log_data(data); !copy_result)
    {
      return copy_result;
    }
  }
  return {};
}

LogExpected<void> MemoryWriter::copy_log_data(std::span<const std::byte> data)
{
  if (data.empty())
  {
    return {};
  }
  if (log_buffer_.size() - log_buffer_size_ < data.size())
  {
    jewels::log_cerr_error(
      "Log buffer is full, have {} bytes, need {}", log_buffer_.size() - log_buffer_size_, data.size());
    return jewels::unexpected(LogError::buffer_full);
  }
  const auto offset = log_buffer_size_;
  log_buffer_size_ += data.size();
  std::memcpy(&log_buffer_[offset], data.data(), data.size());
  return {};
}

void MemoryWriter::update_log_time_range(LogTimestamp message_time, LogTimestamp log_time)
{
  maybe_min_log_timestamp_ = std::min(log_time, maybe_min_log_timestamp_.value_or(log_time));
  maybe_max_log_timestamp_ = std::max(log_time, maybe_max_log_timestamp_.value_or(log_time));
  maybe_min_message_timestamp_ = std::min(message_time, maybe_min_message_timestamp_.value_or(message_time));
  maybe_max_message_timestamp_ = std::max(message_time, maybe_max_message_timestamp_.value_or(message_time));
}

} // namespace clockwork_logging::onboard
