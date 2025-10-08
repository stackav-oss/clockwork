// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/container/at.hh" // IWYU pragma: keep
#include "jewels/container/tap/protobuf_to_tap.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace jewels
{
struct TestTag
{
};

TEST_CASE("Test Uuid Conversion")
{
  jewels::Uuid<TestTag> output{};
  auto status = protobuf_to_tap(output, "some-junk");
  CHECK_FALSE(status);

  auto good_uuid = jewels::Uuid<TestTag>::random_uuid();
  auto good_status = protobuf_to_tap(output, good_uuid.to_string());
  CHECK(good_status);
  CHECK(good_uuid == output);
}

TEST_CASE("Test string conversion")
{
  tap::VarString<2> too_small;
  const std::string test_string = "Test string";

  auto error_status = protobuf_to_tap(too_small, test_string);
  CHECK_FALSE(error_status);

  tap::VarString<100> big_enough;
  auto ok_status = protobuf_to_tap(big_enough, test_string);

  CHECK(ok_status);
  CHECK(big_enough.string_view() == test_string);
}

TEST_CASE("Test byte conversion")
{
  std::byte output{};
  const std::string too_big = "Bigger than a byte";

  auto error_status = protobuf_to_tap(output, too_big);
  CHECK_FALSE(error_status);

  const std::string just_right = "b";

  auto ok_status = protobuf_to_tap(output, just_right);
  CHECK(ok_status);

  CHECK(output == static_cast<std::byte>(just_right[0]));
}

TEST_CASE("Test byte array")
{
  tap::VarArray<std::byte, 3> too_small;
  const std::string test_string = "test_string";

  auto error_status = protobuf_to_tap(too_small, test_string);
  CHECK_FALSE(error_status);

  tap::VarArray<std::byte, 100> big_enough;

  auto ok_status = protobuf_to_tap(big_enough, test_string);
  CHECK(ok_status);

  for (size_t i = 0; i < test_string.size(); ++i)
  {
    CHECK(big_enough[i] == static_cast<std::byte>(test_string[i]));
  }
}

TEST_CASE("Test byte array with a span")
{
  std::array<std::byte, 3> too_small_array{};
  auto too_small_span = std::span<std::byte, 3>{too_small_array};
  const std::string test_string = "test_string";

  auto error_status = protobuf_to_tap(too_small_span, test_string);
  CHECK_FALSE(error_status);

  std::array<std::byte, 100> big_enough_array{};
  auto big_enough_span = std::span<std::byte, 100>{big_enough_array};

  auto ok_status = protobuf_to_tap(big_enough_span, test_string);
  CHECK(ok_status);

  for (size_t i = 0; i < test_string.size(); ++i)
  {
    CHECK(big_enough_span[i] == static_cast<std::byte>(test_string[i]));
  }
}

TEST_CASE("Test int conversion")
{
  uint32_t u32o = 0;
  int32_t i32o = 0;
  uint16_t u16o = 0;
  int16_t i16o = 0;
  uint8_t u8o = 0;
  int8_t i8o = 0;

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage) Test-only, using a macro to preserve line number information
#define CONV_PASS(var, value) \
  CHECK(protobuf_to_tap(var, value)); \
  CHECK((var) == (value));
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage) Test-only, consistent with test API
#define CONV_FAIL(var, value) CHECK(!protobuf_to_tap(var, value));

  constexpr auto i16_max = std::numeric_limits<int16_t>::max();
  CONV_FAIL(i8o, i16_max);
  CONV_FAIL(u8o, i16_max);
  CONV_PASS(i16o, i16_max);
  CONV_PASS(u16o, i16_max);
  CONV_PASS(i32o, i16_max);
  CONV_PASS(u32o, i16_max);

  constexpr auto i16_min = std::numeric_limits<int16_t>::lowest();
  CONV_FAIL(i8o, i16_min);
  CONV_FAIL(u8o, i16_min);
  CONV_PASS(i16o, i16_min);
  CONV_FAIL(u16o, i16_min);
  CONV_PASS(i32o, i16_min);
  CONV_FAIL(u32o, i16_min);

  constexpr auto u16_max = std::numeric_limits<uint16_t>::max();
  CONV_FAIL(i8o, u16_max);
  CONV_FAIL(u8o, u16_max);
  CONV_FAIL(i16o, u16_max);
  CONV_PASS(u16o, u16_max);
  CONV_PASS(i32o, u16_max);
  CONV_PASS(u32o, u16_max);

  constexpr auto u16_min = std::numeric_limits<uint16_t>::lowest();
  CONV_PASS(i8o, u16_min);
  CONV_PASS(u8o, u16_min);
  CONV_PASS(i16o, u16_min);
  CONV_PASS(u16o, u16_min);
  CONV_PASS(i32o, u16_min);
  CONV_PASS(u32o, u16_min);

#undef CONV_PASS
#undef CONV_FAIL
}

} // namespace jewels
