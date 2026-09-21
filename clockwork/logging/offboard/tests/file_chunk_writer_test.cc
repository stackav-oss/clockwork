// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/file_chunk_writer.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/filesystem_wrapper.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstring>
#include <memory>
#include <memory_resource>
#include <string>
#include <vector>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("FileChunkWriter")
{
  constexpr auto test_file_name = "test_file.slog";

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_file_path = test_dir.get_path() / test_file_name;

  constexpr auto chunk1_size = 1234U;
  const std::pmr::vector<std::byte> chunk1(chunk1_size, std::byte{'1'}, memory_resource);
  constexpr auto chunk2_size = 2345U;
  const std::pmr::vector<std::byte> chunk2(chunk2_size, std::byte{'2'}, memory_resource);

  const auto writer_result = FileChunkWriter<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(writer_result);
  auto& writer = *writer_result.value();

  SECTION("Write file")
  {
    REQUIRE(writer.open());
    const auto start_time = jewels::time::SteadyClock::now();
    REQUIRE(writer.write_chunk(chunk1) == 0U);
    REQUIRE(writer.get_file_size() == chunk1_size);
    REQUIRE(writer.write_chunk(chunk2) == chunk1_size);
    REQUIRE(writer.get_file_size() == chunk1_size + chunk2_size);
    const auto close_result = writer.close();
    REQUIRE(close_result);
    const auto& write_metrics = close_result.value();
    const auto end_time = jewels::time::SteadyClock::now();
    REQUIRE(write_metrics.byte_count == chunk1_size + chunk2_size);
    REQUIRE(write_metrics.write_count == 2U);
    REQUIRE(write_metrics.write_latency > std::chrono::nanoseconds(0));
    REQUIRE(write_metrics.write_latency <= end_time - start_time);

    const auto read_result = onboard::tests::try_read_file(test_file_path.string());
    REQUIRE(read_result);
    REQUIRE(read_result.value().size() == chunk1_size + chunk2_size);
    REQUIRE(std::memcmp(chunk1.data(), read_result.value().data(), chunk1_size) == 0);
    REQUIRE(std::memcmp(chunk2.data(), &read_result.value().at(chunk1_size), chunk2_size) == 0);
  }

  SECTION("Error handling")
  {
    SECTION("open fails")
    {
      writer.filesystem().inject_open_error(EEXIST);
      REQUIRE(writer.open() == jewels::unexpected(LogError::file_exists));
    }

    SECTION("write fails")
    {
      REQUIRE(writer.open());
      writer.filesystem().inject_write_error(ENOSPC);
      REQUIRE(writer.write_chunk(chunk1) == jewels::unexpected(LogError::no_space_on_device));
    }

    SECTION("not open")
    {
      REQUIRE(writer.write_chunk(chunk1) == jewels::unexpected(LogError::not_open));
      REQUIRE(writer.get_file_size() == jewels::unexpected(LogError::not_open));
      REQUIRE(writer.close() == jewels::unexpected(LogError::not_open));
    }

    SECTION("already closed")
    {
      REQUIRE(writer.open());
      REQUIRE(writer.close());
      REQUIRE(writer.open() == jewels::unexpected(LogError::already_closed));
      REQUIRE(writer.get_file_size() == jewels::unexpected(LogError::already_closed));
      REQUIRE(writer.write_chunk(chunk1) == jewels::unexpected(LogError::already_closed));
      REQUIRE(writer.close() == jewels::unexpected(LogError::already_closed));
    }

    SECTION("invalid URI")
    {
      REQUIRE(
        FileChunkWriter<>::make_shared("s3://host/path", memory_resource) ==
        jewels::unexpected(LogError::invalid_log_uri));
    }
  }
}

} // namespace
} // namespace clockwork_logging::offboard
