// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/file_chunk_reader.hh"
#include "clockwork/logging/offboard/file_chunk_writer.hh"
#include "clockwork/logging/offboard/log_file_trailer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/reader_types.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/filesystem_wrapper.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <memory>
#include <memory_resource>
#include <string>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("Log file trailer chunk reader/writer")
{
  constexpr auto test_file_name = "test_file.slog";

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_file_path = test_dir.get_path() / test_file_name;
  const auto compressor_ptr = jewels::memory::make_shared<ChunkCompressor>(memory_resource);
  const auto writer_result = FileChunkWriter<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(writer_result);
  const auto& file_writer_ptr = writer_result.value();
  const auto reader_result = FileChunkReader<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(reader_result);
  const auto& file_reader_ptr = reader_result.value();

  const auto metadata_chunk_location = ChunkLocation{
    .chunk_offset = 12345U,
    .chunk_size = 123U,
  };

  const auto metrics_chunk_location = ChunkLocation{
    .chunk_offset = 23456U,
    .chunk_size = 234U,
  };

  const auto index_chunk_location = ChunkLocation{
    .chunk_offset = 34567U,
    .chunk_size = 345U,
  };

  SECTION("Read/write log file trailer")
  {
    REQUIRE(file_writer_ptr->open());
    REQUIRE(write_log_file_trailer(
      memory_resource,
      metadata_chunk_location,
      metrics_chunk_location,
      index_chunk_location,
      *file_writer_ptr,
      *compressor_ptr));
    REQUIRE(file_writer_ptr->close());
    REQUIRE(file_reader_ptr->open());
    const auto read_result = read_log_file_trailer(file_reader_ptr, compressor_ptr);
    REQUIRE(read_result);
    const auto& trailer_info = read_result.value();
    REQUIRE(trailer_info.metadata_chunk_handle.location.chunk_offset == metadata_chunk_location.chunk_offset);
    REQUIRE(trailer_info.metadata_chunk_handle.location.chunk_size == metadata_chunk_location.chunk_size);
    REQUIRE(trailer_info.metrics_chunk_handle.location.chunk_offset == metrics_chunk_location.chunk_offset);
    REQUIRE(trailer_info.metrics_chunk_handle.location.chunk_size == metrics_chunk_location.chunk_size);
    REQUIRE(trailer_info.index_chunk_handle.location.chunk_offset == index_chunk_location.chunk_offset);
    REQUIRE(trailer_info.index_chunk_handle.location.chunk_size == index_chunk_location.chunk_size);
  }

  SECTION("Error handling")
  {
    SECTION("Writer not open")
    {
      REQUIRE(
        write_log_file_trailer(
          memory_resource,
          metadata_chunk_location,
          metrics_chunk_location,
          index_chunk_location,
          *file_writer_ptr,
          *compressor_ptr) == jewels::unexpected(LogError::not_open));
    }

    SECTION("Write fails")
    {
      REQUIRE(file_writer_ptr->open());
      file_writer_ptr->filesystem().inject_write_error(EIO);
      REQUIRE(
        write_log_file_trailer(
          memory_resource,
          metadata_chunk_location,
          metrics_chunk_location,
          index_chunk_location,
          *file_writer_ptr,
          *compressor_ptr) == jewels::unexpected(LogError::io_error));
    }

    SECTION("Reader not open")
    {
      REQUIRE(file_writer_ptr->open());
      REQUIRE(write_log_file_trailer(
        memory_resource,
        metadata_chunk_location,
        metrics_chunk_location,
        index_chunk_location,
        *file_writer_ptr,
        *compressor_ptr));
      REQUIRE(file_writer_ptr->close());
      REQUIRE(read_log_file_trailer(file_reader_ptr, compressor_ptr) == jewels::unexpected(LogError::not_open));
    }

    SECTION("Invalid log file")
    {
      REQUIRE(file_writer_ptr->open());
      REQUIRE(file_writer_ptr->close());
      REQUIRE(file_reader_ptr->open());
      REQUIRE(
        read_log_file_trailer(file_reader_ptr, compressor_ptr) == jewels::unexpected(LogError::invalid_file_chunk));
    }

    SECTION("Read fails")
    {
      REQUIRE(file_writer_ptr->open());
      REQUIRE(write_log_file_trailer(
        memory_resource,
        metadata_chunk_location,
        metrics_chunk_location,
        index_chunk_location,
        *file_writer_ptr,
        *compressor_ptr));
      REQUIRE(file_writer_ptr->close());
      REQUIRE(file_reader_ptr->open());
      file_reader_ptr->filesystem().inject_read_error(EIO);
      REQUIRE(read_log_file_trailer(file_reader_ptr, compressor_ptr) == jewels::unexpected(LogError::io_error));
    }

    SECTION("Decompression fails")
    {
      REQUIRE(file_writer_ptr->open());
      REQUIRE(write_log_file_trailer(
        memory_resource,
        metadata_chunk_location,
        metrics_chunk_location,
        index_chunk_location,
        *file_writer_ptr,
        *compressor_ptr));
      REQUIRE(file_writer_ptr->close());
      REQUIRE(onboard::tests::corrupt_log_file(test_file_path.string(), 10U, "XXX"));
      REQUIRE(file_reader_ptr->open());
      REQUIRE(
        read_log_file_trailer(file_reader_ptr, compressor_ptr) == jewels::unexpected(LogError::decompression_failure));
    }
  }
}

} // namespace
} // namespace clockwork_logging::offboard
