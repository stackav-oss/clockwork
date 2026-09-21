// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/container/tap/tap_to_protobuf.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/conversions.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>
#include <gsl/util>

#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
namespace jewels::test
{

namespace
{

// Tag for testing UUID
struct UuidTag
{
};

} // namespace

TEST_CASE("tap_to_protobuf: UUID conversion", "[tap_to_protobuf]")
{
  SECTION("Valid UUID")
  {
    // Use a known valid UUID string
    const std::string uuid_str = "123e4567-e89b-12d3-a456-426614174000";
    auto uuid = Uuid<UuidTag>::from_string(uuid_str).value();

    std::string output;
    tap_to_protobuf(output, uuid);
    CHECK(output == uuid_str);
  }
}

TEST_CASE("tap_to_protobuf: VarString conversion", "[tap_to_protobuf]")
{
  SECTION("Successful conversion")
  {
    constexpr size_t capacity = 50;
    const std::string test_str = "This is a test string";

    tap::VarString<capacity> input;
    REQUIRE(input.try_set(test_str));

    std::string output;
    tap_to_protobuf(output, input);
    CHECK(output == test_str);
  }

  SECTION("Empty string")
  {
    constexpr size_t capacity = 10;
    const tap::VarString<capacity> input;

    std::string output = "should be cleared";
    tap_to_protobuf(output, input);
    CHECK(output.empty());
  }
}

TEST_CASE("tap_to_protobuf: SyncTime to Timestamp conversion", "[tap_to_protobuf]")
{
  using namespace std::chrono_literals;

  SECTION("Convert positive time")
  {
    const auto input_time = time::sync_time_from_ns(123456789012345);
    google::protobuf::Timestamp output;

    tap_to_protobuf(output, input_time);
    CHECK(output.seconds() == 123456);
    CHECK(output.nanos() == 789012345);
  }

  SECTION("Convert zero time")
  {
    const auto input_time = time::sync_time_from_ns(0);
    google::protobuf::Timestamp output;

    tap_to_protobuf(output, input_time);
    CHECK(output.seconds() == 0);
    CHECK(output.nanos() == 0);
  }
}

TEST_CASE("tap_to_protobuf: Duration conversion", "[tap_to_protobuf]")
{
  using namespace std::chrono_literals;

  SECTION("Convert positive duration")
  {
    const auto input = 123456789012345ns;
    google::protobuf::Duration output;

    tap_to_protobuf(output, input);
    CHECK(output.seconds() == 123456);
    CHECK(output.nanos() == 789012345);
  }

  SECTION("Convert zero duration")
  {
    const auto input = 0ns;
    google::protobuf::Duration output;

    tap_to_protobuf(output, input);
    CHECK(output.seconds() == 0);
    CHECK(output.nanos() == 0);
  }
}

TEST_CASE("tap_to_protobuf: Byte conversion", "[tap_to_protobuf]")
{
  SECTION("Single byte")
  {
    const std::byte input{65}; // ASCII 'A'
    std::string output{};

    tap_to_protobuf(output, input);
    CHECK(output == "A");
    CHECK(output.size() == 1);
  }

  SECTION("Zero byte")
  {
    const std::byte input{0};
    std::string output;

    tap_to_protobuf(output, input);
    CHECK(output.size() == 1);
    CHECK(output[0] == '\0');
  }
}

TEST_CASE("tap_to_protobuf: VarArray<byte> conversion", "[tap_to_protobuf]")
{
  SECTION("Non-empty array")
  {
    constexpr size_t capacity = 10;
    tap::VarArray<std::byte, capacity> input;
    input.push_back(std::byte{65}); // 'A'
    input.push_back(std::byte{66}); // 'B'
    input.push_back(std::byte{67}); // 'C'

    std::string output;
    tap_to_protobuf(output, input);
    CHECK(output == "ABC");
    CHECK(output.size() == 3);
  }

  SECTION("Empty array")
  {
    constexpr size_t capacity = 10;
    const tap::VarArray<std::byte, capacity> input;

    std::string output = "should be cleared";
    tap_to_protobuf(output, input);
    CHECK(output.empty());
  }
}

TEST_CASE("tap_to_protobuf: Fixed span of bytes conversion", "[tap_to_protobuf]")
{
  SECTION("Non-empty span")
  {
    std::array<std::byte, 3> bytes_array{std::byte{65}, std::byte{66}, std::byte{67}}; // ABC
    auto input = std::span<const std::byte>{bytes_array};

    std::string output;
    tap_to_protobuf(output, input);
    CHECK(output == "ABC");
    CHECK(output.size() == 3);
  }

  SECTION("Empty span")
  {
    std::array<std::byte, 0> bytes_array{};
    auto input = std::span<const std::byte>{bytes_array};

    std::string output = "should be cleared";
    tap_to_protobuf(output, input);
    CHECK(output.empty());
  }
}

TEST_CASE("tap_to_protobuf: string_view conversion", "[tap_to_protobuf]")
{
  SECTION("Non-empty string_view")
  {
    const std::string_view input_sv = "Hello, string_view!";
    std::string output;

    tap_to_protobuf(output, input_sv);

    CHECK(output == input_sv);
  }

  SECTION("Empty string_view")
  {
    const std::string_view input_sv{};
    std::string output = "should be cleared";

    tap_to_protobuf(output, input_sv);

    CHECK(output.empty());
  }
}
} // namespace jewels::test
