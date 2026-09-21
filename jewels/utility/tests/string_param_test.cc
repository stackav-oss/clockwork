// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/utility/string_param.hh"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

namespace jewels
{
namespace
{

template <StringParam str>
class TestClass
{
public:
  [[nodiscard]] static constexpr std::string_view string_view();
};

template <StringParam str>
[[nodiscard]] constexpr std::string_view TestClass<str>::string_view()
{
  return str;
}

TEST_CASE("string parameter")
{
  STATIC_REQUIRE(StringParam{"Test123"}.operator std::string_view() == "Test123");
  STATIC_REQUIRE(TestClass<"Test123">::string_view() == "Test123");
}

} // namespace
} // namespace jewels
