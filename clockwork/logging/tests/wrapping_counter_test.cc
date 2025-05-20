// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/wrapping_counter.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace clockwork_logging
{
namespace
{

TEMPLATE_TEST_CASE("WrappingCounter", "", int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t)
{
  using CounterType = WrappingCounter<TestType>;

  STATIC_REQUIRE(CounterType{}.value() == 0);
  STATIC_REQUIRE(CounterType{100}.value() == 100);

  STATIC_REQUIRE(CounterType{1} == CounterType{1});
  STATIC_REQUIRE(CounterType{0} != CounterType{1});

  STATIC_REQUIRE(
    CounterType{CounterType::max_value - CounterType::wrap_window} <
    CounterType{CounterType::min_value + CounterType::wrap_window - 1});
  STATIC_REQUIRE_FALSE(
    CounterType{CounterType::max_value - CounterType::wrap_window} <
    CounterType{CounterType::min_value + CounterType::wrap_window});
  STATIC_REQUIRE_FALSE(
    CounterType{CounterType::max_value - CounterType::wrap_window - 1} <
    CounterType{CounterType::min_value + CounterType::wrap_window - 1});

  STATIC_REQUIRE_FALSE(
    CounterType{CounterType::min_value + CounterType::wrap_window - 1} <
    CounterType{CounterType::max_value - CounterType::wrap_window});
  STATIC_REQUIRE(
    CounterType{CounterType::min_value + CounterType::wrap_window} <
    CounterType{CounterType::max_value - CounterType::wrap_window});
  STATIC_REQUIRE(
    CounterType{CounterType::min_value + CounterType::wrap_window - 1} <
    CounterType{CounterType::max_value - CounterType::wrap_window - 1});
}

} // namespace
} // namespace clockwork_logging
