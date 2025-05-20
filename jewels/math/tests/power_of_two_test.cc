// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/math/power_of_two.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace jewels::math
{

TEMPLATE_TEST_CASE("Power of two", "", uint8_t, uint64_t)
{
  REQUIRE_FALSE(is_power_of_two(TestType{0U}));
  REQUIRE(is_power_of_two(TestType{1U}));
  REQUIRE(is_power_of_two(TestType{2U}));
  REQUIRE_FALSE(is_power_of_two(TestType{3U}));
  REQUIRE(is_power_of_two(TestType{4U}));
}

TEMPLATE_TEST_CASE("Round up to power of two multiple", "", uint8_t, uint64_t)
{
  REQUIRE(round_up_to_power_of_two_multiple<2U>(TestType{0U}) == TestType{0U});
  REQUIRE(round_up_to_power_of_two_multiple<2U>(TestType{1U}) == TestType{2U});
  REQUIRE(round_up_to_power_of_two_multiple<2U>(TestType{2U}) == TestType{2U});
  REQUIRE(round_up_to_power_of_two_multiple<2U>(TestType{3U}) == TestType{4U});
  // Not a power of two, but a multiple.
  REQUIRE(round_up_to_power_of_two_multiple<2U>(TestType{5U}) == TestType{6U});

  REQUIRE(round_up_to_power_of_two_multiple<8U>(TestType{0U}) == TestType{0U});
  REQUIRE(round_up_to_power_of_two_multiple<8U>(TestType{1U}) == TestType{8U});
  REQUIRE(round_up_to_power_of_two_multiple<8U>(TestType{7U}) == TestType{8U});
  REQUIRE(round_up_to_power_of_two_multiple<8U>(TestType{8U}) == TestType{8U});
  REQUIRE(round_up_to_power_of_two_multiple<8U>(TestType{9U}) == TestType{16U});
}

TEST_CASE("Round up to power of two")
{
  SECTION("Manual")
  {
    REQUIRE(round_up_to_power_of_two(0U) == 0U);
    REQUIRE(round_up_to_power_of_two(1U) == 1U);
    REQUIRE(round_up_to_power_of_two(2U) == 2U);
    REQUIRE(round_up_to_power_of_two(3U) == 4U);
    REQUIRE(round_up_to_power_of_two(4U) == 4U);
    REQUIRE(round_up_to_power_of_two(5U) == 8U);
    REQUIRE(round_up_to_power_of_two(6U) == 8U);
    REQUIRE(round_up_to_power_of_two(7U) == 8U);
    REQUIRE(round_up_to_power_of_two(8U) == 8U);
    REQUIRE(round_up_to_power_of_two(9U) == 16U);
    REQUIRE(round_up_to_power_of_two(15U) == 16U);
    REQUIRE(round_up_to_power_of_two(16U) == 16U);
    REQUIRE(round_up_to_power_of_two(17U) == 32U);
    REQUIRE(round_up_to_power_of_two(18U) == 32U);
    REQUIRE(round_up_to_power_of_two(19U) == 32U);
    REQUIRE(round_up_to_power_of_two(20U) == 32U);
    REQUIRE(round_up_to_power_of_two(21U) == 32U);
    REQUIRE(round_up_to_power_of_two(22U) == 32U);
    REQUIRE(round_up_to_power_of_two(23U) == 32U);
    REQUIRE(round_up_to_power_of_two(24U) == 32U);
    REQUIRE(round_up_to_power_of_two(25U) == 32U);
    REQUIRE(round_up_to_power_of_two(26U) == 32U);
    REQUIRE(round_up_to_power_of_two(27U) == 32U);
    REQUIRE(round_up_to_power_of_two(28U) == 32U);
    REQUIRE(round_up_to_power_of_two(29U) == 32U);
    REQUIRE(round_up_to_power_of_two(30U) == 32U);
    REQUIRE(round_up_to_power_of_two(31U) == 32U);
    REQUIRE(round_up_to_power_of_two(32U) == 32U);
    REQUIRE(round_up_to_power_of_two(33U) == 64U);
  }
  SECTION("Thorough")
  {
    for (auto pos = 2UL; pos < 64UL; ++pos)
    {
      const auto value = 1UL << pos;
      const auto next_value = value << 1UL;
      REQUIRE(round_up_to_power_of_two(value - 1UL) == value);
      REQUIRE(round_up_to_power_of_two(value) == value);

      if (next_value != 0UL)
      {
        constexpr auto max_steps = 100UL;
        const auto step_size{std::max((next_value - value) / max_steps, 1UL)};
        for (auto intermediate = value + 1UL; intermediate < next_value; intermediate += step_size)
        {
          REQUIRE(round_up_to_power_of_two(intermediate) == next_value);
        }
      }
    }

    REQUIRE(round_up_to_power_of_two(std::numeric_limits<size_t>::max()) == 0U);
  }
}

} // namespace jewels::math
