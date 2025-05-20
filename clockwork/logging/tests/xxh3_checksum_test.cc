// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/xxh3_checksum.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <type_traits>

namespace clockwork_logging
{
namespace
{

TEST_CASE("XXH3 Checksum")
{
  SECTION("Empty message")
  {
    SECTION("compute_xxh3_checksum")
    {
      REQUIRE(compute_xxh3_checksum(std::span<const std::byte>{}) == 0x2D06800538D394C2U);
      const std::array data_spans = {std::span<const std::byte>{}, std::span<const std::byte>{}};
      REQUIRE(compute_xxh3_checksum(data_spans) == 0x2D06800538D394C2U);
    }

    SECTION("update_checksum spans")
    {
      auto state = init_xxh3_checksum();
      const std::array data_spans = {std::span<const std::byte>{}, std::span<const std::byte>{}};
      for (const auto data : data_spans)
      {
        update_xxh3_checksum(state, data);
      }
      REQUIRE(digest_xxh3_checksum(state) == 0x2D06800538D394C2U);
    }

    SECTION("update_checksum span of spans")
    {
      auto state = init_xxh3_checksum();
      const std::array data_spans = {std::span<const std::byte>{}, std::span<const std::byte>{}};
      update_xxh3_checksum(state, {data_spans});
      REQUIRE(digest_xxh3_checksum(state) == 0x2D06800538D394C2U);
    }
  }

  SECTION("Message in pieces")
  {
    std::array data = {'a', 'b', 'c', 'd', 'e', 'f'};
    REQUIRE(compute_xxh3_checksum(std::as_bytes(std::span{data})) == 0xDA87BD32D3C47DB6U);
    std::array<std::span<const std::byte>, 6U> data_spans = {
      std::as_bytes(std::span{&data.at(0U), 1U}),
      std::as_bytes(std::span{&data.at(1U), 1U}),
      std::as_bytes(std::span{&data.at(2U), 1U}),
      std::as_bytes(std::span{&data.at(3U), 1U}),
      std::as_bytes(std::span{&data.at(4U), 1U}),
      std::as_bytes(std::span{&data.at(5U), 1U})};

    SECTION("compute_checksum")
    {
      REQUIRE(compute_xxh3_checksum({data_spans}) == 0xDA87BD32D3C47DB6U);
    }

    SECTION("update_checksum spans")
    {
      auto state = init_xxh3_checksum();
      for (const auto data_span : data_spans)
      {
        update_xxh3_checksum(state, data_span);
      }
      REQUIRE(digest_xxh3_checksum(state) == 0xDA87BD32D3C47DB6U);
    }

    SECTION("update_checksum span of spans")
    {
      auto state = init_xxh3_checksum();
      update_xxh3_checksum(state, {data_spans});
      REQUIRE(digest_xxh3_checksum(state) == 0xDA87BD32D3C47DB6U);
    }
  }
}

} // namespace
} // namespace clockwork_logging
