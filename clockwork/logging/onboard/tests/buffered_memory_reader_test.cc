// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/onboard/buffered_memory_reader.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "jewels/container/at.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <ranges>
#include <span>
#include <sys/types.h>
#include <type_traits>
#include <vector>

namespace clockwork_logging::onboard
{
namespace
{

TEST_CASE("BufferedMemoryReader")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;

  constexpr size_t write_data_size = 1011U;
  std::array<std::byte, write_data_size> write_data{};
  tests::fill_with_random_bytes({write_data});

  BufferedMemoryReader reader{memory_resource};

  SECTION("Read with copy_out")
  {
    const size_t read_size = GENERATE(1U, 17U, 127U, 128U);
    CAPTURE(read_size);
    std::vector<std::byte> read_data(read_size);

    REQUIRE(reader.open(write_data));

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
    REQUIRE_FALSE(reader);
    reader.close();
  }

  SECTION("Read with zero_copy_out and offset")
  {
    constexpr size_t read_size = 127U;
    const size_t offset = GENERATE(1U, 37U, 67U);
    CAPTURE(offset);

    REQUIRE(reader.open(write_data));

    size_t read_offset = 0U;
    while (read_offset + offset < write_data_size)
    {
      CAPTURE(read_size, read_offset);
      REQUIRE(reader);
      const auto bytes_to_read = std::min(read_size - offset, write_data_size - read_offset - offset);
      const auto copy_result = reader.zero_copy_out(offset, bytes_to_read);
      REQUIRE(copy_result);
      REQUIRE(std::ranges::equal(
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

    REQUIRE(reader.open(write_data));

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
      REQUIRE(std::ranges::equal(
        copy_result->at(0U) | std::views::join, std::span{&write_data.at(read_offset), header_bytes_to_read}));
      REQUIRE(std::ranges::equal(
        copy_result->at(1U) | std::views::join,
        std::span{&write_data.at(read_offset + header_bytes_to_read), data_bytes_to_read}));
      REQUIRE(std::ranges::equal(
        copy_result->at(2U) | std::views::join,
        std::span{&write_data.at(read_offset + header_bytes_to_read + data_bytes_to_read), checksum_bytes_to_read}));
      const auto total_bytes_to_read = header_bytes_to_read + data_bytes_to_read + checksum_bytes_to_read;
      REQUIRE(reader.advance(total_bytes_to_read));
      read_offset += total_bytes_to_read;
    }
    REQUIRE_FALSE(reader);
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

    REQUIRE(reader.open(pattern_write_data));

    for (const auto pattern_offset : pattern_offsets)
    {
      REQUIRE(reader.advance(std::as_bytes(std::span{pattern})));
      REQUIRE(reader.get_current_offset() == pattern_offset);
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

    REQUIRE(reader.open(test_write_data));

    for (const auto non_zero_offset : non_zero_offsets)
    {
      REQUIRE(reader.skip_pad_bytes());
      REQUIRE(reader.get_current_offset() == non_zero_offset);
      REQUIRE(reader.advance(1U));
    }
    REQUIRE(reader.skip_pad_bytes());
    REQUIRE_FALSE(reader);
  }
}

} // namespace
} // namespace clockwork_logging::onboard
