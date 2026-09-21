// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace jewels
{

template <bool is_noexcept_destructible>
struct NotTriviallyDestructible
{
  NotTriviallyDestructible() noexcept = default;
  NotTriviallyDestructible(const NotTriviallyDestructible&) noexcept = default;
  NotTriviallyDestructible(NotTriviallyDestructible&&) noexcept = default;
  NotTriviallyDestructible& operator=(const NotTriviallyDestructible&) noexcept = default;
  NotTriviallyDestructible& operator=(NotTriviallyDestructible&&) noexcept = default;

  /// Explicitly trying to test non-trivial destructor.  Cannot use `=
  /// default` because then `NotTriviallyDestructible<true>` would in
  /// fact be trivially destructible.
  // NOLINTNEXTLINE(modernize-use-equals-default,performance-noexcept-destructor)
  ~NotTriviallyDestructible() noexcept(is_noexcept_destructible) {}
};

static_assert(!std::is_trivially_destructible_v<NotTriviallyDestructible<true>>);
static_assert(!std::is_trivially_destructible_v<NotTriviallyDestructible<false>>);

struct TriviallyDestructible
{
};

static_assert(std::is_trivially_destructible_v<TriviallyDestructible>);

TEST_CASE("Destructor - noexcept qualification")
{
  SECTION("Both not trivially destructible")
  {
    STATIC_REQUIRE(
      std::is_nothrow_destructible_v<expected<NotTriviallyDestructible<true>, NotTriviallyDestructible<true>>>);
    STATIC_REQUIRE(
      !std::is_nothrow_destructible_v<expected<NotTriviallyDestructible<true>, NotTriviallyDestructible<false>>>);
    STATIC_REQUIRE(
      !std::is_nothrow_destructible_v<expected<NotTriviallyDestructible<false>, NotTriviallyDestructible<true>>>);
    STATIC_REQUIRE(
      !std::is_nothrow_destructible_v<expected<NotTriviallyDestructible<false>, NotTriviallyDestructible<false>>>);
  }
  SECTION("Both are trivially destructible")
  {
    STATIC_REQUIRE(std::is_nothrow_destructible_v<expected<TriviallyDestructible, TriviallyDestructible>>);
  }
  SECTION("Value is trivially destructible")
  {
    STATIC_REQUIRE(std::is_nothrow_destructible_v<expected<TriviallyDestructible, NotTriviallyDestructible<true>>>);
    STATIC_REQUIRE(!std::is_nothrow_destructible_v<expected<TriviallyDestructible, NotTriviallyDestructible<false>>>);
  }
  SECTION("Error is trivially destructible")
  {
    STATIC_REQUIRE(std::is_nothrow_destructible_v<expected<NotTriviallyDestructible<true>, TriviallyDestructible>>);
    STATIC_REQUIRE(!std::is_nothrow_destructible_v<expected<NotTriviallyDestructible<false>, TriviallyDestructible>>);
  }
  SECTION("Value is void and error is trivially destructible")
  {
    STATIC_REQUIRE(std::is_nothrow_destructible_v<expected<void, TriviallyDestructible>>);
  }
  SECTION("Value is void and error is not trivially destructible")
  {
    STATIC_REQUIRE(std::is_nothrow_destructible_v<expected<void, NotTriviallyDestructible<true>>>);
    STATIC_REQUIRE(!std::is_nothrow_destructible_v<expected<void, NotTriviallyDestructible<false>>>);
  }
}

} // namespace jewels
