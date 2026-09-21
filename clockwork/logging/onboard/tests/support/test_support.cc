// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/onboard/tests/support/test_support.hh"

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "clockwork/logging/zstd_helper.hh"
#include "jewels/container/at.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/default_memory_resource.hh"
#include "jewels/memory/memory_resource.hh"

#include <fmt/base.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fcntl.h>
#include <fstream> // IWYU pragma: keep
#include <iostream>
#include <iterator>
#include <limits>
#include <memory_resource>
#include <numeric>
#include <random>
#include <string>
#include <sys/types.h>
#include <utility>

namespace clockwork_logging::onboard::tests
{

void dump_data_span(std::string_view label, std::span<const std::byte> data)
{
  std::string dump_str;
  dump_str.reserve(data.size() * 3U);
  for (const auto val : data)
  {
    fmt::format_to(std::back_inserter(dump_str), " {:02x}", val);
  }
  std::cerr << label << ":" << dump_str << '\n';
}

[[nodiscard]] jewels::expected<std::vector<char>, jewels::MonoError> try_read_file(std::string_view file_path)
{
  const std::string path_str{file_path};
  const auto maybe_file_size =
    jewels::filesystem::Filesystem{jewels::memory::get_default_memory_resource()}.get_size(file_path);
  if (!maybe_file_size)
  {
    jewels::log_cerr_error("Failed to get size of '{}'", file_path);
    return jewels::unexpected(jewels::MonoError{});
  }
  const auto file_size = *maybe_file_size;
  std::ifstream ifs(path_str, std::ios::binary);
  if (!ifs)
  {
    jewels::log_cerr_error("Failed to open '{}'", path_str);
    return jewels::unexpected(jewels::MonoError{});
  }
  std::vector<char> data;
  data.reserve(file_size);
  data.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
  return {std::move(data)};
}

[[nodiscard]] jewels::expected<size_t, jewels::MonoError> try_validate_log_header(std::span<const char> file_data)
{
  if (file_data.size() < log_header_size)
  {
    jewels::log_cerr_error("file data smaller than log header, size: {}", file_data.size());
    return jewels::unexpected(jewels::MonoError{});
  }
  const LogHeader expected_header{};
  LogHeader actual_header{};
  std::memcpy(&actual_header, file_data.data(), log_header_size);
  if (actual_header.magic_number != expected_header.magic_number)
  {
    jewels::log_cerr_error("Invalid log magic number");
    dump_data_span("magic_number", std::as_bytes(std::span{actual_header.magic_number}));
    return jewels::unexpected(jewels::MonoError{});
  }
  return log_header_size;
}

[[nodiscard]] jewels::expected<size_t, jewels::MonoError>
try_validate_pad_bytes(std::span<const char> file_data, size_t offset, size_t length)
{
  if (offset + length > file_data.size())
  {
    jewels::log_cerr_error("File data too small for pad bytes need: {}, size: {}", offset + length, file_data.size());
    return jewels::unexpected(jewels::MonoError{});
  }
  for (auto i = static_cast<ssize_t>(offset); i < static_cast<ssize_t>(offset + length); ++i)
  {
    if (jewels::at(file_data, i) != '\0')
    {
      jewels::log_cerr_error("Non zero pad byte ({:2x}) at offset: {}", jewels::at(file_data, i), i);
      return jewels::unexpected(jewels::MonoError{});
    }
  }
  return offset + length;
}

[[nodiscard]] jewels::expected<void, jewels::MonoError>
try_validate_record_header(const RecordHeader& record_header, size_t record_size, RecordType record_type)
{
  if (record_header.magic_number != record_magic_number)
  {
    jewels::log_cerr_error("Invalid record magic number");
    dump_data_span("magic_number", std::as_bytes(std::span{record_header.magic_number}));
    return jewels::unexpected(jewels::MonoError{});
  }
  if (record_header.record_size != static_cast<uint32_t>(record_size))
  {
    jewels::log_cerr_error(
      "Invalid record size: {}, expected: {}", static_cast<uint32_t>(record_header.record_size), record_size);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (record_header.record_type != record_type)
  {
    jewels::log_cerr_error(
      "Invalid record type: {}: expected: {}", static_cast<uint16_t>(record_header.record_type), record_type);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (!std::ranges::all_of(record_header.reserved, [](const auto val) { return val == std::byte{0}; }))
  {
    jewels::log_cerr_error("Non zero header reserved field");
    dump_data_span("header.reserved", record_header.reserved);
    return jewels::unexpected(jewels::MonoError{});
  }
  return {};
}

[[nodiscard]] jewels::expected<void, jewels::MonoError>
try_validate_record_trailer(std::span<const std::byte> record_data)
{
  if (record_data.size() < record_trailer_size)
  {
    jewels::log_cerr_error("Record data too small for record trailer: size: {}", record_data.size());
    return jewels::unexpected(jewels::MonoError{});
  }
  const std::array checksum_spans = {record_data.first(record_data.size() - record_trailer_size)};
  return try_validate_record_trailer(checksum_spans, record_data.last(record_trailer_size));
}

[[nodiscard]] jewels::expected<void, jewels::MonoError> try_validate_record_trailer(
  std::span<const std::span<const std::byte>> checksum_spans, std::span<const std::byte> trailer_data)
{
  RecordTrailer trailer{};
  std::memcpy(&trailer, trailer_data.data(), record_trailer_size);
  const auto xxh3_checksum = compute_xxh3_checksum(checksum_spans);
  if (trailer.xxh3_checksum != xxh3_checksum)
  {
    jewels::log_cerr_error(
      "Record checksum mismatch, actual: 0x{:x}, expected: 0x{:x}",
      static_cast<size_t>(trailer.xxh3_checksum),
      xxh3_checksum);
    return jewels::unexpected(jewels::MonoError{});
  }
  return {};
}

[[nodiscard]] jewels::expected<size_t, jewels::MonoError> try_validate_schema_record(
  std::span<const char> file_data,
  size_t offset,
  const LoggedChannelMetadata& channel_metadata,
  const std::unordered_map<std::string_view, uint16_t>& schema_id_map)
{
  LogExpected<std::pmr::vector<std::byte>> compress_result;
  auto schema_definition = channel_metadata.schema_definition;
  auto schema_encoding = channel_metadata.schema_encoding;
  if (schema_encoding == SchemaEncoding::clockwork_tachyon)
  {
    const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
    compress_result =
      zstd_compress(std::as_bytes(std::span{schema_definition.data(), schema_definition.size()}), memory_resource);
    if (!compress_result)
    {
      jewels::log_cerr_error("Failed to compress schema definition: {}", compress_result.error());
      return jewels::unexpected(jewels::MonoError{});
    }
    schema_definition = nolint_helper::byte_span_to_string_view(compress_result.value()),
    schema_encoding = SchemaEncoding::clockwork_tachyon_zstd;
  }
  const auto record_size =
    schema_record_header_size + channel_metadata.schema_name.size() + schema_definition.size() + record_trailer_size;
  if (offset + record_size > file_data.size())
  {
    jewels::log_cerr_error(
      "File data too small for schema record need: {}, size: {}", offset + record_size, file_data.size());
    return jewels::unexpected(jewels::MonoError{});
  }
  const std::span record_span{&jewels::at(file_data, static_cast<ssize_t>(offset)), record_size};
  SchemaRecordHeader header{};
  std::memcpy(&header, record_span.data(), schema_record_header_size);
  if (!try_validate_record_header(header.header, record_size, RecordType::schema))
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  if (header.schema_id != schema_id_map.at(channel_metadata.schema_name))
  {
    jewels::log_cerr_error("Invalid schema_id: {}", header.schema_id);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (header.schema_encoding != schema_encoding)
  {
    jewels::log_cerr_error("Invalid schema_encoding: {}", static_cast<uint16_t>(header.schema_encoding));
    return jewels::unexpected(jewels::MonoError{});
  }
  if (!std::ranges::all_of(header.reserved, [](const auto val) { return val == std::byte{0}; }))
  {
    jewels::log_cerr_error("Non zero reserved field");
    dump_data_span("reserved", header.reserved);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (header.schema_name_length != channel_metadata.schema_name.size())
  {
    jewels::log_cerr_error("Invalid schema name length: {}", header.schema_name_length);
    return jewels::unexpected(jewels::MonoError{});
  }
  const auto record_payload_size = record_size - schema_record_header_size - record_trailer_size;
  const auto record_payload_span = file_data.subspan(offset + schema_record_header_size, record_payload_size);
  const auto schema_definition_size = record_payload_size - header.schema_name_length;
  const auto schema_name_span = record_payload_span.first(header.schema_name_length);
  const std::string_view schema_name_str(schema_name_span.data(), schema_name_span.size());
  if (schema_name_str != channel_metadata.schema_name)
  {
    jewels::log_cerr_error("Invalid schema name: {}", schema_name_str);
    return jewels::unexpected(jewels::MonoError{});
  }
  const auto definition_span = record_payload_span.last(schema_definition_size);
  std::string_view definition_str(definition_span.data(), definition_span.size());
  if (definition_str != schema_definition)
  {
    jewels::log_cerr_error("Invalid schema definition: {}", definition_str);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (!try_validate_record_trailer(std::as_bytes(record_span)))
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  return offset + record_size;
}

[[nodiscard]] jewels::expected<size_t, jewels::MonoError> try_validate_channel_record(
  std::span<const char> file_data,
  size_t offset,
  const LoggedChannelMetadata& channel_metadata,
  const std::unordered_map<std::string_view, uint16_t>& schema_id_map,
  const std::unordered_map<std::string_view, uint16_t>& channel_id_map)
{
  const auto record_size = channel_record_header_size + channel_metadata.channel_name.size() + record_trailer_size;
  if (offset + record_size > file_data.size())
  {
    jewels::log_cerr_error(
      "File data too small for channel record need: {}, size: {}", offset + record_size, file_data.size());
    return jewels::unexpected(jewels::MonoError{});
  }
  const std::span record_span{&jewels::at(file_data, static_cast<ssize_t>(offset)), record_size};
  ChannelRecordHeader header{};
  std::memcpy(&header, record_span.data(), channel_record_header_size);
  if (!try_validate_record_header(header.header, record_size, RecordType::channel))
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  if (header.channel_id != channel_id_map.at(channel_metadata.channel_name))
  {
    jewels::log_cerr_error("Invalid channel_id: {}", header.channel_id);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (header.schema_id != schema_id_map.at(channel_metadata.schema_name))
  {
    jewels::log_cerr_error("Invalid schema_id: {}", header.schema_id);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (header.compression_type != channel_metadata.compression_type)
  {
    jewels::log_cerr_error("Invalid compression_type: {}", static_cast<uint16_t>(header.compression_type));
    return jewels::unexpected(jewels::MonoError{});
  }
  if (header.message_encoding != channel_metadata.message_encoding)
  {
    jewels::log_cerr_error("Invalid message_encoding: {}", static_cast<uint16_t>(header.message_encoding));
    return jewels::unexpected(jewels::MonoError{});
  }
  if (header.flags.reserved != 0U)
  {
    jewels::log_cerr_error("Non-zero reserved flags: {}", static_cast<uint16_t>(header.flags.reserved));
    return jewels::unexpected(jewels::MonoError{});
  }
  if (header.flags.is_persistent != (channel_metadata.channel_type == ChannelType::persistent ? 1U : 0U))
  {
    jewels::log_cerr_error("Invalid is_persistent flag: {}", static_cast<uint16_t>(header.flags.is_persistent));
    return jewels::unexpected(jewels::MonoError{});
  }
  if (!std::ranges::all_of(header.reserved, [](const auto val) { return val == std::byte{0}; }))
  {
    jewels::log_cerr_error("Non zero reserved field");
    dump_data_span("reserved", header.reserved);
    return jewels::unexpected(jewels::MonoError{});
  }
  const auto channel_name_size = record_size - channel_record_header_size - record_trailer_size;
  const auto channel_name_span = file_data.subspan(offset + channel_record_header_size, channel_name_size);
  const std::string_view channel_name_str(channel_name_span.data(), channel_name_span.size());
  if (channel_name_str != channel_metadata.channel_name)
  {
    jewels::log_cerr_error("Invalid channel name: {}", channel_name_str);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (!try_validate_record_trailer(std::as_bytes(record_span)))
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  return offset + record_size;
}

[[nodiscard]] jewels::expected<size_t, jewels::MonoError> try_validate_end_log_file_record(
  std::span<const char> file_data,
  size_t offset,
  bool has_messages,
  LogTimestamp min_log_time,
  LogTimestamp max_log_time,
  LogTimestamp min_message_time,
  LogTimestamp max_message_time)
{
  const auto record_size = end_log_file_record_header_size + record_trailer_size;
  if (offset + record_size > file_data.size())
  {
    jewels::log_cerr_error(
      "File data too small for end log file record need: {}, size: {}", offset + record_size, file_data.size());
    return jewels::unexpected(jewels::MonoError{});
  }
  const std::span record_span{&jewels::at(file_data, static_cast<ssize_t>(offset)), record_size};
  EndLogFileRecordHeader header{};
  std::memcpy(&header, record_span.data(), end_log_file_record_header_size);
  if (!try_validate_record_header(header.header, record_size, RecordType::end_log_file))
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  if (header.has_messages != has_messages)
  {
    jewels::log_cerr_error("Invalid has_messages: {}", header.has_messages ? "true" : "false");
    return jewels::unexpected(jewels::MonoError{});
  }
  if (header.min_log_time_ns != min_log_time.get_nanoseconds())
  {
    jewels::log_cerr_error("Invalid min_log_time_ns: {}", header.min_log_time_ns);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (header.max_log_time_ns != max_log_time.get_nanoseconds())
  {
    jewels::log_cerr_error("Invalid max_log_time_ns: {}", header.max_log_time_ns);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (header.min_message_time_ns != min_message_time.get_nanoseconds())
  {
    jewels::log_cerr_error("Invalid min_message_time_ns: {}", header.min_message_time_ns);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (header.max_message_time_ns != max_message_time.get_nanoseconds())
  {
    jewels::log_cerr_error("Invalid max_message_time_ns: {}", header.max_message_time_ns);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (!try_validate_record_trailer(std::as_bytes(record_span)))
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  return offset + record_size;
}

void fill_with_random_bytes(std::span<std::byte> buffer)
{
  std::random_device random_device;
  std::mt19937 gen(random_device());
  std::uniform_int_distribution<uint8_t> distrib(
    std::numeric_limits<uint8_t>::min(), std::numeric_limits<uint8_t>::max());
  for (auto& value : buffer)
  {
    value = static_cast<std::byte>(distrib(gen));
    // 'X' is used to corrupt log files
    while (value == std::byte{'X'})
    {
      value = static_cast<std::byte>(distrib(gen));
    }
  }
}

[[nodiscard]] jewels::expected<void, jewels::MonoError>
corrupt_log_file(std::string_view file_path, size_t offset, std::string_view data)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  jewels::filesystem::Filesystem kits_fs{memory_resource};
  auto open_result = kits_fs.open(file_path, O_WRONLY);
  if (!open_result || !kits_fs.write(*open_result, offset, std::as_bytes(std::span{data})) || !open_result->close())
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  return {};
}

[[nodiscard]] jewels::expected<size_t, jewels::MonoError> try_validate_message_record(
  std::span<const char> file_data,
  size_t offset,
  const Message& msg,
  const std::unordered_map<std::string_view, uint16_t>& channel_id_map,
  bool is_lite_compressed,
  bool is_persistent,
  bool is_repeated)
{
  if (offset + message_record_header_size > file_data.size())
  {
    jewels::log_cerr_error(
      "File data too small for message record header: {}, size: {}",
      offset + message_record_header_size,
      file_data.size());
    return jewels::unexpected(jewels::MonoError{});
  }
  const std::span record_header_span{&jewels::at(file_data, static_cast<ssize_t>(offset)), message_record_header_size};
  MessageRecordHeader record_header{};
  std::memcpy(&record_header, record_header_span.data(), message_record_header_size);
  const auto record_size = message_record_header_size + msg.header.size() + msg.data.size() + record_trailer_size;
  if (offset + record_size > file_data.size())
  {
    jewels::log_cerr_error(
      "File data too small for message record need: {}, size: {}", offset + record_size, file_data.size());
    return jewels::unexpected(jewels::MonoError{});
  }
  if (!try_validate_record_header(record_header.header, record_size, RecordType::message))
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  if (record_header.channel_id != channel_id_map.at(msg.channel_name))
  {
    jewels::log_cerr_error("Invalid channel_id: {}", record_header.channel_id);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (record_header.sequence_number != msg.sequence_number)
  {
    jewels::log_cerr_error("Invalid sequence_number: {}", record_header.sequence_number);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (record_header.log_time_ns != msg.log_time.get_nanoseconds())
  {
    jewels::log_cerr_error("Invalid log_time_ns: {}", record_header.log_time_ns);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (record_header.message_time_ns != msg.message_time.get_nanoseconds())
  {
    jewels::log_cerr_error("Invalid message_time_ns: {}", record_header.message_time_ns);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (record_header.header_length != msg.header.size())
  {
    jewels::log_cerr_error("Invalid header length: {}", record_header.header_length);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (!std::ranges::all_of(record_header.reserved, [](const auto val) { return val == std::byte{0}; }))
  {
    jewels::log_cerr_error("Non zero header reserved field");
    dump_data_span("header.reserved", record_header.reserved);
    return jewels::unexpected(jewels::MonoError{});
  }
  if (record_header.flags.is_lite_compressed != (is_lite_compressed ? 1U : 0U))
  {
    jewels::log_cerr_error(
      "Invalid lite_compressed flag: {}", static_cast<uint16_t>(record_header.flags.is_lite_compressed));
    return jewels::unexpected(jewels::MonoError{});
  }
  if (record_header.flags.is_persistent != (is_persistent ? 1U : 0U))
  {
    jewels::log_cerr_error("Invalid is_persistent flag: {}", static_cast<uint16_t>(record_header.flags.is_persistent));
    return jewels::unexpected(jewels::MonoError{});
  }
  if (record_header.flags.is_repeated != (is_repeated ? 1U : 0U))
  {
    jewels::log_cerr_error("Invalid is_repeated flag: {}", static_cast<uint16_t>(record_header.flags.is_repeated));
    return jewels::unexpected(jewels::MonoError{});
  }
  const auto header_offset = offset + message_record_header_size;
  const std::span header_span{&jewels::at(file_data, static_cast<ssize_t>(header_offset)), msg.header.size()};
  if (
    (header_span.size() != msg.header.size()) ||
    (!msg.header.empty() && (std::memcmp(header_span.data(), msg.header.data(), msg.header.size()) != 0)))
  {
    jewels::log_cerr_error("Invalid message header");
    dump_data_span("header", std::as_bytes(header_span));
    return jewels::unexpected(jewels::MonoError{});
  }
  const auto data_offset = header_offset + msg.header.size();
  const std::span data_span{&jewels::at(file_data, static_cast<ssize_t>(data_offset)), msg.data.size()};
  if (std::memcmp(data_span.data(), msg.data.data(), msg.data.size()) != 0)
  {
    jewels::log_cerr_error("Invalid message data");
    return jewels::unexpected(jewels::MonoError{});
  }
  const std::array checksum_spans = {
    std::as_bytes(record_header_span), std::as_bytes(header_span), std::as_bytes(data_span)};
  const auto trailer_offset = data_offset + msg.data.size();
  const std::span trailer_span{&jewels::at(file_data, static_cast<ssize_t>(trailer_offset)), record_trailer_size};
  if (!try_validate_record_trailer(checksum_spans, std::as_bytes(trailer_span)))
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  return offset + record_size;
}

[[nodiscard]] jewels::expected<size_t, jewels::MonoError> try_validate_message_record(
  std::span<const char> file_data,
  size_t offset,
  const ZeroCopyMessage& msg,
  const std::unordered_map<std::string_view, uint16_t>& channel_id_map,
  bool is_lite_compressed,
  bool is_persistent,
  bool is_repeated)
{
  const auto data_size = std::accumulate(
    msg.data.begin(), msg.data.end(), size_t{0U}, [](size_t lhs, auto& rhs) { return lhs + rhs.size(); });
  std::vector<std::byte> data(data_size);
  size_t data_offset = 0U;
  for (const auto span : msg.data)
  {
    if (!span.empty())
    {
      std::memcpy(&data.at(data_offset), span.data(), span.size());
      data_offset += span.size();
    }
  }
  return try_validate_message_record(
    file_data,
    offset,
    Message{
      .channel_name = msg.channel_name,
      .sequence_number = msg.sequence_number,
      .log_time = msg.log_time,
      .message_time = msg.message_time,
      .header = msg.header,
      .data = data,
    },
    channel_id_map,
    is_lite_compressed,
    is_persistent,
    is_repeated);
}

} // namespace clockwork_logging::onboard::tests
