// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/s3_utils.hh"
#include "clockwork/logging/onboard/buffered_reader.hh"
#include "clockwork/logging/onboard/offboard_buffered_reader.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "jewels/aligner/aligner.hh"
#include "jewels/container/at.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/filesystem_wrapper.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <memory_resource>
#include <ranges>
#include <span>
#include <string>
#include <sys/types.h>
#include <type_traits>
#include <vector>

namespace clockwork_logging::onboard
{
namespace
{

struct TestReaderPolicy
{
  /// Filesystem library type
  using FilesystemType = jewels::filesystem::testing::FilesystemWrapper;

  /// S3 utilities library type
  using S3UtilsType = offboard::S3Utils;

  /// Read buffer size
  static constexpr size_t read_buffer_size = 32U;

  /// Maximum read size
  static constexpr size_t max_read_size = 128U;

  /// Minimum size of reads when recovering from I/O error
  static constexpr size_t min_io_error_recover_read_size = 8U;
};

TEMPLATE_TEST_CASE("BufferedReader", "", BufferedReader<TestReaderPolicy>, OffboardBufferedReader<TestReaderPolicy>)
{
  using BufferedReaderType = TestType;

  constexpr auto* test_file_name = "test_file";

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_file_path = test_dir.get_path() / test_file_name;

  constexpr size_t write_data_size = 1011U;
  std::array<std::byte, write_data_size> write_data{};
  tests::fill_with_random_bytes({write_data});
  std::ofstream ofs(test_file_path.c_str());
  ofs.write(nolint_helper::char_ptr_to_span_element(write_data), write_data.size());
  REQUIRE(ofs);
  ofs.close();

  const auto reader_ptr = tests::make_buffered_reader<BufferedReaderType>();
  auto& reader = *reader_ptr;

  SECTION("Read with copy_out")
  {
    const size_t read_size = GENERATE(1U, 17U, 127U, 128U);
    CAPTURE(read_size);
    std::vector<std::byte> read_data(read_size);

    const auto start_time = jewels::time::SteadyClock::now();
    REQUIRE(reader.open(test_file_path.string()));
    REQUIRE(reader);

    size_t read_offset = 0U;
    while (read_offset < write_data_size)
    {
      CAPTURE(read_size, read_offset);
      REQUIRE(reader);
      const auto bytes_to_read = std::min(read_size, write_data_size - read_offset);
      REQUIRE(reader.copy_out(0U, {read_data.data(), bytes_to_read}));
      REQUIRE(std::memcmp(&write_data.at(read_offset), read_data.data(), bytes_to_read) == 0);
      REQUIRE(reader.advance(bytes_to_read));
      read_offset += bytes_to_read;
    }
    const auto end_time = jewels::time::SteadyClock::now();
    REQUIRE_FALSE(reader);
    const auto read_metrics = reader.get_read_metrics();
    REQUIRE(read_metrics.byte_count == write_data_size);
    REQUIRE(read_metrics.read_count != 0U);
    REQUIRE(read_metrics.read_latency > std::chrono::nanoseconds(0));
    REQUIRE(read_metrics.read_latency <= end_time - start_time);
    reader.close();
  }

  SECTION("Read with zero_copy_out and offset")
  {
    constexpr size_t read_size = 127U;
    const size_t offset = GENERATE(1U, 37U, 67U);
    CAPTURE(offset);

    REQUIRE(reader.open(test_file_path.string()));

    size_t read_offset = 0U;
    while (read_offset + offset < write_data_size)
    {
      CAPTURE(read_size, read_offset);
      REQUIRE(reader);
      const auto bytes_to_read = std::min(read_size - offset, write_data_size - read_offset - offset);
      const auto copy_result = reader.zero_copy_out(offset, bytes_to_read);
      REQUIRE(copy_result);
      REQUIRE(
        std::ranges::equal(
          *copy_result | std::views::join, std::span{&write_data.at(read_offset + offset), bytes_to_read}));
      REQUIRE(reader.advance(bytes_to_read));
      read_offset += bytes_to_read;
    }
    REQUIRE(read_offset < write_data_size);
    const auto bytes_to_read = write_data_size - read_offset;
    const auto copy_result = reader.zero_copy_out(0U, bytes_to_read);
    REQUIRE(copy_result);
    REQUIRE(std::ranges::equal(*copy_result | std::views::join, std::span{&write_data.at(read_offset), bytes_to_read}));
    REQUIRE(reader.advance(bytes_to_read));
    REQUIRE_FALSE(reader);
  }

  SECTION("Read with array zero_copy_out")
  {
    constexpr size_t read_size = 127U;
    constexpr size_t header_size = 8U;
    constexpr size_t checksum_size = 8U;

    REQUIRE(reader.open(test_file_path.string()));

    size_t read_offset = 0U;
    while (read_offset < write_data_size)
    {
      CAPTURE(read_size, read_offset);
      REQUIRE(reader);
      const auto header_bytes_to_read = std::min(header_size, write_data_size - read_offset);
      const auto data_bytes_to_read =
        std::min(read_size - header_size - checksum_size, write_data_size - read_offset - header_bytes_to_read);
      const auto checksum_bytes_to_read =
        std::min(checksum_size, write_data_size - read_offset - header_bytes_to_read - data_bytes_to_read);
      const auto copy_result =
        reader.zero_copy_out(0U, header_bytes_to_read, data_bytes_to_read, checksum_bytes_to_read);
      REQUIRE(copy_result);
      REQUIRE(
        std::ranges::equal(
          copy_result->at(0U) | std::views::join, std::span{&write_data.at(read_offset), header_bytes_to_read}));
      REQUIRE(
        std::ranges::equal(
          copy_result->at(1U) | std::views::join,
          std::span{&write_data.at(read_offset + header_bytes_to_read), data_bytes_to_read}));
      REQUIRE(
        std::ranges::equal(
          copy_result->at(2U) | std::views::join,
          std::span{&write_data.at(read_offset + header_bytes_to_read + data_bytes_to_read), checksum_bytes_to_read}));
      const auto total_bytes_to_read = header_bytes_to_read + data_bytes_to_read + checksum_bytes_to_read;
      REQUIRE(reader.advance(total_bytes_to_read));
      read_offset += total_bytes_to_read;
    }
    REQUIRE_FALSE(reader);
  }

  SECTION("Read with I/O error injection")
  {
    if constexpr (std::is_same_v<BufferedReaderType, BufferedReader<TestReaderPolicy>>)
    {
      constexpr size_t read_size = 127;
      const size_t io_error_offset = 319U;
      const size_t io_error_length = 2U;
      const size_t aligned_io_error_start_offset =
        jewels::Aligner<TestReaderPolicy::min_io_error_recover_read_size>::align_prev(io_error_offset);
      const size_t aligned_io_error_end_offset =
        jewels::Aligner<TestReaderPolicy::min_io_error_recover_read_size>::align_next(
          io_error_offset + io_error_length);

      REQUIRE(reader.open(test_file_path.string()));

      auto expected_data = write_data;
      std::memset(
        &expected_data.at(aligned_io_error_start_offset),
        0,
        aligned_io_error_end_offset - aligned_io_error_start_offset);
      reader.get_filesystem().inject_read_io_errors(io_error_offset, io_error_length);

      size_t read_offset = 0U;
      while (read_offset < write_data_size)
      {
        CAPTURE(read_size, read_offset);
        REQUIRE(reader);
        const auto bytes_to_read = std::min(read_size, write_data_size - read_offset);
        const auto copy_result = reader.zero_copy_out(0U, bytes_to_read);
        REQUIRE(copy_result);
        REQUIRE(
          std::ranges::equal(
            *copy_result | std::views::join, std::span{&expected_data.at(read_offset), bytes_to_read}));
        REQUIRE(reader.advance(bytes_to_read));
        read_offset += bytes_to_read;
      }
      REQUIRE_FALSE(reader);
    }
  }

  SECTION("Advance to pattern")
  {
    constexpr std::array pattern_offsets = {11U, 63U, 457U, 823U};
    constexpr std::array pattern = {'_', 'A', 'B', 'C'};

    std::array<std::byte, write_data_size> pattern_write_data{};
    ssize_t pattern_index = 0;
    std::ranges::for_each(
      pattern_write_data,
      [pattern, &pattern_index](auto& value)
      {
        value = static_cast<std::byte>(jewels::at(pattern, pattern_index));
        pattern_index = (pattern_index + 1) % static_cast<ssize_t>(pattern.size() - 1U);
      });
    for (const auto pattern_offset : pattern_offsets)
    {
      std::memcpy(&pattern_write_data.at(pattern_offset), pattern.data(), pattern.size());
    }
    std::memcpy(
      &pattern_write_data.at(pattern_write_data.size() - pattern.size() + 1U), pattern.data(), pattern.size() - 1U);
    ofs.open(test_file_path.c_str(), std::ios::trunc);
    ofs.write(nolint_helper::char_ptr_to_span_element(pattern_write_data), pattern_write_data.size());
    REQUIRE(ofs);
    ofs.close();

    REQUIRE(reader.open(test_file_path.string()));

    for (const auto pattern_offset : pattern_offsets)
    {
      REQUIRE(reader.advance(std::as_bytes(std::span{pattern})));
      REQUIRE(reader.get_window_offset() == pattern_offset);
      REQUIRE(reader.advance(1U));
    }
    REQUIRE(reader.advance(std::as_bytes(std::span{pattern})));
    REQUIRE_FALSE(reader);
  }

  SECTION("Skip pad bytes")
  {
    constexpr std::array non_zero_offsets = {11U, 63U, 457U, 823U};

    std::array<std::byte, write_data_size> test_write_data{};
    for (const auto non_zero_offset : non_zero_offsets)
    {
      jewels::at(test_write_data, static_cast<ssize_t>(non_zero_offset)) = std::byte{'X'};
    }
    ofs.open(test_file_path.c_str(), std::ios::trunc);
    ofs.write(nolint_helper::char_ptr_to_span_element(test_write_data), test_write_data.size());
    REQUIRE(ofs);
    ofs.close();

    REQUIRE(reader.open(test_file_path.string()));

    for (const auto non_zero_offset : non_zero_offsets)
    {
      REQUIRE(reader.skip_pad_bytes());
      REQUIRE(reader.get_window_offset() == non_zero_offset);
      REQUIRE(reader.advance(1U));
    }
    REQUIRE(reader.skip_pad_bytes());
    REQUIRE_FALSE(reader);
  }

  SECTION("Error handling")
  {
    SECTION("Reader not open")
    {
      constexpr size_t read_size = 1U;
      std::array<std::byte, read_size> read_data{};
      REQUIRE_FALSE(reader);
      REQUIRE(reader.copy_out(0U, {read_data}) == jewels::unexpected(LogError::not_open));
      REQUIRE(reader.zero_copy_out(0U, 1U) == jewels::unexpected(LogError::not_open));
      REQUIRE(reader.advance(1U) == jewels::unexpected(LogError::not_open));
      REQUIRE(reader.advance(std::as_bytes(std::span{read_data})) == jewels::unexpected(LogError::not_open));
      REQUIRE(reader.skip_pad_bytes() == jewels::unexpected(LogError::not_open));
      REQUIRE(reader.get_window_offset() == 0U);
      REQUIRE(reader.get_bytes_remaining() == 0U);
    }

    SECTION("Open fails in open")
    {
      if constexpr (std::is_same_v<BufferedReaderType, BufferedReader<TestReaderPolicy>>)
      {
        constexpr size_t read_size = 1U;
        std::array<std::byte, read_size> read_data{};
        reader.get_filesystem().inject_open_error(EBADMSG);
        REQUIRE(reader.open(test_file_path.string()) == jewels::unexpected(LogError::unspecified_system_error));
        REQUIRE(reader.copy_out(0U, {read_data}) == jewels::unexpected(LogError::not_open));
        REQUIRE(reader.zero_copy_out(0U, 1U) == jewels::unexpected(LogError::not_open));
        REQUIRE(reader.advance(1U) == jewels::unexpected(LogError::not_open));
        REQUIRE(reader.advance(std::as_bytes(std::span{read_data})) == jewels::unexpected(LogError::not_open));
        REQUIRE(reader.skip_pad_bytes() == jewels::unexpected(LogError::not_open));
        REQUIRE(reader.get_window_offset() == 0U);
        REQUIRE(reader.get_bytes_remaining() == 0U);
      }
    }

    SECTION("Open fails in get file size")
    {
      constexpr size_t read_size = 1U;
      std::array<std::byte, read_size> read_data{};
      reader.get_filesystem().inject_stat_error(EBADMSG);
      REQUIRE(reader.open(test_file_path.string()) == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(reader.copy_out(0U, {read_data}) == jewels::unexpected(LogError::not_open));
      REQUIRE(reader.zero_copy_out(0U, 1U) == jewels::unexpected(LogError::not_open));
      REQUIRE(reader.advance(1U) == jewels::unexpected(LogError::not_open));
      REQUIRE(reader.advance(std::as_bytes(std::span{read_data})) == jewels::unexpected(LogError::not_open));
      REQUIRE(reader.skip_pad_bytes() == jewels::unexpected(LogError::not_open));
      REQUIRE(reader.get_window_offset() == 0U);
      REQUIRE(reader.get_bytes_remaining() == 0U);
    }

    SECTION("Read fails in copy_out")
    {
      constexpr size_t read_size = 127U;
      std::array<std::byte, read_size> read_data{};
      const auto skip_count = GENERATE(0U, 1U, 2U, 3U);
      CAPTURE(skip_count);
      reader.get_filesystem().inject_read_error(EBADMSG, skip_count);
      REQUIRE(reader.open(test_file_path.string()));
      REQUIRE(reader.copy_out(0U, {read_data}) == jewels::unexpected(LogError::unspecified_system_error));

      REQUIRE(reader.zero_copy_out(0U, 1U) == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(reader.advance(1U) == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(
        reader.advance(std::as_bytes(std::span{read_data})) == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(reader.skip_pad_bytes() == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(reader.get_window_offset() == 0U);
      REQUIRE(reader.get_bytes_remaining() == 0U);
    }

    SECTION("Read fails in zero_copy_out")
    {
      constexpr size_t read_size = 127U;
      std::array<std::byte, read_size> read_data{};
      const auto skip_count = GENERATE(0U, 1U, 2U, 3U);
      CAPTURE(skip_count);
      reader.get_filesystem().inject_read_error(EBADMSG, skip_count);
      REQUIRE(reader.open(test_file_path.string()));
      REQUIRE(reader.zero_copy_out(0U, read_size) == jewels::unexpected(LogError::unspecified_system_error));

      REQUIRE(reader.copy_out(0U, {read_data}) == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(reader.advance(1U) == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(
        reader.advance(std::as_bytes(std::span{read_data})) == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(reader.skip_pad_bytes() == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(reader.get_window_offset() == 0U);
      REQUIRE(reader.get_bytes_remaining() == 0U);
    }

    SECTION("Try to read more than max read size")
    {
      std::array<std::byte, TestReaderPolicy::max_read_size + 1U> read_data{};
      REQUIRE(reader.open(test_file_path.string()));
      REQUIRE(reader.zero_copy_out(0U, TestReaderPolicy::max_read_size + 1U) == jewels::unexpected(LogError::failed));

      REQUIRE(reader.copy_out(0U, {read_data}) == jewels::unexpected(LogError::failed));
      REQUIRE(reader.advance(1U) == jewels::unexpected(LogError::failed));
      REQUIRE(reader.advance(std::as_bytes(std::span{read_data})) == jewels::unexpected(LogError::failed));
      REQUIRE(reader.skip_pad_bytes() == jewels::unexpected(LogError::failed));
      REQUIRE(reader.get_window_offset() == 0U);
      REQUIRE(reader.get_bytes_remaining() == 0U);
    }

    SECTION("Try to advance past the end of the window")
    {
      constexpr size_t read_size = 1U;
      std::array<std::byte, read_size> read_data{};
      const auto skip_count = GENERATE(0U, 1U, 2U, 3U);
      CAPTURE(skip_count);
      reader.get_filesystem().inject_read_error(EBADMSG, skip_count);
      REQUIRE(reader.open(test_file_path.string()));
      constexpr size_t advance_size = 127U;
      REQUIRE(reader.advance(advance_size) == jewels::unexpected(LogError::failed));

      REQUIRE(reader.copy_out(0U, {read_data}) == jewels::unexpected(LogError::failed));
      REQUIRE(reader.zero_copy_out(0U, read_size) == jewels::unexpected(LogError::failed));
      REQUIRE(reader.advance(std::as_bytes(std::span{read_data})) == jewels::unexpected(LogError::failed));
      REQUIRE(reader.skip_pad_bytes() == jewels::unexpected(LogError::failed));
      REQUIRE(reader.get_window_offset() == 0U);
      REQUIRE(reader.get_bytes_remaining() == 0U);
    }

    SECTION("Read fails in skip pad bytes")
    {
      std::array<std::byte, write_data_size> test_write_data{};
      ofs.open(test_file_path.c_str(), std::ios::trunc);
      ofs.write(nolint_helper::char_ptr_to_span_element(test_write_data), test_write_data.size());
      REQUIRE(ofs);
      ofs.close();

      constexpr size_t read_size = 1U;
      std::array<std::byte, read_size> read_data{};
      const auto skip_count = GENERATE(0U, 1U, 2U, 3U);
      CAPTURE(skip_count);
      reader.get_filesystem().inject_read_error(EBADMSG, skip_count);
      REQUIRE(reader.open(test_file_path.string()));
      REQUIRE(reader.skip_pad_bytes() == jewels::unexpected(LogError::unspecified_system_error));

      REQUIRE(reader.advance(1U) == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(reader.zero_copy_out(0U, read_size) == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(reader.copy_out(0U, {read_data}) == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(
        reader.advance(std::as_bytes(std::span{read_data})) == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(reader.get_window_offset() == 0U);
      REQUIRE(reader.get_bytes_remaining() == 0U);
    }

    SECTION("Read fails in advance to pattern")
    {
      constexpr std::array pattern = {'_', 'A', 'B', 'C'};
      std::array<std::byte, write_data_size> test_write_data{};
      ofs.open(test_file_path.c_str(), std::ios::trunc);
      ofs.write(nolint_helper::char_ptr_to_span_element(test_write_data), test_write_data.size());
      REQUIRE(ofs);
      ofs.close();

      constexpr size_t read_size = 1U;
      std::array<std::byte, read_size> read_data{};
      const auto skip_count = GENERATE(0U, 1U, 2U, 3U);
      CAPTURE(skip_count);
      reader.get_filesystem().inject_read_error(EBADMSG, skip_count);
      REQUIRE(reader.open(test_file_path.string()));
      REQUIRE(
        reader.advance(std::as_bytes(std::span{pattern})) == jewels::unexpected(LogError::unspecified_system_error));

      REQUIRE(reader.advance(1U) == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(reader.copy_out(0U, {read_data}) == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(reader.zero_copy_out(0U, read_size) == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(reader.skip_pad_bytes() == jewels::unexpected(LogError::unspecified_system_error));
      REQUIRE(reader.get_window_offset() == 0U);
      REQUIRE(reader.get_bytes_remaining() == 0U);
    }

    SECTION("Invalid pattern in advance to pattern")
    {
      const auto pattern =
        GENERATE(std::string{}, std::string(TestReaderPolicy::max_read_size + 1U, 'X'), std::string("_AB_"));
      CAPTURE(pattern);

      constexpr size_t read_size = 1U;
      std::array<std::byte, read_size> read_data{};
      REQUIRE(reader.open(test_file_path.string()));
      REQUIRE(reader.advance(std::as_bytes(std::span{pattern})) == jewels::unexpected(LogError::failed));

      REQUIRE(reader.copy_out(0U, {read_data}) == jewels::unexpected(LogError::failed));
      REQUIRE(reader.zero_copy_out(0U, read_size) == jewels::unexpected(LogError::failed));
      REQUIRE(reader.advance(1U) == jewels::unexpected(LogError::failed));
      REQUIRE(reader.skip_pad_bytes() == jewels::unexpected(LogError::failed));
      REQUIRE(reader.get_window_offset() == 0U);
      REQUIRE(reader.get_bytes_remaining() == 0U);
    }
  }
}

} // namespace
} // namespace clockwork_logging::onboard
