// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/file_chunk_reader.hh"
#include "clockwork/logging/offboard/file_chunk_writer.hh"
#include "clockwork/logging/offboard/index_chunk_reader.hh"
#include "clockwork/logging/offboard/index_chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/reader_types.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/filesystem_wrapper.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace clockwork_logging::offboard
{
namespace
{

/// Test channel index entry
struct TestChannelIndexEntry
{
  /// Channel name
  std::string channel_name;

  /// Channel ID
  uint16_t channel_id{};

  /// Compression type
  CompressionType compression_type{};

  /// Channel index
  std::vector<IndexChunkIndexEntry> channel_index;
};

/// Expected chunk handle
struct ExpectedChunkHandle
{
  /// Pointer to the test channel index entry
  jewels::memory::ObjectPtr<const TestChannelIndexEntry> index_entry_ptr;

  /// Index of the expected entry in the channel index
  size_t chunk_index;
};

/// Validate the contents of a message chunk handle
/// @param[in] chunk_handle Message chunk handle to validate
/// @param[in] expected_chunk_handle Expected chunk handle
/// @param[in] maybe_log_interval Optional log interval
/// @return True iff the contents of the chunk handle match the expected contents
[[nodiscard]] bool validate_message_chunk_handle(
  const reader::MessageChunkHandle& chunk_handle,
  const ExpectedChunkHandle& expected_chunk_handle,
  const std::optional<LogInterval>& maybe_log_interval)
{
  if (chunk_handle.channel_name != expected_chunk_handle.index_entry_ptr->channel_name)
  {
    jewels::log_cerr_error(
      "Channel name mismatch: actual: {}, expected {}",
      chunk_handle.channel_name,
      expected_chunk_handle.index_entry_ptr->channel_name);
    return false;
  }
  if (chunk_handle.chunk_handle.compression_type != expected_chunk_handle.index_entry_ptr->compression_type)
  {
    jewels::log_cerr_error(
      "Channel name mismatch: actual: {}, expected {}",
      chunk_handle.chunk_handle.compression_type,
      expected_chunk_handle.index_entry_ptr->compression_type);
    return false;
  }
  if (chunk_handle.maybe_log_interval != maybe_log_interval)
  {
    jewels::log_cerr_error("Log interval mismatch");
    return false;
  }
  const auto expected_min_transmit_time_ns =
    expected_chunk_handle.index_entry_ptr->channel_index.at(expected_chunk_handle.chunk_index).min_transmit_time_ns;
  if (chunk_handle.min_transmit_time.get_nanoseconds() != expected_min_transmit_time_ns)
  {
    jewels::log_cerr_error(
      "Min transmit time mismatch: actual: {}, expected {}",
      chunk_handle.min_transmit_time.get_nanoseconds(),
      expected_min_transmit_time_ns);
    return false;
  }
  const auto expected_chunk_offset =
    expected_chunk_handle.index_entry_ptr->channel_index.at(expected_chunk_handle.chunk_index).location.chunk_offset;
  if (chunk_handle.chunk_handle.location.chunk_offset != expected_chunk_offset)
  {
    jewels::log_cerr_error(
      "Chunk offset mismatch: actual: {}, expected {}",
      chunk_handle.chunk_handle.location.chunk_offset,
      expected_chunk_offset);
    return false;
  }
  const auto expected_chunk_size =
    expected_chunk_handle.index_entry_ptr->channel_index.at(expected_chunk_handle.chunk_index).location.chunk_size;
  if (chunk_handle.chunk_handle.location.chunk_size != expected_chunk_size)
  {
    jewels::log_cerr_error(
      "Chunk size mismatch: actual: {}, expected {}",
      chunk_handle.chunk_handle.location.chunk_size,
      expected_chunk_size);
    return false;
  }
  return true;
}

/// Validate the chunk handles returned from the index reader
/// @param[in] chunk_handles Chunk handes to validate
/// @param[in] expected_chunk_handles Expected chunk handles
/// @param[in] maybe_log_interval Optional log interval
/// @return True iff the contents of the chunk handles match the expected contents
[[nodiscard]] bool validate_message_chunk_handles(
  const std::pmr::list<reader::MessageChunkHandle>& chunk_handles,
  const std::vector<ExpectedChunkHandle>& expected_chunk_handles,
  const std::optional<LogInterval>& maybe_log_interval)
{
  if (chunk_handles.size() != expected_chunk_handles.size())
  {
    jewels::log_cerr_error(
      "Chunk list size mismatch: actual {}, expected: {}", chunk_handles.size(), expected_chunk_handles.size());
  }
  size_t index = 0;
  for (const auto& chunk_handle : chunk_handles)
  {
    if (!validate_message_chunk_handle(chunk_handle, expected_chunk_handles.at(index), maybe_log_interval))
    {
      return false;
    }
    ++index;
  }
  return true;
}

TEST_CASE("IndexChunkReader")
{
  constexpr auto test_file_name = "test_file.slog";

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_file_path = test_dir.get_path() / test_file_name;
  const auto compressor_ptr = jewels::memory::make_shared<ChunkCompressor>(memory_resource);
  const auto writer_result = FileChunkWriter<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(writer_result);
  auto& file_writer = *writer_result.value();
  const auto reader_result = FileChunkReader<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(reader_result);
  auto& file_reader = *reader_result.value();
  IndexChunkWriter index_writer{memory_resource};

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

  SECTION("Empty chunk")
  {
    REQUIRE(file_writer.open());
    const auto write_result = index_writer.write_chunk(*compressor_ptr, file_writer);
    REQUIRE(write_result);
    REQUIRE(file_writer.close());
    REQUIRE(file_reader.open());
    REQUIRE(validate_message_chunk_handles({}, {}, {}));
  }

  SECTION("Chunk with multiple channels")
  {
    static constexpr auto time1 = LogTimestamp{std::chrono::seconds(1)};
    static constexpr auto time2 = LogTimestamp{std::chrono::seconds(2)};
    static constexpr auto time3 = LogTimestamp{std::chrono::seconds(3)};
    static constexpr auto time4 = LogTimestamp{std::chrono::seconds(4)};
    static constexpr auto time5 = LogTimestamp{std::chrono::seconds(5)};
    static constexpr auto time6 = LogTimestamp{std::chrono::seconds(6)};
    static constexpr auto time7 = LogTimestamp{std::chrono::seconds(7)};
    static constexpr auto time8 = LogTimestamp{std::chrono::seconds(8)};

    const std::vector<TestChannelIndexEntry> expected_indexes = {
      TestChannelIndexEntry{
        .channel_name = "channel1",
        .channel_id = 1U,
        .compression_type = CompressionType::zstd,
        .channel_index =
          std::vector<IndexChunkIndexEntry>{
            IndexChunkIndexEntry{
              .min_transmit_time_ns = time5.get_nanoseconds(),
              .max_transmit_time_ns = time6.get_nanoseconds(),
              .location =
                ChunkLocation{
                  .chunk_offset = 3U,
                  .chunk_size = 4U,
                },
            },
            IndexChunkIndexEntry{
              .min_transmit_time_ns = time3.get_nanoseconds(),
              .max_transmit_time_ns = time4.get_nanoseconds(),
              .location =
                ChunkLocation{
                  .chunk_offset = 5U,
                  .chunk_size = 6U,
                },
            },
          },
      },
      TestChannelIndexEntry{
        .channel_name = "channel2",
        .channel_id = 2U,
        .compression_type = CompressionType::none,
        .channel_index =
          std::vector<IndexChunkIndexEntry>{
            IndexChunkIndexEntry{
              .min_transmit_time_ns = time7.get_nanoseconds(),
              .max_transmit_time_ns = time8.get_nanoseconds(),
              .location =
                ChunkLocation{
                  .chunk_offset = 1U,
                  .chunk_size = 2U,
                },
            },
            IndexChunkIndexEntry{
              .min_transmit_time_ns = time1.get_nanoseconds(),
              .max_transmit_time_ns = time2.get_nanoseconds(),
              .location =
                ChunkLocation{
                  .chunk_offset = 7U,
                  .chunk_size = 8U,
                },
            },
          },
      },
    };
    for (const auto& expected_index : expected_indexes)
    {
      index_writer.add_channel_index(expected_index.channel_id, expected_index.channel_index);
    }
    REQUIRE(file_writer.open());
    const auto write_result = index_writer.write_chunk(*compressor_ptr, file_writer);
    REQUIRE(write_result);
    REQUIRE(file_writer.close());
    REQUIRE(file_reader.open());

    SECTION("No filters")
    {
      std::pmr::unordered_set<std::pmr::string> desired_channels = {"channel1", "channel2"};
      auto read_index_result = read_index_chunk(
        memory_resource,
        write_result.value(),
        channel_info_map,
        {},
        {},
        desired_channels,
        reader_result.value(),
        compressor_ptr);
      REQUIRE(read_index_result);
      const std::vector<ExpectedChunkHandle> expected_chunk_handles = {
        ExpectedChunkHandle{
          .index_entry_ptr = jewels::memory::make_non_null_from_ref(expected_indexes.at(1U)),
          .chunk_index = 1U,
        },
        ExpectedChunkHandle{
          .index_entry_ptr = jewels::memory::make_non_null_from_ref(expected_indexes.at(0U)),
          .chunk_index = 1U,
        },
        ExpectedChunkHandle{
          .index_entry_ptr = jewels::memory::make_non_null_from_ref(expected_indexes.at(0U)),
          .chunk_index = 0U,
        },
        ExpectedChunkHandle{
          .index_entry_ptr = jewels::memory::make_non_null_from_ref(expected_indexes.at(1U)),
          .chunk_index = 0U,
        },
      };
      REQUIRE(validate_message_chunk_handles(read_index_result.value(), expected_chunk_handles, {}));
    }

    SECTION("Filter by time range")
    {
      std::pmr::unordered_set<std::pmr::string> desired_channels = {"channel1", "channel2"};
      auto read_index_result = read_index_chunk(
        memory_resource,
        write_result.value(),
        channel_info_map,
        {},
        {{time1, time6}},
        desired_channels,
        reader_result.value(),
        compressor_ptr);
      REQUIRE(read_index_result);
      const std::vector<ExpectedChunkHandle> expected_chunk_handles = {
        ExpectedChunkHandle{
          .index_entry_ptr = jewels::memory::make_non_null_from_ref(expected_indexes.at(1U)),
          .chunk_index = 1U,
        },
        ExpectedChunkHandle{
          .index_entry_ptr = jewels::memory::make_non_null_from_ref(expected_indexes.at(0U)),
          .chunk_index = 1U,
        },
        ExpectedChunkHandle{
          .index_entry_ptr = jewels::memory::make_non_null_from_ref(expected_indexes.at(0U)),
          .chunk_index = 0U,
        },
      };
      REQUIRE(validate_message_chunk_handles(read_index_result.value(), expected_chunk_handles, {{time1, time6}}));
    }

    SECTION("Filter by excluded channel ID")
    {
      std::pmr::unordered_set<uint16_t> channel_ids_to_exclude = {2U};
      auto read_index_result = read_index_chunk(
        memory_resource,
        write_result.value(),
        channel_info_map,
        channel_ids_to_exclude,
        {},
        {},
        reader_result.value(),
        compressor_ptr);
      REQUIRE(read_index_result);
      const std::vector<ExpectedChunkHandle> expected_chunk_handles = {
        ExpectedChunkHandle{
          .index_entry_ptr = jewels::memory::make_non_null_from_ref(expected_indexes.at(0U)),
          .chunk_index = 1U,
        },
        ExpectedChunkHandle{
          .index_entry_ptr = jewels::memory::make_non_null_from_ref(expected_indexes.at(0U)),
          .chunk_index = 0U,
        },
      };
      REQUIRE(validate_message_chunk_handles(read_index_result.value(), expected_chunk_handles, {}));
    }

    SECTION("Filter by channel name")
    {
      std::pmr::unordered_set<std::pmr::string> desired_channels = {"channel1"};
      auto read_index_result = read_index_chunk(
        memory_resource,
        write_result.value(),
        channel_info_map,
        {},
        {},
        desired_channels,
        reader_result.value(),
        compressor_ptr);
      REQUIRE(read_index_result);
      const std::vector<ExpectedChunkHandle> expected_chunk_handles = {
        ExpectedChunkHandle{
          .index_entry_ptr = jewels::memory::make_non_null_from_ref(expected_indexes.at(0U)),
          .chunk_index = 1U,
        },
        ExpectedChunkHandle{
          .index_entry_ptr = jewels::memory::make_non_null_from_ref(expected_indexes.at(0U)),
          .chunk_index = 0U,
        },
      };
      REQUIRE(validate_message_chunk_handles(read_index_result.value(), expected_chunk_handles, {}));
    }

    SECTION("Filter by channel name and time range")
    {
      std::pmr::unordered_set<std::pmr::string> desired_channels = {"channel1"};
      auto read_index_result = read_index_chunk(
        memory_resource,
        write_result.value(),
        channel_info_map,
        {},
        {{time5, time6}},
        desired_channels,
        reader_result.value(),
        compressor_ptr);
      REQUIRE(read_index_result);
      const std::vector<ExpectedChunkHandle> expected_chunk_handles = {
        ExpectedChunkHandle{
          .index_entry_ptr = jewels::memory::make_non_null_from_ref(expected_indexes.at(0U)),
          .chunk_index = 0U,
        },
      };
      REQUIRE(validate_message_chunk_handles(read_index_result.value(), expected_chunk_handles, {{time5, time6}}));
    }
  }

  SECTION("Read fails")
  {
    REQUIRE(file_writer.open());
    const auto write_result = index_writer.write_chunk(*compressor_ptr, file_writer);
    REQUIRE(write_result);
    REQUIRE(file_writer.close());
    REQUIRE(reader_result.value()->open());
    reader_result.value()->filesystem().inject_read_error(EIO);
    REQUIRE(
      read_index_chunk(
        memory_resource, write_result.value(), channel_info_map, {}, {}, {}, reader_result.value(), compressor_ptr) ==
      jewels::unexpected(LogError::io_error));
  }
}

} // namespace
} // namespace clockwork_logging::offboard
