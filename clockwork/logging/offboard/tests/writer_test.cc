// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/reader.hh"
#include "clockwork/logging/offboard/tests/support/test_support.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/v1/log_metadata.pb.h"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "clockwork/logging/tests/support/test_message.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fmt10/format.h>
#include <google/protobuf/repeated_ptr_field.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging::offboard
{
namespace
{

constexpr auto clk_repo = "clockwork";

TEST_CASE("Writer, offload interface")
{
  constexpr auto test_log_name = "test_log";

  const auto message_chunk_index_format = GENERATE(MessageChunkIndexFormat::v1, MessageChunkIndexFormat::v2);
  CAPTURE(message_chunk_index_format);

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_log_path = test_dir.get_path() / test_log_name;
  Writer writer{memory_resource, message_chunk_index_format};
  LiteCompressor lite_compressor{memory_resource};

  constexpr LogTimestamp time1{std::chrono::seconds(1)};
  constexpr LogTimestamp time2{std::chrono::seconds(2)};
  constexpr LogTimestamp time3{std::chrono::seconds(3)};
  constexpr LogTimestamp time4{std::chrono::seconds(4)};

  constexpr auto channel_name1 = "channel1";
  constexpr auto metadata1 = LoggedChannelMetadata{
    .channel_name = channel_name1,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::regular,
    .schema_name = "schema1",
    .schema_encoding = SchemaEncoding::undefined,
    .schema_definition = "Schema definition 1",
  };
  constexpr auto header1_size = 123U;
  std::vector<std::byte> header1(header1_size);
  onboard::tests::fill_with_random_bytes(header1);
  constexpr auto data1_size = 1234U;
  std::vector<std::byte> data1(data1_size);
  onboard::tests::fill_with_random_bytes(data1);

  constexpr auto channel_name2 = "channel2";
  constexpr auto metadata2 = LoggedChannelMetadata{
    .channel_name = channel_name2,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::persistent,
    .schema_name = "schema2",
    .schema_encoding = SchemaEncoding::unspecified,
    .schema_definition = "Schema definition 1",
  };
  constexpr auto header2_size = 234U;
  std::vector<std::byte> header2(header2_size);
  onboard::tests::fill_with_random_bytes(header2);
  constexpr auto data2_size = 2345U;
  std::vector<std::byte> data2(data2_size);
  onboard::tests::fill_with_random_bytes(data2);

  constexpr auto header3_size = 345U;
  std::vector<std::byte> header3(header3_size);
  onboard::tests::fill_with_random_bytes(header3);
  constexpr auto data3_size = 3456U;
  std::vector<std::byte> data3(data3_size);
  onboard::tests::fill_with_random_bytes(data3);

  constexpr auto header4_size = 456U;
  std::vector<std::byte> header4(header4_size);
  onboard::tests::fill_with_random_bytes(header4);
  constexpr auto data4_size = 4567U;
  std::vector<std::byte> data4(data4_size);
  onboard::tests::fill_with_random_bytes(data4);

  SECTION("Smoke test")
  {
    constexpr auto multi_file_config = R"(
      # proto-file: clockwork/logging/offboard/v1/writer_config.proto
      # proto-message: WriterConfig
      rule {
        compression_type: COMPRESSION_TYPE_ZSTD
        regex: ".*"
      }
    )";
    constexpr auto single_file_config = R"(
      # proto-file: clockwork/logging/offboard/v1/writer_config.proto
      # proto-message: WriterConfig
      rule {
        compression_type: COMPRESSION_TYPE_ZSTD
        regex: ".*"
        file_name_prefix: "channels"
      }
    )";

    const auto multi_file_flag = GENERATE(false, true);

    const auto start_time = jewels::time::SteadyClock::now();
    REQUIRE(writer.open(test_log_path.string(), multi_file_flag ? multi_file_config : single_file_config));
    REQUIRE(writer.create_channel(metadata1));
    REQUIRE(writer.create_channel(metadata2));

    REQUIRE(writer.write(LoggedMessage{
      .channel_name = channel_name1,
      .sequence_number = 1U,
      .log_time = time1,
      .transmit_time = time1,
      .header = header1,
      .data = data1,
      .is_repeated_persistent = true,
      .is_lite_compressed = false,
    }));

    const auto compressed_data2 = offboard::tests::lite_compress(data2, lite_compressor);
    REQUIRE(writer.write(LoggedMessage{
      .channel_name = channel_name2,
      .sequence_number = 2U,
      .log_time = time2,
      .transmit_time = time2,
      .header = header2,
      .data = compressed_data2,
      .is_repeated_persistent = false,
      .is_lite_compressed = true,
    }));

    const auto close_result = writer.close();
    const auto end_time = jewels::time::SteadyClock::now();
    REQUIRE(close_result);
    const auto& write_metrics = close_result.value();
    REQUIRE(write_metrics.write_count != 0U);
    REQUIRE(write_metrics.write_latency > std::chrono::nanoseconds(0));
    REQUIRE(write_metrics.write_latency < end_time - start_time);

    ChunkReaderWriterFactory chunk_rw_factory{memory_resource};
    const auto log_metadata_result = chunk_rw_factory.read_text_proto<::clockwork::logging::offboard::v1::LogMetadata>(
      (test_log_path / log_metadata_filename).string());
    REQUIRE(log_metadata_result);
    const auto& log_metadata_proto = log_metadata_result.value();
    REQUIRE(log_metadata_proto.min_transmit_time_ns() == time1.get_nanoseconds());
    REQUIRE(log_metadata_proto.max_transmit_time_ns() == time2.get_nanoseconds());

    if (multi_file_flag)
    {
      REQUIRE(std::filesystem::exists(test_log_path / "channel1_0.slog"));
      REQUIRE(std::filesystem::exists(test_log_path / "channel2_0.slog"));
      REQUIRE(
        write_metrics.byte_count == std::filesystem::file_size(test_log_path / "channel1_0.slog") +
                                      std::filesystem::file_size(test_log_path / "channel2_0.slog"));

      REQUIRE(log_metadata_proto.log_writer_metadata().size() == 2U);
      for (const auto& log_writer_metadata : log_metadata_proto.log_writer_metadata())
      {
        const auto& channels = log_writer_metadata.channel();
        REQUIRE(channels.size() == 1U);
        const auto& persistent_channels = log_writer_metadata.persistent_channel();
        REQUIRE(log_writer_metadata.log_file_metadata().size() == 1U);
        const auto& file_metadata = log_writer_metadata.log_file_metadata(0);
        if (file_metadata.log_file_name() == "channel1_0.slog")
        {
          REQUIRE(channels.at(0U) == "channel1");
          REQUIRE(persistent_channels.empty());
          REQUIRE(file_metadata.min_transmit_time_ns() == time1.get_nanoseconds());
          REQUIRE(file_metadata.max_transmit_time_ns() == time1.get_nanoseconds());
        }
        else
        {
          REQUIRE(channels.at(0U) == "channel2");
          REQUIRE(persistent_channels.size() == 1U);
          REQUIRE(persistent_channels.at(0U) == "channel2");
          REQUIRE(file_metadata.log_file_name() == "channel2_0.slog");
          REQUIRE(file_metadata.min_transmit_time_ns() == time2.get_nanoseconds());
          REQUIRE(file_metadata.max_transmit_time_ns() == time2.get_nanoseconds());
        }
      }
    }
    else
    {
      REQUIRE(std::filesystem::exists(test_log_path / "channels_0.slog"));
      REQUIRE(write_metrics.byte_count == std::filesystem::file_size(test_log_path / "channels_0.slog"));

      REQUIRE(log_metadata_proto.log_writer_metadata().size() == 1U);
      const auto& log_writer_metadata = log_metadata_proto.log_writer_metadata(0);
      const auto& channels = log_writer_metadata.channel();
      REQUIRE(channels.size() == 2U);
      const auto& persistent_channels = log_writer_metadata.persistent_channel();
      REQUIRE(std::ranges::find(channels, channel_name1) != std::end(channels));
      REQUIRE(std::ranges::find(channels, channel_name2) != std::end(channels));
      REQUIRE(persistent_channels.size() == 1U);
      REQUIRE(persistent_channels.at(0U) == "channel2");
      REQUIRE(log_writer_metadata.log_file_metadata().size() == 1U);
      const auto& file_metadata = log_writer_metadata.log_file_metadata(0);
      REQUIRE(file_metadata.log_file_name() == "channels_0.slog");
      REQUIRE(file_metadata.min_transmit_time_ns() == time1.get_nanoseconds());
      REQUIRE(file_metadata.max_transmit_time_ns() == time2.get_nanoseconds());
    }

    Reader reader{memory_resource, test_log_path.string()};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 2U);
    REQUIRE((*metadata_result)->at(channel_name1) == metadata1);
    REQUIRE((*metadata_result)->at(channel_name2) == metadata2);

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == 2U);
    REQUIRE((*metrics_result)->byte_count == data1.size() + compressed_data2.size() + header1.size() + header2.size());
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time1, time2});
    REQUIRE((*metrics_result)->metrics_map.size() == 2U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).byte_count == data1.size() + header1.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval == LogInterval{time1, time1});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == compressed_data2.size() + header2.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time2, time2});

    REQUIRE(reader.open());
    REQUIRE(reader);

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == time1);
    REQUIRE(read_result->transmit_time == time1);
    REQUIRE(read_result->header.size() == header1.size());
    REQUIRE(std::memcmp(read_result->header.data(), header1.data(), header1.size()) == 0);
    REQUIRE(read_result->data.size() == data1.size());
    REQUIRE(std::memcmp(read_result->data.data(), data1.data(), data1.size()) == 0);
    REQUIRE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.size() == header2.size());
    REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
    REQUIRE(read_result->data.size() == data2.size());
    REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Split log files test")
  {
    constexpr auto multi_file_config = R"(
      # proto-file: clockwork/logging/offboard/v1/writer_config.proto
      # proto-message: WriterConfig
      rule {
        compression_type: COMPRESSION_TYPE_ZSTD
        regex: ".*"
      }
    )";

    const auto start_time = jewels::time::SteadyClock::now();
    REQUIRE(writer.open(test_log_path.string(), multi_file_config));
    REQUIRE(writer.create_channel(metadata1));
    REQUIRE(writer.create_channel(metadata2));

    REQUIRE(writer.write(LoggedMessage{
      .channel_name = channel_name1,
      .sequence_number = 1U,
      .log_time = time1,
      .transmit_time = time1,
      .header = header1,
      .data = data1,
      .is_repeated_persistent = true,
      .is_lite_compressed = false,
    }));

    const auto compressed_data2 = offboard::tests::lite_compress(data2, lite_compressor);
    REQUIRE(writer.write(LoggedMessage{
      .channel_name = channel_name2,
      .sequence_number = 2U,
      .log_time = time2,
      .transmit_time = time2,
      .header = header2,
      .data = compressed_data2,
      .is_repeated_persistent = false,
      .is_lite_compressed = true,
    }));

    REQUIRE(writer.split_log_files());

    REQUIRE(writer.write(LoggedMessage{
      .channel_name = channel_name1,
      .sequence_number = 3U,
      .log_time = time3,
      .transmit_time = time3,
      .header = header3,
      .data = data3,
      .is_repeated_persistent = false,
      .is_lite_compressed = false,
    }));

    const auto compressed_data4 = offboard::tests::lite_compress(data4, lite_compressor);
    REQUIRE(writer.write(LoggedMessage{
      .channel_name = channel_name2,
      .sequence_number = 4U,
      .log_time = time4,
      .transmit_time = time4,
      .header = header4,
      .data = compressed_data4,
      .is_repeated_persistent = false,
      .is_lite_compressed = true,
    }));

    const auto close_result = writer.close();
    const auto end_time = jewels::time::SteadyClock::now();
    REQUIRE(close_result);
    const auto& write_metrics = close_result.value();
    REQUIRE(write_metrics.write_count != 0U);
    REQUIRE(write_metrics.write_latency > std::chrono::nanoseconds(0));
    REQUIRE(write_metrics.write_latency < end_time - start_time);

    REQUIRE(std::filesystem::exists(test_log_path / "channel1_0.slog"));
    REQUIRE(std::filesystem::exists(test_log_path / "channel1_1.slog"));
    REQUIRE(std::filesystem::exists(test_log_path / "channel2_0.slog"));
    REQUIRE(std::filesystem::exists(test_log_path / "channel2_1.slog"));
    REQUIRE(
      write_metrics.byte_count == std::filesystem::file_size(test_log_path / "channel1_0.slog") +
                                    std::filesystem::file_size(test_log_path / "channel1_1.slog") +
                                    std::filesystem::file_size(test_log_path / "channel2_0.slog") +
                                    std::filesystem::file_size(test_log_path / "channel2_1.slog"));

    ChunkReaderWriterFactory chunk_rw_factory{memory_resource};
    const auto log_metadata_result = chunk_rw_factory.read_text_proto<::clockwork::logging::offboard::v1::LogMetadata>(
      (test_log_path / log_metadata_filename).string());
    REQUIRE(log_metadata_result);
    const auto& log_metadata_proto = log_metadata_result.value();

    REQUIRE(log_metadata_proto.min_transmit_time_ns() == time1.get_nanoseconds());
    REQUIRE(log_metadata_proto.max_transmit_time_ns() == time4.get_nanoseconds());
    REQUIRE(log_metadata_proto.log_writer_metadata().size() == 2U);
    for (const auto& log_writer_metadata : log_metadata_proto.log_writer_metadata())
    {
      const auto& channels = log_writer_metadata.channel();
      REQUIRE(channels.size() == 1U);
      const auto& persistent_channels = log_writer_metadata.persistent_channel();
      REQUIRE(log_writer_metadata.log_file_metadata().size() == 2U);
      const auto& file_metadata = log_writer_metadata.log_file_metadata();
      if (file_metadata.at(0U).log_file_name() == "channel1_0.slog")
      {
        REQUIRE(channels.at(0U) == "channel1");
        REQUIRE(persistent_channels.empty());
        REQUIRE(file_metadata.at(0U).min_transmit_time_ns() == time1.get_nanoseconds());
        REQUIRE(file_metadata.at(0U).max_transmit_time_ns() == time1.get_nanoseconds());
        REQUIRE(file_metadata.at(1U).log_file_name() == "channel1_1.slog");
        REQUIRE(file_metadata.at(1U).min_transmit_time_ns() == time3.get_nanoseconds());
        REQUIRE(file_metadata.at(1U).max_transmit_time_ns() == time3.get_nanoseconds());
      }
      else
      {
        REQUIRE(channels.at(0U) == "channel2");
        REQUIRE(persistent_channels.size() == 1U);
        REQUIRE(persistent_channels.at(0U) == "channel2");
        REQUIRE(file_metadata.at(0U).log_file_name() == "channel2_0.slog");
        REQUIRE(file_metadata.at(0U).min_transmit_time_ns() == time2.get_nanoseconds());
        REQUIRE(file_metadata.at(0U).max_transmit_time_ns() == time2.get_nanoseconds());
        REQUIRE(file_metadata.at(1U).log_file_name() == "channel2_1.slog");
        REQUIRE(file_metadata.at(1U).min_transmit_time_ns() == time4.get_nanoseconds());
        REQUIRE(file_metadata.at(1U).max_transmit_time_ns() == time4.get_nanoseconds());
      }
    }

    Reader reader{memory_resource, test_log_path.string()};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 2U);
    REQUIRE((*metadata_result)->at(channel_name1) == metadata1);
    REQUIRE((*metadata_result)->at(channel_name2) == metadata2);

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == 4U);
    REQUIRE(
      (*metrics_result)->byte_count == data1.size() + compressed_data2.size() + data3.size() + compressed_data4.size() +
                                         header1.size() + header2.size() + header3.size() + header4.size());
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time1, time4});
    REQUIRE((*metrics_result)->metrics_map.size() == 2U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == 2U);
    REQUIRE(
      (*metrics_result)->metrics_map.at(channel_name1).byte_count ==
      data1.size() + header1.size() + data3.size() + header3.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval == LogInterval{time1, time3});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 2U);
    REQUIRE(
      (*metrics_result)->metrics_map.at(channel_name2).byte_count ==
      compressed_data2.size() + header2.size() + compressed_data4.size() + header4.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time2, time4});

    REQUIRE(reader.open());
    REQUIRE(reader);

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == time1);
    REQUIRE(read_result->transmit_time == time1);
    REQUIRE(read_result->header.size() == header1.size());
    REQUIRE(std::memcmp(read_result->header.data(), header1.data(), header1.size()) == 0);
    REQUIRE(read_result->data.size() == data1.size());
    REQUIRE(std::memcmp(read_result->data.data(), data1.data(), data1.size()) == 0);
    REQUIRE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.size() == header2.size());
    REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
    REQUIRE(read_result->data.size() == data2.size());
    REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == time3);
    REQUIRE(read_result->transmit_time == time3);
    REQUIRE(read_result->header.size() == header3.size());
    REQUIRE(std::memcmp(read_result->header.data(), header3.data(), header3.size()) == 0);
    REQUIRE(read_result->data.size() == data3.size());
    REQUIRE(std::memcmp(read_result->data.data(), data3.data(), data3.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 4U);
    REQUIRE(read_result->log_time == time4);
    REQUIRE(read_result->transmit_time == time4);
    REQUIRE(read_result->header.size() == header4.size());
    REQUIRE(std::memcmp(read_result->header.data(), header4.data(), header4.size()) == 0);
    REQUIRE(read_result->data.size() == data4.size());
    REQUIRE(std::memcmp(read_result->data.data(), data4.data(), data4.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Error handling")
  {
    SECTION("Not open")
    {
      REQUIRE(writer.create_channel(metadata1) == jewels::unexpected(LogError::not_open));
      REQUIRE(
        writer.write(LoggedMessage{
          .channel_name = channel_name2,
          .sequence_number = 2U,
          .log_time = time2,
          .transmit_time = time2,
          .header = header2,
          .data = data2,
          .is_repeated_persistent = false,
        }) == jewels::unexpected(LogError::not_open));
      REQUIRE(writer.close() == jewels::unexpected(LogError::not_open));
    }

    SECTION("Duplicate channel")
    {
      REQUIRE(writer.open(test_log_path.string()));
      REQUIRE(writer.create_channel(metadata1));
      REQUIRE(writer.create_channel(metadata1) == jewels::unexpected(LogError::channel_already_exists));
    }

    SECTION("Unknown channel")
    {
      REQUIRE(writer.open(test_log_path.string()));
      REQUIRE(
        writer.write(LoggedMessage{
          .channel_name = channel_name2,
          .sequence_number = 2U,
          .log_time = time2,
          .transmit_time = time2,
          .header = header2,
          .data = data2,
          .is_repeated_persistent = false,
        }) == jewels::unexpected(LogError::unknown_channel));
    }

    SECTION("Log exists")
    {
      std::filesystem::create_directories(test_log_path);
      REQUIRE(writer.open(test_log_path.string()) == jewels::unexpected(LogError::log_already_exists));
    }
  }
}

TEST_CASE("Writer, tachyon interface")
{
  constexpr auto test_log_name = "test_log";

  const auto message_chunk_index_format = GENERATE(MessageChunkIndexFormat::v1, MessageChunkIndexFormat::v2);
  CAPTURE(message_chunk_index_format);

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_log_path = test_dir.get_path() / test_log_name;
  Writer writer{memory_resource, message_chunk_index_format};

  constexpr LogTimestamp time1{std::chrono::seconds(1)};
  constexpr LogTimestamp time2{std::chrono::seconds(2)};
  constexpr LogTimestamp time3{std::chrono::seconds(3)};
  constexpr LogTimestamp time4{std::chrono::seconds(4)};

  constexpr auto channel_name1 = "channel1";
  constexpr auto channel_name2 = "channel2";

  clockwork::Tappy<clockwork_logging::tests::TestMessage> message1{};
  message1.get_underlying_message_string().resize(message1.get_underlying_message_string().capacity());
  onboard::tests::fill_with_random_bytes(std::as_writable_bytes(
    std::span{message1.get_underlying_message_string().data(), message1.get_underlying_message_string().size()}));

  clockwork::Tappy<clockwork_logging::tests::TestMessage> message2{};
  message2.get_underlying_message_string().resize(message2.get_underlying_message_string().capacity());
  onboard::tests::fill_with_random_bytes(std::as_writable_bytes(
    std::span{message2.get_underlying_message_string().data(), message2.get_underlying_message_string().size()}));

  clockwork::Tappy<clockwork_logging::tests::TestMessage> message3{};
  message3.get_underlying_message_string().resize(message3.get_underlying_message_string().capacity());
  onboard::tests::fill_with_random_bytes(std::as_writable_bytes(
    std::span{message3.get_underlying_message_string().data(), message3.get_underlying_message_string().size()}));

  clockwork::Tappy<clockwork_logging::tests::TestMessage> message4{};
  message4.get_underlying_message_string().resize(message4.get_underlying_message_string().capacity());
  onboard::tests::fill_with_random_bytes(std::as_writable_bytes(
    std::span{message4.get_underlying_message_string().data(), message4.get_underlying_message_string().size()}));

  SECTION("Smoke test")
  {
    constexpr auto multi_file_config = R"(
      # proto-file: clockwork/logging/offboard/v1/writer_config.proto
      # proto-message: WriterConfig
      rule {
        compression_type: COMPRESSION_TYPE_ZSTD
        regex: ".*"
      }
    )";
    constexpr auto single_file_config = R"(
      # proto-file: clockwork/logging/offboard/v1/writer_config.proto
      # proto-message: WriterConfig
      rule {
        compression_type: COMPRESSION_TYPE_ZSTD
        regex: ".*"
        file_name_prefix: "channels"
      }
    )";

    const auto multi_file_flag = GENERATE(false, true);

    const auto start_time = jewels::time::SteadyClock::now();
    REQUIRE(writer.open(test_log_path.string(), multi_file_flag ? multi_file_config : single_file_config));
    REQUIRE(writer.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage>>(channel_name1));
    REQUIRE(writer.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage>>(
      channel_name2, ChannelType::persistent));

    REQUIRE(writer.write(channel_name1, 1U, time1, time1, message1, true));

    REQUIRE(writer.write(channel_name2, 2U, time2, time2, message2, false));

    const auto close_result = writer.close();
    const auto end_time = jewels::time::SteadyClock::now();
    REQUIRE(close_result);
    const auto& write_metrics = close_result.value();
    REQUIRE(write_metrics.write_count != 0U);
    REQUIRE(write_metrics.write_latency > std::chrono::nanoseconds(0));
    REQUIRE(write_metrics.write_latency < end_time - start_time);

    ChunkReaderWriterFactory chunk_rw_factory{memory_resource};
    const auto log_metadata_result = chunk_rw_factory.read_text_proto<::clockwork::logging::offboard::v1::LogMetadata>(
      (test_log_path / log_metadata_filename).string());
    REQUIRE(log_metadata_result);
    const auto& log_metadata_proto = log_metadata_result.value();
    REQUIRE(log_metadata_proto.min_transmit_time_ns() == time1.get_nanoseconds());
    REQUIRE(log_metadata_proto.max_transmit_time_ns() == time2.get_nanoseconds());

    if (multi_file_flag)
    {
      REQUIRE(std::filesystem::exists(test_log_path / "channel1_0.slog"));
      REQUIRE(std::filesystem::exists(test_log_path / "channel2_0.slog"));
      REQUIRE(
        write_metrics.byte_count == std::filesystem::file_size(test_log_path / "channel1_0.slog") +
                                      std::filesystem::file_size(test_log_path / "channel2_0.slog"));

      REQUIRE(log_metadata_proto.log_writer_metadata().size() == 2U);
      for (const auto& log_writer_metadata : log_metadata_proto.log_writer_metadata())
      {
        const auto& channels = log_writer_metadata.channel();
        REQUIRE(channels.size() == 1U);
        const auto& persistent_channels = log_writer_metadata.persistent_channel();
        REQUIRE(log_writer_metadata.log_file_metadata().size() == 1U);
        const auto& file_metadata = log_writer_metadata.log_file_metadata(0);
        if (file_metadata.log_file_name() == "channel1_0.slog")
        {
          REQUIRE(channels.at(0U) == "channel1");
          REQUIRE(persistent_channels.empty());
          REQUIRE(file_metadata.min_transmit_time_ns() == time1.get_nanoseconds());
          REQUIRE(file_metadata.max_transmit_time_ns() == time1.get_nanoseconds());
        }
        else
        {
          REQUIRE(channels.at(0U) == "channel2");
          REQUIRE(persistent_channels.size() == 1U);
          REQUIRE(persistent_channels.at(0U) == "channel2");
          REQUIRE(file_metadata.log_file_name() == "channel2_0.slog");
          REQUIRE(file_metadata.min_transmit_time_ns() == time2.get_nanoseconds());
          REQUIRE(file_metadata.max_transmit_time_ns() == time2.get_nanoseconds());
        }
      }
    }
    else
    {
      REQUIRE(std::filesystem::exists(test_log_path / "channels_0.slog"));
      REQUIRE(write_metrics.byte_count == std::filesystem::file_size(test_log_path / "channels_0.slog"));

      REQUIRE(log_metadata_proto.log_writer_metadata().size() == 1U);
      const auto& log_writer_metadata = log_metadata_proto.log_writer_metadata(0);
      const auto& channels = log_writer_metadata.channel();
      REQUIRE(channels.size() == 2U);
      const auto& persistent_channels = log_writer_metadata.persistent_channel();
      REQUIRE(std::ranges::find(channels, channel_name1) != std::end(channels));
      REQUIRE(std::ranges::find(channels, channel_name2) != std::end(channels));
      REQUIRE(persistent_channels.size() == 1U);
      REQUIRE(persistent_channels.at(0U) == "channel2");
      REQUIRE(log_writer_metadata.log_file_metadata().size() == 1U);
      const auto& file_metadata = log_writer_metadata.log_file_metadata(0);
      REQUIRE(file_metadata.log_file_name() == "channels_0.slog");
      REQUIRE(file_metadata.min_transmit_time_ns() == time1.get_nanoseconds());
      REQUIRE(file_metadata.max_transmit_time_ns() == time2.get_nanoseconds());
    }

    Reader reader{memory_resource, test_log_path.string()};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 2U);
    const auto& metadata1 = (*metadata_result)->at(channel_name1);
    REQUIRE(metadata1.channel_name == channel_name1);
    REQUIRE(metadata1.message_encoding == MessageEncoding::tachyon);
    REQUIRE(metadata1.schema_encoding == SchemaEncoding::clockwork_tachyon);
    REQUIRE(
      metadata1.schema_name ==
      fmt::format("@{}::clockwork::logging::tests::support::test_message::TestMessage", clk_repo));
    REQUIRE(
      metadata1.schema_definition ==
      std::string_view{
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage>>::schema_definition.data(),
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage>>::schema_definition.size()});
    const auto& metadata2 = (*metadata_result)->at(channel_name2);
    REQUIRE(metadata2.channel_name == channel_name2);
    REQUIRE(metadata2.message_encoding == MessageEncoding::tachyon);
    REQUIRE(metadata2.schema_encoding == SchemaEncoding::clockwork_tachyon);
    REQUIRE(
      metadata2.schema_name ==
      fmt::format("@{}::clockwork::logging::tests::support::test_message::TestMessage", clk_repo));
    REQUIRE(
      metadata2.schema_definition ==
      std::string_view{
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage>>::schema_definition.data(),
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage>>::schema_definition.size()});

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == 2U);
    REQUIRE((*metrics_result)->byte_count == sizeof(message1) + sizeof(message2));
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time1, time2});
    REQUIRE((*metrics_result)->metrics_map.size() == 2U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).byte_count == sizeof(message1));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval == LogInterval{time1, time1});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == sizeof(message2));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time2, time2});

    REQUIRE(reader.open());
    REQUIRE(reader);

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == time1);
    REQUIRE(read_result->transmit_time == time1);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message1));
    REQUIRE(std::memcmp(read_result->data.data(), &message1, read_result->data.size()) == 0);
    REQUIRE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message2));
    REQUIRE(std::memcmp(read_result->data.data(), &message2, read_result->data.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Split log files test")
  {
    constexpr auto multi_file_config = R"(
      # proto-file: clockwork/logging/offboard/v1/writer_config.proto
      # proto-message: WriterConfig
      rule {
        compression_type: COMPRESSION_TYPE_ZSTD
        regex: ".*"
      }
    )";

    const auto start_time = jewels::time::SteadyClock::now();
    REQUIRE(writer.open(test_log_path.string(), multi_file_config));
    REQUIRE(writer.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage>>(channel_name1));
    REQUIRE(writer.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage>>(
      channel_name2, ChannelType::persistent));

    REQUIRE(writer.write(channel_name1, 1U, time1, time1, message1, true));

    REQUIRE(writer.write(channel_name2, 2U, time2, time2, message2, false));

    REQUIRE(writer.split_log_files());

    REQUIRE(writer.write(channel_name1, 3U, time3, time3, message3, false));

    REQUIRE(writer.write(channel_name2, 4U, time4, time4, message4, false));

    const auto close_result = writer.close();
    const auto end_time = jewels::time::SteadyClock::now();
    REQUIRE(close_result);
    const auto& write_metrics = close_result.value();
    REQUIRE(write_metrics.write_count != 0U);
    REQUIRE(write_metrics.write_latency > std::chrono::nanoseconds(0));
    REQUIRE(write_metrics.write_latency < end_time - start_time);

    REQUIRE(std::filesystem::exists(test_log_path / "channel1_0.slog"));
    REQUIRE(std::filesystem::exists(test_log_path / "channel1_1.slog"));
    REQUIRE(std::filesystem::exists(test_log_path / "channel2_0.slog"));
    REQUIRE(std::filesystem::exists(test_log_path / "channel2_1.slog"));
    REQUIRE(
      write_metrics.byte_count == std::filesystem::file_size(test_log_path / "channel1_0.slog") +
                                    std::filesystem::file_size(test_log_path / "channel1_1.slog") +
                                    std::filesystem::file_size(test_log_path / "channel2_0.slog") +
                                    std::filesystem::file_size(test_log_path / "channel2_1.slog"));

    ChunkReaderWriterFactory chunk_rw_factory{memory_resource};
    const auto log_metadata_result = chunk_rw_factory.read_text_proto<::clockwork::logging::offboard::v1::LogMetadata>(
      (test_log_path / log_metadata_filename).string());
    REQUIRE(log_metadata_result);
    const auto& log_metadata_proto = log_metadata_result.value();

    REQUIRE(log_metadata_proto.min_transmit_time_ns() == time1.get_nanoseconds());
    REQUIRE(log_metadata_proto.max_transmit_time_ns() == time4.get_nanoseconds());
    REQUIRE(log_metadata_proto.log_writer_metadata().size() == 2U);
    for (const auto& log_writer_metadata : log_metadata_proto.log_writer_metadata())
    {
      const auto& channels = log_writer_metadata.channel();
      REQUIRE(channels.size() == 1U);
      const auto& persistent_channels = log_writer_metadata.persistent_channel();
      REQUIRE(log_writer_metadata.log_file_metadata().size() == 2U);
      const auto& file_metadata = log_writer_metadata.log_file_metadata();
      if (file_metadata.at(0U).log_file_name() == "channel1_0.slog")
      {
        REQUIRE(channels.at(0U) == "channel1");
        REQUIRE(persistent_channels.empty());
        REQUIRE(file_metadata.at(0U).min_transmit_time_ns() == time1.get_nanoseconds());
        REQUIRE(file_metadata.at(0U).max_transmit_time_ns() == time1.get_nanoseconds());
        REQUIRE(file_metadata.at(1U).log_file_name() == "channel1_1.slog");
        REQUIRE(file_metadata.at(1U).min_transmit_time_ns() == time3.get_nanoseconds());
        REQUIRE(file_metadata.at(1U).max_transmit_time_ns() == time3.get_nanoseconds());
      }
      else
      {
        REQUIRE(channels.at(0U) == "channel2");
        REQUIRE(persistent_channels.size() == 1U);
        REQUIRE(persistent_channels.at(0U) == "channel2");
        REQUIRE(file_metadata.at(0U).log_file_name() == "channel2_0.slog");
        REQUIRE(file_metadata.at(0U).min_transmit_time_ns() == time2.get_nanoseconds());
        REQUIRE(file_metadata.at(0U).max_transmit_time_ns() == time2.get_nanoseconds());
        REQUIRE(file_metadata.at(1U).log_file_name() == "channel2_1.slog");
        REQUIRE(file_metadata.at(1U).min_transmit_time_ns() == time4.get_nanoseconds());
        REQUIRE(file_metadata.at(1U).max_transmit_time_ns() == time4.get_nanoseconds());
      }
    }

    Reader reader{memory_resource, test_log_path.string()};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 2U);
    const auto& metadata1 = (*metadata_result)->at(channel_name1);
    REQUIRE(metadata1.channel_name == channel_name1);
    REQUIRE(metadata1.message_encoding == MessageEncoding::tachyon);
    REQUIRE(metadata1.schema_encoding == SchemaEncoding::clockwork_tachyon);
    REQUIRE(
      metadata1.schema_name ==
      fmt::format("@{}::clockwork::logging::tests::support::test_message::TestMessage", clk_repo));
    REQUIRE(
      metadata1.schema_definition ==
      std::string_view{
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage>>::schema_definition.data(),
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage>>::schema_definition.size()});
    const auto& metadata2 = (*metadata_result)->at(channel_name2);
    REQUIRE(metadata2.channel_name == channel_name2);
    REQUIRE(metadata2.message_encoding == MessageEncoding::tachyon);
    REQUIRE(metadata2.schema_encoding == SchemaEncoding::clockwork_tachyon);
    REQUIRE(
      metadata2.schema_name ==
      fmt::format("@{}::clockwork::logging::tests::support::test_message::TestMessage", clk_repo));
    REQUIRE(
      metadata2.schema_definition ==
      std::string_view{
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage>>::schema_definition.data(),
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage>>::schema_definition.size()});

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == 4U);
    REQUIRE((*metrics_result)->byte_count == sizeof(message1) + sizeof(message2) + sizeof(message3) + sizeof(message4));
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time1, time4});
    REQUIRE((*metrics_result)->metrics_map.size() == 2U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == 2U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).byte_count == sizeof(message1) + sizeof(message3));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval == LogInterval{time1, time3});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 2U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == sizeof(message2) + sizeof(message4));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time2, time4});

    REQUIRE(reader.open());
    REQUIRE(reader);

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == time1);
    REQUIRE(read_result->transmit_time == time1);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message1));
    REQUIRE(std::memcmp(read_result->data.data(), &message1, read_result->data.size()) == 0);
    REQUIRE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message2));
    REQUIRE(std::memcmp(read_result->data.data(), &message2, read_result->data.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == time3);
    REQUIRE(read_result->transmit_time == time3);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message3));
    REQUIRE(std::memcmp(read_result->data.data(), &message3, read_result->data.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 4U);
    REQUIRE(read_result->log_time == time4);
    REQUIRE(read_result->transmit_time == time4);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message4));
    REQUIRE(std::memcmp(read_result->data.data(), &message4, read_result->data.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }
}

} // namespace
} // namespace clockwork_logging::offboard
