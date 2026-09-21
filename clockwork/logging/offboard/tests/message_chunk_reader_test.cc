// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/file_chunk_reader.hh"
#include "clockwork/logging/offboard/file_chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/message_chunk_reader.hh"
#include "clockwork/logging/offboard/message_chunk_writer.hh"
#include "clockwork/logging/offboard/tests/support/test_support.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/filesystem_wrapper.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("MessageChunkReader")
{
  constexpr auto test_file_name = "test_file.slog";
  constexpr auto channel_name = "test_channel";

  const auto message_chunk_index_format = GENERATE(MessageChunkIndexFormat::v1, MessageChunkIndexFormat::v2);
  CAPTURE(message_chunk_index_format);

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_file_path = test_dir.get_path() / test_file_name;
  const ChunkCompressor compressor{memory_resource};
  LiteCompressor lite_compressor{memory_resource};
  const auto writer_result = FileChunkWriter<>::make_shared(test_file_path.string(), memory_resource);
  REQUIRE(writer_result);
  auto& file_writer = *writer_result.value();
  const auto reader_result = FileChunkReader<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(reader_result);
  auto& file_reader = *reader_result.value();
  MessageChunkWriter message_writer{memory_resource, message_chunk_index_format, CompressionType::zstd};

  SECTION("Empty chunk")
  {
    REQUIRE(file_writer.open());
    const auto write_result = message_writer.write_chunk(compressor, file_writer);
    REQUIRE(write_result);
    REQUIRE(file_writer.close());
    REQUIRE(file_reader.open());
    MessageChunkReader message_reader{memory_resource, channel_name, ChannelType::regular, {}, CompressionType::zstd};
    REQUIRE(message_reader.read_chunk(write_result.value().location, compressor, file_reader));
    REQUIRE(message_reader.is_empty());
    REQUIRE(message_reader.get_next_message_identifier() == jewels::unexpected(LogError::end_of_chunk));
    REQUIRE(message_reader.read_next() == jewels::unexpected(LogError::end_of_chunk));
  }

  SECTION("Not empty chunk")
  {
    constexpr auto header_size = 125U;
    constexpr auto data_size = 1234U;
    constexpr LogTimestamp transmit_time0{std::chrono::seconds(0)};
    constexpr LogTimestamp log_time1{std::chrono::seconds(1)};
    constexpr LogTimestamp transmit_time1{std::chrono::seconds(10)};
    constexpr LogTimestamp log_time2{std::chrono::seconds(2)};
    constexpr LogTimestamp transmit_time2{std::chrono::seconds(20)};
    constexpr LogTimestamp log_time3{std::chrono::seconds(3)};
    constexpr LogTimestamp transmit_time3{std::chrono::seconds(30)};
    constexpr LogTimestamp transmit_time4{std::chrono::seconds(40)};

    std::vector<std::byte> message1_data(data_size);
    onboard::tests::fill_with_random_bytes(message1_data);
    tests::TestLoggedMessage message1{
      .channel_name = channel_name,
      .sequence_number = 1U,
      .log_time = log_time1,
      .logged_transmit_time = transmit_time1,
      .expected_transmit_time = transmit_time1,
      .header = std::vector<std::byte>(header_size),
      .data = offboard::tests::zero_copy_lite_compress(message1_data, lite_compressor),
      .is_repeated_persistent = false,
      .is_lite_compressed = true,
    };
    onboard::tests::fill_with_random_bytes(message1.header);

    std::vector<std::byte> message2a_data(data_size);
    onboard::tests::fill_with_random_bytes(message2a_data);
    std::span<const std::byte> message2a_span{message2a_data};
    tests::TestLoggedMessage message2a{
      .channel_name = channel_name,
      .sequence_number = 2U,
      .log_time = log_time2,
      .logged_transmit_time = transmit_time2,
      .expected_transmit_time = transmit_time2,
      .header = std::vector<std::byte>(header_size),
      .data = std::span{&message2a_span, 1U},
      .is_repeated_persistent = true,
      .is_lite_compressed = false,
    };
    onboard::tests::fill_with_random_bytes(message2a.header);

    std::vector<std::byte> message2b_data(data_size);
    onboard::tests::fill_with_random_bytes(message2b_data);
    tests::TestLoggedMessage message2b{
      .channel_name = channel_name,
      .sequence_number = 3U,
      .log_time = log_time2,
      .logged_transmit_time = transmit_time2,
      .expected_transmit_time = transmit_time2,
      .header = std::vector<std::byte>(header_size),
      .data = offboard::tests::zero_copy_lite_compress(message2b_data, lite_compressor),
      .is_repeated_persistent = true,
      .is_lite_compressed = true,
    };
    onboard::tests::fill_with_random_bytes(message2b.header);

    std::vector<std::byte> message3_data(data_size);
    onboard::tests::fill_with_random_bytes(message3_data);
    std::span<const std::byte> message3_span{message3_data};
    tests::TestLoggedMessage message3{
      .channel_name = channel_name,
      .sequence_number = 3U,
      .log_time = log_time3,
      .logged_transmit_time = transmit_time3,
      .expected_transmit_time = transmit_time3,
      .header = std::vector<std::byte>(header_size),
      .data = std::span{&message3_span, 1U},
      .is_repeated_persistent = false,
      .is_lite_compressed = false,
    };
    onboard::tests::fill_with_random_bytes(message3.header);

    REQUIRE(message_writer.add_message(
      offboard::tests::spans_size(message3.data),
      ZeroCopyLoggedMessage{
        .sequence_number = message3.sequence_number,
        .log_time = message3.log_time,
        .transmit_time = message3.logged_transmit_time,
        .header = message3.header,
        .data = message3.data,
        .is_repeated_persistent = message3.is_repeated_persistent,
        .is_lite_compressed = message3.is_lite_compressed,
      }));
    REQUIRE(message_writer.add_message(
      offboard::tests::spans_size(message2b.data),
      ZeroCopyLoggedMessage{
        .sequence_number = message2b.sequence_number,
        .log_time = message2b.log_time,
        .transmit_time = message2b.logged_transmit_time,
        .header = message2b.header,
        .data = message2b.data,
        .is_repeated_persistent = message2b.is_repeated_persistent,
        .is_lite_compressed = message2b.is_lite_compressed,
      }));
    REQUIRE(message_writer.add_message(
      offboard::tests::spans_size(message2a.data),
      ZeroCopyLoggedMessage{
        .sequence_number = message2a.sequence_number,
        .log_time = message2a.log_time,
        .transmit_time = message2a.logged_transmit_time,
        .header = message2a.header,
        .data = message2a.data,
        .is_repeated_persistent = message2a.is_repeated_persistent,
        .is_lite_compressed = message2a.is_lite_compressed,
      }));
    REQUIRE(message_writer.add_message(
      offboard::tests::spans_size(message1.data),
      ZeroCopyLoggedMessage{
        .sequence_number = message1.sequence_number,
        .log_time = message1.log_time,
        .transmit_time = message1.logged_transmit_time,
        .header = message1.header,
        .data = message1.data,
        .is_repeated_persistent = message1.is_repeated_persistent,
        .is_lite_compressed = message1.is_lite_compressed,
      }));

    REQUIRE(file_writer.open());
    const auto write_result = message_writer.write_chunk(compressor, file_writer);
    REQUIRE(write_result);
    REQUIRE(file_writer.close());

    SECTION("regular channels")
    {
      SECTION("No time range")
      {
        REQUIRE(file_reader.open());
        MessageChunkReader message_reader{
          memory_resource, channel_name, ChannelType::regular, {}, CompressionType::zstd};
        REQUIRE(message_reader.read_chunk(write_result.value().location, compressor, file_reader));
        REQUIRE(tests::check_read_result({message1, message2a, message2b, message3}, message_reader));
      }

      SECTION("Time range before chunk")
      {
        REQUIRE(file_reader.open());
        MessageChunkReader message_reader{
          memory_resource, channel_name, ChannelType::regular, LogInterval{transmit_time0}, CompressionType::zstd};
        REQUIRE(message_reader.read_chunk(write_result.value().location, compressor, file_reader));
        REQUIRE(tests::check_read_result({}, message_reader));
      }

      SECTION("Time range after chunk")
      {
        REQUIRE(file_reader.open());
        MessageChunkReader message_reader{
          memory_resource, channel_name, ChannelType::regular, LogInterval{transmit_time4}, CompressionType::zstd};
        REQUIRE(message_reader.read_chunk(write_result.value().location, compressor, file_reader));
        REQUIRE(tests::check_read_result({}, message_reader));
      }

      SECTION("Time range covers entire chunk")
      {
        REQUIRE(file_reader.open());
        MessageChunkReader message_reader{
          memory_resource,
          channel_name,
          ChannelType::regular,
          LogInterval{transmit_time0, transmit_time4},
          CompressionType::zstd};
        REQUIRE(message_reader.read_chunk(write_result.value().location, compressor, file_reader));
        REQUIRE(tests::check_read_result({message1, message2a, message2b, message3}, message_reader));
      }

      SECTION("Time range covers first two messages")
      {
        REQUIRE(file_reader.open());
        MessageChunkReader message_reader{
          memory_resource,
          channel_name,
          ChannelType::regular,
          LogInterval{transmit_time0, transmit_time2},
          CompressionType::zstd};
        REQUIRE(message_reader.read_chunk(write_result.value().location, compressor, file_reader));
        REQUIRE(tests::check_read_result({message1, message2a, message2b}, message_reader));
      }

      SECTION("Time range covers last two messages")
      {
        REQUIRE(file_reader.open());
        MessageChunkReader message_reader{
          memory_resource,
          channel_name,
          ChannelType::regular,
          LogInterval{transmit_time2, transmit_time4},
          CompressionType::zstd};
        REQUIRE(message_reader.read_chunk(write_result.value().location, compressor, file_reader));
        REQUIRE(tests::check_read_result({message2a, message2b, message3}, message_reader));
      }

      SECTION("Reader not open")
      {
        MessageChunkReader message_reader{
          memory_resource, channel_name, ChannelType::regular, {}, CompressionType::zstd};
        REQUIRE(
          message_reader.read_chunk(write_result.value().location, compressor, file_reader) ==
          jewels::unexpected(LogError::not_open));
      }

      SECTION("Read fails")
      {
        REQUIRE(file_reader.open());
        file_reader.filesystem().inject_read_error(EIO);
        MessageChunkReader message_reader{
          memory_resource, channel_name, ChannelType::regular, {}, CompressionType::zstd};
        REQUIRE(
          message_reader.read_chunk(write_result.value().location, compressor, file_reader) ==
          jewels::unexpected(LogError::io_error));
      }

      SECTION("Decompression fails")
      {
        MessageChunkReader message_reader{
          memory_resource, channel_name, ChannelType::regular, {}, CompressionType::zstd};
        REQUIRE(onboard::tests::corrupt_log_file(test_file_path.string(), 32U, "XXX"));
        REQUIRE(file_reader.open());
        REQUIRE(
          message_reader.read_chunk(write_result.value().location, compressor, file_reader) ==
          jewels::unexpected(LogError::decompression_failure));
      }
    }

    SECTION("persistent channels")
    {
      SECTION("No time range")
      {
        REQUIRE(file_reader.open());
        MessageChunkReader message_reader{
          memory_resource, channel_name, ChannelType::persistent, {}, CompressionType::zstd};
        REQUIRE(message_reader.read_chunk(write_result.value().location, compressor, file_reader));
        REQUIRE(tests::check_read_result({message1, message2a, message2b, message3}, message_reader));
      }

      SECTION("Time range before chunk")
      {
        REQUIRE(file_reader.open());
        MessageChunkReader message_reader{
          memory_resource, channel_name, ChannelType::persistent, LogInterval{transmit_time0}, CompressionType::zstd};
        REQUIRE(message_reader.read_chunk(write_result.value().location, compressor, file_reader));
        REQUIRE(tests::check_read_result({}, message_reader));
      }

      SECTION("Time range after chunk")
      {
        REQUIRE(file_reader.open());
        MessageChunkReader message_reader{
          memory_resource, channel_name, ChannelType::persistent, LogInterval{transmit_time4}, CompressionType::zstd};
        REQUIRE(message_reader.read_chunk(write_result.value().location, compressor, file_reader));
        auto message3_prime = message3;
        message3_prime.expected_transmit_time = transmit_time4;
        message3_prime.is_repeated_persistent = true;
        message3_prime.is_lite_compressed = false;
        REQUIRE(tests::check_read_result({message3_prime}, message_reader));
      }

      SECTION("Time range exactly covers entire chunk")
      {
        REQUIRE(file_reader.open());
        MessageChunkReader message_reader{
          memory_resource,
          channel_name,
          ChannelType::persistent,
          LogInterval{transmit_time0, transmit_time4},
          CompressionType::zstd};
        REQUIRE(message_reader.read_chunk(write_result.value().location, compressor, file_reader));
        REQUIRE(tests::check_read_result({message1, message2a, message2b, message3}, message_reader));
      }

      SECTION("Time range exactly covers first two messages")
      {
        REQUIRE(file_reader.open());
        MessageChunkReader message_reader{
          memory_resource,
          channel_name,
          ChannelType::persistent,
          LogInterval{transmit_time1, transmit_time2},
          CompressionType::zstd};
        REQUIRE(message_reader.read_chunk(write_result.value().location, compressor, file_reader));
        REQUIRE(tests::check_read_result({message1, message2a, message2b}, message_reader));
      }

      SECTION("Time range covers first two messages")
      {
        REQUIRE(file_reader.open());
        MessageChunkReader message_reader{
          memory_resource,
          channel_name,
          ChannelType::persistent,
          LogInterval{transmit_time1 + std::chrono::nanoseconds(1), transmit_time2},
          CompressionType::zstd};
        REQUIRE(message_reader.read_chunk(write_result.value().location, compressor, file_reader));
        auto message1_prime = message1;
        message1_prime.expected_transmit_time = transmit_time1 + std::chrono::nanoseconds(1);
        message1_prime.is_repeated_persistent = true;
        REQUIRE(tests::check_read_result({message1_prime, message2a, message2b}, message_reader));
      }

      SECTION("Time range exactly covers last two messages")
      {
        REQUIRE(file_reader.open());
        MessageChunkReader message_reader{
          memory_resource,
          channel_name,
          ChannelType::persistent,
          LogInterval{transmit_time2, transmit_time3},
          CompressionType::zstd};
        REQUIRE(message_reader.read_chunk(write_result.value().location, compressor, file_reader));
        REQUIRE(tests::check_read_result({message2a, message2b, message3}, message_reader));
      }

      SECTION("Time range covers last two messages")
      {
        REQUIRE(file_reader.open());
        MessageChunkReader message_reader{
          memory_resource,
          channel_name,
          ChannelType::persistent,
          LogInterval{transmit_time2 + std::chrono::nanoseconds(1), transmit_time3},
          CompressionType::zstd};
        REQUIRE(message_reader.read_chunk(write_result.value().location, compressor, file_reader));
        auto message2b_prime = message2b;
        message2b_prime.expected_transmit_time = transmit_time2 + std::chrono::nanoseconds(1);
        REQUIRE(tests::check_read_result({message2b_prime, message3}, message_reader));
      }
    }
  }
}

} // namespace
} // namespace clockwork_logging::offboard
