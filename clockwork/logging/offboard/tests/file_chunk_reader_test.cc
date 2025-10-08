// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/file_chunk_reader.hh"
#include "clockwork/logging/offboard/file_chunk_writer.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/filesystem_wrapper.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <memory_resource>
#include <string>
#include <vector>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("FileChunkReader")
{
  constexpr auto test_file_name = "test_file.slog";

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_file_path = test_dir.get_path() / test_file_name;

  constexpr auto chunk1_size = 1234U;
  const std::pmr::vector<std::byte> chunk1(chunk1_size, std::byte{'1'}, memory_resource);
  constexpr auto chunk2_size = 2345U;
  const std::pmr::vector<std::byte> chunk2(chunk2_size, std::byte{'2'}, memory_resource);
  const auto writer_result = FileChunkWriter<>::make_shared(test_file_path.string(), memory_resource);
  REQUIRE(writer_result);
  auto& writer = *writer_result.value();
  REQUIRE(writer.open());
  REQUIRE(writer.write_chunk(chunk1));
  REQUIRE(writer.write_chunk(chunk2));
  REQUIRE(writer.close());

  const auto reader_result = FileChunkReader<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(reader_result);
  auto& reader = *reader_result.value();

  SECTION("Read file")
  {
    REQUIRE(reader.open());
    REQUIRE(reader.file_size() == chunk1_size + chunk2_size);
    auto read_result = reader.read_chunk(0U, chunk1_size);
    REQUIRE(reader.read_chunk(0U, chunk1_size) == chunk1);
    REQUIRE(reader.read_chunk(chunk1_size, chunk2_size) == chunk2);
    REQUIRE(reader.close());
  }

  SECTION("Error handling")
  {
    SECTION("open fails")
    {
      reader.filesystem().inject_open_error(ENOENT);
      REQUIRE(reader.open() == jewels::unexpected(LogError::no_such_file_or_directory));
    }

    SECTION("read fails")
    {
      REQUIRE(reader.open());
      reader.filesystem().inject_read_error(EIO);
      REQUIRE(reader.read_chunk(0U, chunk1_size) == jewels::unexpected(LogError::io_error));
    }

    SECTION("get size fails")
    {
      REQUIRE(reader.open());
      reader.filesystem().inject_stat_error(EIO);
      REQUIRE(reader.file_size() == jewels::unexpected(LogError::io_error));
    }

    SECTION("read past end of file")
    {
      REQUIRE(reader.open());
      REQUIRE(reader.read_chunk(chunk1_size + 1U, chunk2_size) == jewels::unexpected(LogError::short_read));
    }

    SECTION("not open")
    {
      REQUIRE(reader.read_chunk(0U, chunk1_size) == jewels::unexpected(LogError::not_open));
      REQUIRE(reader.file_size() == jewels::unexpected(LogError::not_open));
      REQUIRE(reader.close() == jewels::unexpected(LogError::not_open));
    }

    SECTION("already closed")
    {
      REQUIRE(reader.open());
      REQUIRE(reader.close());
      REQUIRE(reader.open() == jewels::unexpected(LogError::already_closed));
      REQUIRE(reader.read_chunk(0U, chunk1_size) == jewels::unexpected(LogError::already_closed));
      REQUIRE(reader.file_size() == jewels::unexpected(LogError::already_closed));
      REQUIRE(reader.close() == jewels::unexpected(LogError::already_closed));
    }

    SECTION("invalid URI")
    {
      REQUIRE(
        FileChunkReader<>::make_shared("s3://host/path", memory_resource) ==
        jewels::unexpected(LogError::invalid_log_uri));
    }
  }
}

} // namespace
} // namespace clockwork_logging::offboard
