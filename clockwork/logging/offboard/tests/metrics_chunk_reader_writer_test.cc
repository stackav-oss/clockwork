// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/file_chunk_reader.hh"
#include "clockwork/logging/offboard/file_chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/metrics_chunk_reader.hh"
#include "clockwork/logging/offboard/metrics_chunk_writer.hh"
#include "clockwork/logging/offboard/reader_types.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/filesystem_wrapper.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory_resource>
#include <string>
#include <unordered_map>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("Metrics chunk reader/writer")
{
  constexpr auto test_file_name = "test_file.slog";

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_file_path = test_dir.get_path() / test_file_name;
  ChunkCompressor compressor{memory_resource};
  const auto writer_result = FileChunkWriter<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(writer_result);
  auto& file_writer = *writer_result.value();
  const auto reader_result = FileChunkReader<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(reader_result);
  auto& file_reader = *reader_result.value();
  MetricsChunkWriter metrics_writer{memory_resource};

  std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo> channel_info_map{memory_resource};
  channel_info_map.emplace(
    1U,
    reader::LoggedChannelInfo{
      .compression_type = CompressionType::zstd,
      .channel_name = "channel1",
    });
  channel_info_map.emplace(
    2U,
    reader::LoggedChannelInfo{
      .compression_type = CompressionType::none,
      .channel_name = "channel2",
    });

  SECTION("Empty metrics")
  {
    REQUIRE(file_writer.open());
    const auto write_result = metrics_writer.write_chunk(compressor, file_writer);
    REQUIRE(write_result);
    REQUIRE(file_writer.close());
    REQUIRE(file_reader.open());
    const auto read_result =
      read_metrics_chunk(memory_resource, write_result.value(), channel_info_map, file_reader, compressor);
    REQUIRE(read_result);
    const auto& log_metrics = read_result.value();
    REQUIRE(log_metrics.message_count == 0U);
    REQUIRE(log_metrics.byte_count == 0U);
    REQUIRE(log_metrics.transmit_time_interval == LogInterval{});
    REQUIRE(log_metrics.metrics_map.empty());
  }

  SECTION("Chunk with metrics")
  {
    constexpr LogTimestamp time1{std::chrono::seconds(1)};
    constexpr LogTimestamp time2{std::chrono::seconds(2)};
    metrics_writer.count_message(1U, time1, 1U);
    metrics_writer.count_message(2U, time2, 2U);
    constexpr LogTimestamp time10{std::chrono::seconds(10)};
    constexpr LogTimestamp time20{std::chrono::seconds(20)};
    metrics_writer.count_message(1U, time10, 10U);
    metrics_writer.count_message(2U, time20, 20U);
    constexpr LogTimestamp time100{std::chrono::seconds(100)};
    constexpr LogTimestamp time200{std::chrono::seconds(200)};
    metrics_writer.count_message(1U, time100, 100U);
    metrics_writer.count_message(2U, time200, 200U);
    REQUIRE(file_writer.open());
    const auto write_result = metrics_writer.write_chunk(compressor, file_writer);
    REQUIRE(write_result);
    REQUIRE(file_writer.close());
    REQUIRE(file_reader.open());

    const auto read_result =
      read_metrics_chunk(memory_resource, write_result.value(), channel_info_map, file_reader, compressor);
    REQUIRE(read_result);
    const auto& log_metrics = read_result.value();
    REQUIRE(log_metrics.message_count == 6U);
    REQUIRE(log_metrics.byte_count == 333U);
    REQUIRE(log_metrics.transmit_time_interval == LogInterval{time1, time200});
    REQUIRE(log_metrics.metrics_map.size() == 2U);
    REQUIRE(log_metrics.metrics_map.contains("channel1"));
    REQUIRE(log_metrics.metrics_map.at("channel1").message_count == 3U);
    REQUIRE(log_metrics.metrics_map.at("channel1").byte_count == 111U);
    REQUIRE(log_metrics.metrics_map.at("channel1").transmit_time_interval == LogInterval{time1, time100});
    REQUIRE(log_metrics.metrics_map.contains("channel2"));
    REQUIRE(log_metrics.metrics_map.at("channel2").message_count == 3U);
    REQUIRE(log_metrics.metrics_map.at("channel2").byte_count == 222U);
    REQUIRE(log_metrics.metrics_map.at("channel2").transmit_time_interval == LogInterval{time2, time200});
  }

  SECTION("Error handling")
  {
    SECTION("Writer not open")
    {
      REQUIRE(metrics_writer.write_chunk(compressor, file_writer) == jewels::unexpected(LogError::not_open));
    }

    SECTION("Write fails")
    {
      REQUIRE(file_writer.open());
      file_writer.filesystem().inject_write_error(EIO);
      REQUIRE(metrics_writer.write_chunk(compressor, file_writer) == jewels::unexpected(LogError::io_error));
    }

    SECTION("Reader not open")
    {
      REQUIRE(file_writer.open());
      const auto write_result = metrics_writer.write_chunk(compressor, file_writer);
      REQUIRE(write_result);
      REQUIRE(file_writer.close());
      REQUIRE(
        read_metrics_chunk(memory_resource, write_result.value(), channel_info_map, file_reader, compressor) ==
        jewels::unexpected(LogError::not_open));
    }

    SECTION("Read fails")
    {
      REQUIRE(file_writer.open());
      const auto write_result = metrics_writer.write_chunk(compressor, file_writer);
      REQUIRE(write_result);
      REQUIRE(file_writer.close());
      REQUIRE(file_reader.open());
      file_reader.filesystem().inject_read_error(EIO);
      REQUIRE(
        read_metrics_chunk(memory_resource, write_result.value(), channel_info_map, file_reader, compressor) ==
        jewels::unexpected(LogError::io_error));
    }

    SECTION("Decompression fails")
    {
      REQUIRE(file_writer.open());
      const auto write_result = metrics_writer.write_chunk(compressor, file_writer);
      REQUIRE(write_result);
      REQUIRE(file_writer.close());
      REQUIRE(onboard::tests::corrupt_log_file(test_file_path.string(), 10U, "XXX"));
      REQUIRE(file_reader.open());
      REQUIRE(
        read_metrics_chunk(memory_resource, write_result.value(), channel_info_map, file_reader, compressor) ==
        jewels::unexpected(LogError::decompression_failure));
    }
  }
}

} // namespace
} // namespace clockwork_logging::offboard
