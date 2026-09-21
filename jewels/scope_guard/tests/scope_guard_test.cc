// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/scope_guard/scope_guard.hh"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace jewels
{
namespace
{
/// Test callable with a noexcept move constructor for testing noexcept classifications with ScopeGuard
struct NothrowMovableCallable
{
  NothrowMovableCallable() = default;
  NothrowMovableCallable(NothrowMovableCallable&& rhs) noexcept = default;
  NothrowMovableCallable(const NothrowMovableCallable& rhs) = delete;
  NothrowMovableCallable& operator=(const NothrowMovableCallable& rhs) = delete;
  NothrowMovableCallable& operator=(const NothrowMovableCallable&& rhs) = delete;
  ~NothrowMovableCallable() = default;

  void operator()() noexcept {}
};

/// Test callable with a throwing move constructor for testing noexecpt classifications.
struct ThrowingMovableCallable
{
  ThrowingMovableCallable() = default;
  ThrowingMovableCallable(ThrowingMovableCallable&& /*rhs*/) noexcept(false) {}
  ThrowingMovableCallable(const ThrowingMovableCallable& rhs) = delete;
  ThrowingMovableCallable& operator=(const ThrowingMovableCallable& rhs) = delete;
  ThrowingMovableCallable& operator=(const ThrowingMovableCallable&& rhs) = delete;
  ~ThrowingMovableCallable() = default;

  void operator()() noexcept {}
};
} // namespace

TEST_CASE("ScopeGuard invokes callback when scope exits")
{
  int value = 7;

  {
    const ScopeGuard test_scope_guard([&value]() { value = 12; });

    CHECK(value == 7);
  }

  CHECK(value == 12);
}

TEST_CASE("ScopeGuard dismiss disables callback")
{
  int call_count = 0;

  {
    ScopeGuard test_scope_guard([&call_count]() { ++call_count; });
    test_scope_guard.dismiss();
  }

  CHECK(call_count == 0);
}

TEST_CASE("ScopeGuard move constructor transfers callback responsibility")
{
  int call_count = 0;

  {
    ScopeGuard original([&call_count]() { ++call_count; });
    {
      ScopeGuard moved(std::move(original));
      CHECK(call_count == 0);
    }

    // The moved-from guard is dismissed by move construction and does not fire.
    CHECK(call_count == 1);
  }

  CHECK(call_count == 1);
}

TEST_CASE("ScopeGuard callbacks run in reverse declaration order")
{
  std::string call_order;

  {
    const ScopeGuard first([&call_order]() { call_order += "A"; });
    const ScopeGuard second([&call_order]() { call_order += "B"; });

    CHECK(call_order.empty());
  }

  CHECK(call_order == "BA");
}

TEST_CASE("ScopeGuard in nested scopes runs at each scope boundary")
{
  int call_count = 0;

  {
    const ScopeGuard outer([&call_count]() { call_count += 10; });
    {
      const ScopeGuard inner([&call_count]() { ++call_count; });
      CHECK(call_count == 0);
    }

    CHECK(call_count == 1);
  }

  CHECK(call_count == 11);
}

TEST_CASE("ScopeGuard supports move-only captures")
{
  int observed_value = 0;

  {
    auto moved_capture = std::make_unique<int>(42);
    const ScopeGuard test_scope_guard([capture = std::move(moved_capture), &observed_value]()
                                      { observed_value = *capture; });

    CHECK(moved_capture == nullptr);
    CHECK(observed_value == 0);
  }

  CHECK(observed_value == 42);
}

TEST_CASE("ScopeGuard runs on early return")
{
  int call_count = 0;

  auto run_with_early_return = [&call_count]()
  {
    {
      const ScopeGuard test_scope_guard([&call_count]() { ++call_count; });
      return;
    }
  };

  run_with_early_return();
  CHECK(call_count == 1);
}

TEST_CASE("ScopeGuard type traits enforce intended API")
{
  using GuardType = ScopeGuard<decltype([]() noexcept {})>;

  STATIC_CHECK(std::is_move_constructible_v<GuardType>);
  STATIC_CHECK_FALSE(std::is_default_constructible_v<GuardType>);
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<GuardType>);
  STATIC_CHECK_FALSE(std::is_copy_assignable_v<GuardType>);
  STATIC_CHECK_FALSE(std::is_move_assignable_v<GuardType>);
}

TEST_CASE("ScopeGuard destructor noexcept follows callable noexcept")
{
  using NothrowGuardType = ScopeGuard<decltype([]() noexcept {})>;
  using ThrowingGuardType = ScopeGuard<decltype([]() {})>;

  STATIC_CHECK(std::is_nothrow_destructible_v<NothrowGuardType>);
  STATIC_CHECK_FALSE(std::is_nothrow_destructible_v<ThrowingGuardType>);
}

TEST_CASE("ScopeGuard constructor noexcept follows callable move noexcept")
{
  using NothrowGuardType = ScopeGuard<NothrowMovableCallable>;
  using ThrowingGuardType = ScopeGuard<ThrowingMovableCallable>;

  STATIC_CHECK(noexcept(NothrowGuardType{NothrowMovableCallable{}}));
  STATIC_CHECK_FALSE(noexcept(ThrowingGuardType{ThrowingMovableCallable{}}));
}

TEST_CASE("ScopeGuard move constructor noexcept follows callable move noexcept")
{
  using NothrowGuardType = ScopeGuard<NothrowMovableCallable>;
  using ThrowingGuardType = ScopeGuard<ThrowingMovableCallable>;

  STATIC_CHECK(noexcept(NothrowGuardType{std::declval<NothrowGuardType&&>()}));
  STATIC_CHECK_FALSE(noexcept(ThrowingGuardType{std::declval<ThrowingGuardType&&>()}));
}

TEST_CASE("ScopeGuard runs callback exactly once after move then dismiss")
{
  int call_count = 0;

  {
    ScopeGuard original([&call_count]() { ++call_count; });
    ScopeGuard moved(std::move(original));
    moved.dismiss();
  }

  CHECK(call_count == 0);
}
} // namespace jewels
