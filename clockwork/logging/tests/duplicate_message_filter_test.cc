// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/duplicate_message_filter.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory_resource>

namespace clockwork_logging
{
namespace
{

TEST_CASE("duplicate message filter, times are different, sequence number is zero")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  constexpr uint64_t sequence_number0 = 0U;
  constexpr size_t message_filter_size = 4U;
  constexpr auto message_filter_expiration_interval = std::chrono::seconds(1);
  DuplicateMessageFilter message_filter{memory_resource, message_filter_size, message_filter_expiration_interval};

  REQUIRE_FALSE(message_filter.try_get_min_timestamp());
  REQUIRE_FALSE(message_filter.try_get_max_timestamp());

  constexpr auto channel_str1 = "1";
  constexpr auto channel_str2 = "2";

  constexpr auto time1 = LogTimestamp(std::chrono::seconds(1));
  constexpr auto time2 = LogTimestamp(std::chrono::seconds(2));
  REQUIRE_FALSE(message_filter.is_duplicate(channel_str1, time1, sequence_number0));
  REQUIRE_FALSE(message_filter.is_duplicate(channel_str2, time1, sequence_number0));

  SECTION("Duplicate messages are filtered")
  {
    REQUIRE(message_filter.is_duplicate(channel_str1, time1, sequence_number0));
    REQUIRE(message_filter.is_duplicate(channel_str2, time1, sequence_number0));
  }

  SECTION("Messages removed from filter when expiration time is reached")
  {
    // Time interval [1,1]
    auto min_timestamp = message_filter.try_get_min_timestamp();
    auto max_timestamp = message_filter.try_get_max_timestamp();
    REQUIRE(min_timestamp);
    REQUIRE(time1 == *min_timestamp);
    REQUIRE(max_timestamp);
    REQUIRE(time1 == *max_timestamp);

    constexpr auto time1p5 = LogTimestamp(std::chrono::milliseconds(1'500));
    REQUIRE_FALSE(message_filter.is_duplicate(channel_str1, time1p5, sequence_number0));
    REQUIRE_FALSE(message_filter.is_duplicate(channel_str2, time1p5, sequence_number0));

    // Time interval [1,1.5]
    min_timestamp = message_filter.try_get_min_timestamp();
    max_timestamp = message_filter.try_get_max_timestamp();
    REQUIRE(min_timestamp);
    REQUIRE(time1 == *min_timestamp);
    REQUIRE(max_timestamp);
    REQUIRE(time1p5 == *max_timestamp);

    REQUIRE(message_filter.is_duplicate(channel_str1, time1p5, sequence_number0));
    REQUIRE(message_filter.is_duplicate(channel_str2, time1p5, sequence_number0));

    // Time interval [1,1.5]
    min_timestamp = message_filter.try_get_min_timestamp();
    max_timestamp = message_filter.try_get_max_timestamp();
    REQUIRE(min_timestamp);
    REQUIRE(time1 == *min_timestamp);
    REQUIRE(max_timestamp);
    REQUIRE(time1p5 == *max_timestamp);

    constexpr auto time2p5 = time2 + std::chrono::milliseconds(100);
    REQUIRE_FALSE(message_filter.is_duplicate(channel_str1, time2p5, sequence_number0));
    REQUIRE_FALSE(message_filter.is_duplicate(channel_str2, time2p5, sequence_number0));

    // Time interval [1.5,2.1]
    min_timestamp = message_filter.try_get_min_timestamp();
    max_timestamp = message_filter.try_get_max_timestamp();
    REQUIRE(min_timestamp);
    REQUIRE(time1p5 == *min_timestamp);
    REQUIRE(max_timestamp);
    REQUIRE(time2p5 == *max_timestamp);
  }
}

TEST_CASE("duplicate message filter, sequence numbers are different, all times are zero")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  constexpr auto time0 = LogTimestamp(std::chrono::nanoseconds(0));
  constexpr auto time1 = LogTimestamp(std::chrono::nanoseconds(1));
  constexpr auto time2 = LogTimestamp(std::chrono::nanoseconds(2));
  constexpr auto time3 = LogTimestamp(std::chrono::nanoseconds(3));
  constexpr size_t message_filter_size = 4U;
  constexpr auto message_filter_expiration_interval = std::chrono::seconds(1);
  DuplicateMessageFilter message_filter{memory_resource, message_filter_size, message_filter_expiration_interval};

  REQUIRE_FALSE(message_filter.try_get_min_timestamp());
  REQUIRE_FALSE(message_filter.try_get_max_timestamp());

  constexpr auto channel_str1 = "1";
  constexpr auto channel_str2 = "2";

  constexpr auto sequence_number1 = 1U;
  REQUIRE_FALSE(message_filter.is_duplicate(channel_str1, time0, sequence_number1));
  REQUIRE_FALSE(message_filter.is_duplicate(channel_str2, time0, sequence_number1));

  SECTION("Duplicate messages are filtered")
  {
    REQUIRE(message_filter.is_duplicate(channel_str1, time0, sequence_number1));
    REQUIRE(message_filter.is_duplicate(channel_str2, time0, sequence_number1));
  }

  SECTION("Messages removed from filter when maximum size is reached")
  {
    REQUIRE_FALSE(message_filter.is_duplicate(channel_str1, time1, sequence_number1));
    REQUIRE_FALSE(message_filter.is_duplicate(channel_str1, time2, sequence_number1));
    REQUIRE_FALSE(message_filter.is_duplicate(channel_str1, time3, sequence_number1));
    REQUIRE_FALSE(message_filter.is_duplicate(channel_str1, time0, sequence_number1));
  }
}

} // namespace
} // namespace clockwork_logging
