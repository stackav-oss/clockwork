// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/file_chunk_reader.hh"
#include "clockwork/logging/offboard/file_chunk_writer.hh"
#include "clockwork/logging/offboard/index_chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/filesystem_wrapper.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <cstdint>
#include <cstring>
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

/// Test channel index entry
struct TestChannelIndexEntry
{
  /// Channel ID
  uint16_t channel_id;

  /// Channel index
  std::vector<IndexChunkIndexEntry> channel_index;
};

/// Validate the contents of an index chunk
/// @param[in] chunk_reader Chunk reader
/// @param[in] chunk_compressor Chunk compressor
/// @param[in] index_entry Index entry referencing the chunk
/// @param[in] expected_channel_entries Vector of expected channel entries
/// @return True iff the contents of the chunk match the expected contents
[[nodiscard]] bool validate_index_chunk(
  ChunkReader& chunk_reader,
  const ChunkCompressor& chunk_compressor,
  const ChunkLocation& location,
  std::vector<TestChannelIndexEntry> expected_channel_entries)
{
  auto read_result = chunk_reader.read_chunk(location.chunk_offset, location.chunk_size);
  if (!read_result)
  {
    jewels::log_cerr_error("read_chunk failed: {}", read_result.error());
    return false;
  }
  auto decompress_result = chunk_compressor.decompress_chunk(std::move(read_result).value(), CompressionType::zstd);
  if (!decompress_result)
  {
    jewels::log_cerr_error("decompress_chunk failed: {}", decompress_result.error());
    return false;
  }
  const auto& chunk = decompress_result.value();
  const auto trailer_offset = chunk.size() - index_chunk_trailer_size;
  const auto trailer_result = nolint_helper::byte_span_to_value_ptr<IndexChunkTrailer>(
    std::span{&chunk.at(trailer_offset), index_chunk_trailer_size});
  if (!trailer_result)
  {
    return false;
  }
  const auto* trailer_ptr = trailer_result.value();
  const auto channel_entries_size =
    (trailer_offset - trailer_ptr->channel_entries_offset) / index_chunk_channel_entry_size;
  if (channel_entries_size != expected_channel_entries.size())
  {
    jewels::log_cerr_error(
      "Channel entries size mismatch, actual: {} expected: {}", channel_entries_size, expected_channel_entries.size());
    return false;
  }
  const auto channel_entries = nolint_helper::byte_span_to_value_span<IndexChunkChannelEntry>(
    std::span{&chunk.at(trailer_ptr->channel_entries_offset), channel_entries_size * index_chunk_channel_entry_size});
  for (size_t index = 0U; index < channel_entries_size; ++index)
  {
    if (channel_entries[index].channel_id != expected_channel_entries.at(index).channel_id)
    {
      jewels::log_cerr_error(
        "invalid channel_id: actual{}, expected {}",
        channel_entries[index].channel_id,
        expected_channel_entries.at(index).channel_id);
      return false;
    }
    if (channel_entries[index].channel_index_size != expected_channel_entries.at(index).channel_index.size())
    {
      jewels::log_cerr_error(
        "Channel index size mismatch: actual{}, expected {}",
        channel_entries[index].channel_index_size,
        expected_channel_entries.at(index).channel_index.size());
      return false;
    }
    const auto* channel_index_ptr = &chunk.at(channel_entries[index].channel_index_offset);
    if (
      !expected_channel_entries.at(index).channel_index.empty() &&
      std::memcmp(
        channel_index_ptr,
        expected_channel_entries.at(index).channel_index.data(),
        expected_channel_entries.at(index).channel_index.size() * index_chunk_index_entry_size) != 0)
    {
      jewels::log_cerr_error("Channel index mismatch");
      return false;
    }
  }
  return true;
}

TEST_CASE("IndexChunkWriter")
{
  constexpr auto test_file_name = "test_file.slog";

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_file_path = test_dir.get_path() / test_file_name;
  const ChunkCompressor compressor{memory_resource};
  const auto writer_result = FileChunkWriter<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(writer_result);
  auto& file_writer = *writer_result.value();
  const auto reader_result = FileChunkReader<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(reader_result);
  auto& file_reader = *reader_result.value();
  IndexChunkWriter index_writer{memory_resource};

  SECTION("Empty chunk")
  {
    REQUIRE(file_writer.open());
    const auto write_result = index_writer.write_chunk(compressor, file_writer);
    REQUIRE(write_result);
    REQUIRE(file_writer.close());
    REQUIRE(file_reader.open());
    REQUIRE(validate_index_chunk(file_reader, compressor, write_result.value(), {}));
  }

  SECTION("Chunk with multiple channels")
  {
    const std::vector<TestChannelIndexEntry> expected_indexes = {
      TestChannelIndexEntry{
        .channel_id = 1U,
        .channel_index =
          std::vector<IndexChunkIndexEntry>{
            IndexChunkIndexEntry{
              .min_transmit_time_ns = 1,
              .max_transmit_time_ns = 2,
              .location =
                ChunkLocation{
                  .chunk_offset = 3U,
                  .chunk_size = 4U,
                },
            },
            IndexChunkIndexEntry{
              .min_transmit_time_ns = 2,
              .max_transmit_time_ns = 3,
              .location =
                ChunkLocation{
                  .chunk_offset = 4U,
                  .chunk_size = 5U,
                },
            },
          },
      },
      TestChannelIndexEntry{
        .channel_id = 2U,
        .channel_index =
          std::vector<IndexChunkIndexEntry>{
            IndexChunkIndexEntry{
              .min_transmit_time_ns = 3,
              .max_transmit_time_ns = 4,
              .location =
                ChunkLocation{
                  .chunk_offset = 5U,
                  .chunk_size = 6U,
                },
            },
            IndexChunkIndexEntry{
              .min_transmit_time_ns = 4,
              .max_transmit_time_ns = 5,
              .location =
                ChunkLocation{
                  .chunk_offset = 6U,
                  .chunk_size = 7U,
                },
            },
          },
      },
      TestChannelIndexEntry{
        .channel_id = 3U,
        .channel_index = std::vector<IndexChunkIndexEntry>{},
      },
    };
    for (const auto& expected_index : expected_indexes)
    {
      index_writer.add_channel_index(expected_index.channel_id, expected_index.channel_index);
    }
    REQUIRE(file_writer.open());
    const auto write_result = index_writer.write_chunk(compressor, file_writer);
    REQUIRE(write_result);
    REQUIRE(file_writer.close());
    REQUIRE(file_reader.open());
    REQUIRE(validate_index_chunk(file_reader, compressor, write_result.value(), expected_indexes));
  }

  SECTION("Writer not open")
  {
    REQUIRE(index_writer.write_chunk(compressor, file_writer) == jewels::unexpected(LogError::not_open));
  }

  SECTION("Write fails")
  {
    REQUIRE(file_writer.open());
    file_writer.filesystem().inject_write_error(ENOSPC);
    REQUIRE(index_writer.write_chunk(compressor, file_writer) == jewels::unexpected(LogError::no_space_on_device));
  }
}

} // namespace
} // namespace clockwork_logging::offboard
