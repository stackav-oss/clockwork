// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/file_chunk_reader.hh"
#include "clockwork/logging/offboard/file_chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/message_chunk_writer.hh"
#include "clockwork/logging/offboard/tests/support/test_support.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/wrapping_counter.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/filesystem_wrapper.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{
namespace
{

/// Message logged in a message chunk
struct TestLoggedMessage
{
  /// Message header
  std::vector<std::byte> header;

  /// Message data
  std::vector<std::byte> data;

  /// Sequence number
  uint32_t sequence_number{};

  /// Log timestamp
  LogTimestamp log_time{};

  /// Transmit timestamp
  LogTimestamp transmit_time{};

  /// Is repeated persistent flag
  bool is_repeated_persistent{};

  /// Is lite compressed flag
  bool is_lite_compressed{};

  /// Comparison operator
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return True iff lhs.transmit_time < rhs.transmit_time
  [[nodiscard]] friend bool operator<(const TestLoggedMessage& lhs, const TestLoggedMessage& rhs) noexcept
  {
    return (lhs.transmit_time < rhs.transmit_time) ||
           (lhs.transmit_time == rhs.transmit_time && lhs.sequence_number < rhs.sequence_number);
  }
};

/// Validate the contents of a message in a message chunk
/// @param[in] expected_message Expected message contents
/// @param[in] chunk Message chunk
/// @param[in] chunk_offset Message chunk offset
/// @return True iff the message matches the expected contents
[[nodiscard]] bool
validate_message(const TestLoggedMessage& expected_message, std::span<const std::byte> chunk, uint32_t chunk_offset)
{
  const auto message_header_result = nolint_helper::byte_span_to_value_ptr<MessageChunkMessageHeader>(
    std::span{&chunk[chunk_offset], message_chunk_message_header_size});
  if (!message_header_result)
  {
    return false;
  }
  const auto* message_header_ptr = message_header_result.value();
  if (message_header_ptr->sequence_number != expected_message.sequence_number)
  {
    jewels::log_cerr_error(
      "invalid sequence number: actual {}, expected {}",
      message_header_ptr->sequence_number,
      expected_message.sequence_number);
    return false;
  }
  if (message_header_ptr->log_time_ns != expected_message.log_time.get_nanoseconds())
  {
    jewels::log_cerr_error(
      "invalid log time: actual {}, expected {}",
      message_header_ptr->log_time_ns,
      expected_message.log_time.get_nanoseconds());
    return false;
  }
  if (message_header_ptr->transmit_time_ns != expected_message.transmit_time.get_nanoseconds())
  {
    jewels::log_cerr_error(
      "invalid transmit time: actual {}, expected {}",
      message_header_ptr->transmit_time_ns,
      expected_message.transmit_time.get_nanoseconds());
    return false;
  }
  if (
    message_header_ptr->flags.is_repeated_persistent !=
    static_cast<uint8_t>(expected_message.is_repeated_persistent ? 1U : 0U))
  {
    jewels::log_cerr_error(
      "invalid is_repeated_persistent flag: actual {}, expected {}",
      message_header_ptr->flags.is_repeated_persistent,
      expected_message.is_repeated_persistent);
    return false;
  }
  if (
    message_header_ptr->flags.is_lite_compressed != static_cast<uint8_t>(expected_message.is_lite_compressed ? 1U : 0U))
  {
    jewels::log_cerr_error(
      "invalid is_lite_compressed flag: actual {}, expected {}",
      message_header_ptr->flags.is_lite_compressed,
      expected_message.is_lite_compressed);
    return false;
  }
  if (message_header_ptr->flags.reserved != static_cast<uint8_t>(0U))
  {
    jewels::log_cerr_error("non-zero reserved flags: {}", message_header_ptr->flags.reserved);
    return false;
  }
  if (!std::ranges::all_of(message_header_ptr->reserved, [](const auto val) { return val == std::byte{0}; }))
  {
    jewels::log_cerr_error("Non zero header reserved field");
    onboard::tests::dump_data_span("message_header_ptr->reserved", message_header_ptr->reserved);
    return false;
  }
  if (message_header_ptr->header_size != expected_message.header.size())
  {
    jewels::log_cerr_error(
      "invalid header size: actual {}, expected {}", message_header_ptr->header_size, expected_message.header.size());
    return false;
  }
  if (
    std::memcmp(
      &chunk[chunk_offset + message_chunk_message_header_size],
      expected_message.header.data(),
      expected_message.header.size()) != 0)
  {
    jewels::log_cerr_error("message header mismatch");
    return false;
  }
  if (message_header_ptr->data_size != expected_message.data.size())
  {
    jewels::log_cerr_error(
      "invalid data size: actual {}, expected {}", message_header_ptr->data_size, expected_message.data.size());
    return false;
  }
  if (
    std::memcmp(
      &chunk[chunk_offset + message_chunk_message_header_size + expected_message.header.size()],
      expected_message.data.data(),
      expected_message.data.size()) != 0)
  {
    jewels::log_cerr_error("message data mismatch");
    return false;
  }
  return true;
}

/// Validate the contents of a message chunk with v1 index format
/// @param[in] chunk_reader Chunk reader
/// @param[in] chunk_compressor Chunk compressor
/// @param[in] compression_type Compression type
/// @param[in] index_entry Index entry referencing the chunk
/// @param[in] expected_messages Vector of messages expected in the chunk
/// @return True iff the contents of the chunk match the expected contents
[[nodiscard]] bool validate_message_chunk_v1(
  ChunkReader& chunk_reader,
  const ChunkCompressor& chunk_compressor,
  CompressionType compression_type,
  const IndexChunkIndexEntry& index_entry,
  std::vector<TestLoggedMessage> expected_messages)
{
  auto read_result = chunk_reader.read_chunk(index_entry.location.chunk_offset, index_entry.location.chunk_size);
  if (!read_result)
  {
    jewels::log_cerr_error("read_chunk failed: {}", read_result.error());
    return false;
  }
  auto decompress_result = chunk_compressor.decompress_chunk(std::move(read_result).value(), compression_type);
  if (!decompress_result)
  {
    jewels::log_cerr_error("decompress_chunk failed: {}", decompress_result.error());
    return false;
  }
  const auto& chunk = decompress_result.value();
  const auto trailer_offset = chunk.size() - message_chunk_trailer_size;
  const auto trailer_result = nolint_helper::byte_span_to_value_ptr<MessageChunkTrailer>(
    std::span{&chunk.at(trailer_offset), message_chunk_trailer_size});
  if (!trailer_result)
  {
    return false;
  }
  const auto* trailer_ptr = trailer_result.value();
  const auto index_size = (trailer_offset - trailer_ptr->index_offset) / message_chunk_index_entry_v1_size;
  if (index_size != expected_messages.size())
  {
    jewels::log_cerr_error("Index size mismatch, actual: {} expected: {}", index_size, expected_messages.size());
    return false;
  }
  const auto chunk_index = nolint_helper::byte_span_to_value_span<MessageChunkIndexEntryV1>(
    std::span{&chunk.at(trailer_ptr->index_offset), index_size * message_chunk_index_entry_v1_size});
  std::sort(expected_messages.begin(), expected_messages.end());
  for (size_t index = 0U; index < index_size; ++index)
  {
    CAPTURE(index);
    if (chunk_index[index].transmit_time_ns != expected_messages.at(index).transmit_time.get_nanoseconds())
    {
      jewels::log_cerr_error(
        "invalid index transmit time: actual {}, expected {}",
        chunk_index[index].transmit_time_ns,
        expected_messages.at(index).transmit_time.get_nanoseconds());
      return false;
    }
    if (!validate_message(expected_messages.at(index), chunk, chunk_index[index].chunk_offset))
    {
      return false;
    }
  }
  return true;
}

/// Validate the contents of a message chunk with v2 index format
/// @param[in] chunk_reader Chunk reader
/// @param[in] chunk_compressor Chunk compressor
/// @param[in] compression_type Compression type
/// @param[in] index_entry Index entry referencing the chunk
/// @param[in] expected_messages Vector of messages expected in the chunk
/// @return True iff the contents of the chunk match the expected contents
[[nodiscard]] bool validate_message_chunk_v2(
  ChunkReader& chunk_reader,
  const ChunkCompressor& chunk_compressor,
  CompressionType compression_type,
  const IndexChunkIndexEntry& index_entry,
  std::vector<TestLoggedMessage> expected_messages)
{
  auto read_result = chunk_reader.read_chunk(index_entry.location.chunk_offset, index_entry.location.chunk_size);
  if (!read_result)
  {
    jewels::log_cerr_error("read_chunk failed: {}", read_result.error());
    return false;
  }
  auto decompress_result = chunk_compressor.decompress_chunk(std::move(read_result).value(), compression_type);
  if (!decompress_result)
  {
    jewels::log_cerr_error("decompress_chunk failed: {}", decompress_result.error());
    return false;
  }
  const auto& chunk = decompress_result.value();
  const auto trailer_offset = chunk.size() - message_chunk_trailer_size;
  const auto trailer_result = nolint_helper::byte_span_to_value_ptr<MessageChunkTrailer>(
    std::span{&chunk.at(trailer_offset), message_chunk_trailer_size});
  if (!trailer_result)
  {
    return false;
  }
  const auto* trailer_ptr = trailer_result.value();
  const auto index_size = (trailer_offset - trailer_ptr->index_offset) / message_chunk_index_entry_v2_size;
  if (index_size != expected_messages.size())
  {
    jewels::log_cerr_error("Index size mismatch, actual: {} expected: {}", index_size, expected_messages.size());
    return false;
  }
  const auto chunk_index = nolint_helper::byte_span_to_value_span<MessageChunkIndexEntryV2>(
    std::span{&chunk.at(trailer_ptr->index_offset), index_size * message_chunk_index_entry_v2_size});
  std::sort(expected_messages.begin(), expected_messages.end());
  for (size_t index = 0U; index < index_size; ++index)
  {
    CAPTURE(index);
    if (chunk_index[index].transmit_time_ns != expected_messages.at(index).transmit_time.get_nanoseconds())
    {
      jewels::log_cerr_error(
        "invalid index transmit time: actual {}, expected {}",
        chunk_index[index].transmit_time_ns,
        expected_messages.at(index).transmit_time.get_nanoseconds());
      return false;
    }
    if (chunk_index[index].sequence_number.value() != expected_messages.at(index).sequence_number)
    {
      jewels::log_cerr_error(
        "invalid index sequence number: actual {}, expected {}",
        chunk_index[index].sequence_number.value(),
        expected_messages.at(index).sequence_number);
      return false;
    }
    if (!validate_message(expected_messages.at(index), chunk, chunk_index[index].chunk_offset))
    {
      return false;
    }
  }
  return true;
}

/// Validate the contents of a message chunk
/// @param[in] chunk_reader Chunk reader
/// @param[in] chunk_compressor Chunk compressor
/// @param[in] index_format Message index format
/// @param[in] compression_type Compression type
/// @param[in] index_entry Index entry referencing the chunk
/// @param[in] expected_messages Vector of messages expected in the chunk
/// @return True iff the contents of the chunk match the expected contents
[[nodiscard]] bool validate_message_chunk(
  ChunkReader& chunk_reader,
  const ChunkCompressor& chunk_compressor,
  MessageChunkIndexFormat index_format,
  CompressionType compression_type,
  const IndexChunkIndexEntry& index_entry,
  std::vector<TestLoggedMessage> expected_messages)
{
  switch (index_format)
  {
  case MessageChunkIndexFormat::v1:
    return validate_message_chunk_v1(
      chunk_reader, chunk_compressor, compression_type, index_entry, std::move(expected_messages));
  case MessageChunkIndexFormat::v2:
    return validate_message_chunk_v2(
      chunk_reader, chunk_compressor, compression_type, index_entry, std::move(expected_messages));
  }
}

TEST_CASE("MessageChunkWriter")
{
  constexpr auto test_file_name = "test_file.slog";

  const auto message_chunk_index_format = GENERATE(MessageChunkIndexFormat::v1, MessageChunkIndexFormat::v2);
  CAPTURE(message_chunk_index_format);

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_file_path = test_dir.get_path() / test_file_name;
  const ChunkCompressor compressor{memory_resource};
  LiteCompressor lite_compressor{memory_resource};
  const auto writer_result = FileChunkWriter<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(writer_result);
  auto& file_writer = *writer_result.value();
  const auto reader_result = FileChunkReader<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(reader_result);
  auto& file_reader = *reader_result.value();
  MessageChunkWriter message_writer{memory_resource, message_chunk_index_format, CompressionType::zstd};

  SECTION("Empty chunk")
  {
    REQUIRE(message_writer.is_empty());
    REQUIRE_FALSE(message_writer.is_full());
    REQUIRE(file_writer.open());
    const auto write_result = message_writer.write_chunk(compressor, file_writer);
    REQUIRE(write_result);
    REQUIRE(file_writer.close());
    REQUIRE(file_reader.open());
    REQUIRE(validate_message_chunk(
      file_reader, compressor, message_chunk_index_format, CompressionType::zstd, write_result.value(), {}));
  }

  SECTION("Full chunk written in reverse order by timestamp")
  {
    constexpr auto header_size = 125U;
    constexpr auto data_size = 1234U;
    constexpr auto message_count = 7517;
    std::vector<TestLoggedMessage> logged_messages;
    logged_messages.reserve(message_count);
    LogTimestamp log_time{std::chrono::hours(1)};
    LogTimestamp transmit_time{std::chrono::hours(1)};
    for (uint32_t sequence_number = 0U; sequence_number < message_count; ++sequence_number)
    {
      REQUIRE_FALSE(message_writer.is_full());
      std::vector<std::byte> header(header_size);
      onboard::tests::fill_with_random_bytes(header);
      std::vector<std::byte> data(data_size);
      onboard::tests::fill_with_random_bytes(data);
      const std::span<const std::byte> data_span{data};
      std::span data_spans{&data_span, 1U};
      const bool is_lite_compressed = sequence_number % 2U == 0U;
      if (is_lite_compressed)
      {
        data_spans = offboard::tests::zero_copy_lite_compress(data_span, lite_compressor);
      }
      REQUIRE(message_writer.add_message(
        offboard::tests::spans_size(data_spans),
        ZeroCopyLoggedMessage{
          .sequence_number = sequence_number,
          .log_time = log_time,
          .transmit_time = transmit_time,
          .header = header,
          .data = data_spans,
          .is_repeated_persistent = sequence_number == 0U,
          .is_lite_compressed = is_lite_compressed,
        }));
      if (is_lite_compressed)
      {
        data = offboard::tests::lite_compress(data_span, lite_compressor);
      }
      logged_messages.emplace_back();
      auto& logged_message = logged_messages.back();
      logged_message.header = std::move(header);
      logged_message.data = std::move(data);
      logged_message.sequence_number = sequence_number;
      logged_message.log_time = log_time;
      logged_message.transmit_time = transmit_time;
      logged_message.is_repeated_persistent = sequence_number == 0U;
      logged_message.is_lite_compressed = is_lite_compressed;
      log_time += std::chrono::minutes(1);
      transmit_time += std::chrono::minutes(-1);
      REQUIRE_FALSE(message_writer.is_empty());
    }
    REQUIRE(message_writer.is_full());
    REQUIRE(file_writer.open());
    const auto write_result = message_writer.write_chunk(compressor, file_writer);
    REQUIRE(write_result);
    REQUIRE(file_writer.close());
    REQUIRE(file_reader.open());
    REQUIRE(validate_message_chunk(
      file_reader,
      compressor,
      message_chunk_index_format,
      CompressionType::zstd,
      write_result.value(),
      logged_messages));
  }

  SECTION("Full chunk written in reverse order by sequence number")
  {
    constexpr auto header_size = 125U;
    constexpr auto data_size = 1234U;
    constexpr auto message_count = 7517;
    std::vector<TestLoggedMessage> logged_messages;
    logged_messages.reserve(message_count);
    LogTimestamp log_time{std::chrono::hours(1)};
    const LogTimestamp transmit_time{std::chrono::hours(1)};
    for (uint32_t sequence_number = 0U; sequence_number < message_count; ++sequence_number)
    {
      REQUIRE_FALSE(message_writer.is_full());
      std::vector<std::byte> header(header_size);
      onboard::tests::fill_with_random_bytes(header);
      std::vector<std::byte> data(data_size);
      onboard::tests::fill_with_random_bytes(data);
      const std::span<const std::byte> data_span{data};
      std::span data_spans{&data_span, 1U};
      const bool is_lite_compressed = sequence_number % 2U == 0U;
      if (is_lite_compressed)
      {
        data_spans = offboard::tests::zero_copy_lite_compress(data_span, lite_compressor);
      }
      REQUIRE(message_writer.add_message(
        offboard::tests::spans_size(data_spans),
        ZeroCopyLoggedMessage{
          .sequence_number = message_count - sequence_number,
          .log_time = log_time,
          .transmit_time = transmit_time,
          .header = header,
          .data = data_spans,
          .is_repeated_persistent = sequence_number == 0U,
          .is_lite_compressed = is_lite_compressed,
        }));
      if (is_lite_compressed)
      {
        data = offboard::tests::lite_compress(data_span, lite_compressor);
      }
      logged_messages.emplace_back();
      auto& logged_message = logged_messages.back();
      logged_message.header = std::move(header);
      logged_message.data = std::move(data);
      logged_message.sequence_number = message_count - sequence_number;
      logged_message.log_time = log_time;
      logged_message.transmit_time = transmit_time;
      logged_message.is_repeated_persistent = sequence_number == 0U;
      logged_message.is_lite_compressed = is_lite_compressed, log_time += std::chrono::minutes(1);
      REQUIRE_FALSE(message_writer.is_empty());
    }
    REQUIRE(message_writer.is_full());
    REQUIRE(file_writer.open());
    const auto write_result = message_writer.write_chunk(compressor, file_writer);
    REQUIRE(write_result);
    REQUIRE(file_writer.close());
    REQUIRE(file_reader.open());
    REQUIRE(validate_message_chunk(
      file_reader,
      compressor,
      message_chunk_index_format,
      CompressionType::zstd,
      write_result.value(),
      logged_messages));
  }

  SECTION("Writer not open")
  {
    const std::byte data{'1'};
    const std::span data_span{&data, 1U};
    REQUIRE(message_writer.add_message(
      1U,
      ZeroCopyLoggedMessage{
        .sequence_number = 0U,
        .log_time = LogTimestamp{},
        .transmit_time = LogTimestamp{},
        .header = {&data, 1U},
        .data = {&data_span, 1U},
        .is_repeated_persistent = false,
        .is_lite_compressed = false,
      }));
    REQUIRE(message_writer.write_chunk(compressor, file_writer) == jewels::unexpected(LogError::not_open));
  }

  SECTION("Write fails")
  {
    REQUIRE(file_writer.open());
    file_writer.filesystem().inject_write_error(ENOSPC);
    REQUIRE(message_writer.write_chunk(compressor, file_writer) == jewels::unexpected(LogError::no_space_on_device));
  }

  SECTION("Message too big")
  {
    const std::byte data{'1'};
    const std::span data_span{&data, 1U};
    REQUIRE(
      message_writer.add_message(
        max_message_data_size + 1U,
        ZeroCopyLoggedMessage{
          .sequence_number = 0U,
          .log_time = LogTimestamp{},
          .transmit_time = LogTimestamp{},
          .header = {&data, 1U},
          .data = {&data_span, 1U},
          .is_repeated_persistent = false,
          .is_lite_compressed = false,
        }) == jewels::unexpected(LogError::message_data_exceeds_max_size));
    REQUIRE(
      message_writer.add_message(
        1U,
        ZeroCopyLoggedMessage{
          .sequence_number = 0U,
          .log_time = LogTimestamp{},
          .transmit_time = LogTimestamp{},
          .header = {&data, max_message_header_size + 1U},
          .data = {&data_span, 1U},
          .is_repeated_persistent = false,
          .is_lite_compressed = false,
        }) == jewels::unexpected(LogError::message_header_exceeds_max_size));
  }
}

} // namespace
} // namespace clockwork_logging::offboard
