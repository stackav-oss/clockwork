// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/aligner/timer_control.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

namespace clockwork::aligner
{
namespace
{

/// Mock handler matching the DynamicTimerHandler interface for testing AlignerTimerControl.
class MockDynamicTimer
{
public:
  jewels::BinaryOutcome arm(jewels::time::SyncTime trigger_at)
  {
    armed_ = true;
    arm_time_ = trigger_at;
    return jewels::success;
  }

  jewels::BinaryOutcome disarm()
  {
    armed_ = false;
    return jewels::success;
  }

  [[nodiscard]] bool is_armed() const
  {
    return armed_;
  }

  bool armed_{false};
  jewels::time::SyncTime arm_time_{};
};

TEST_CASE("AlignerTimerControl delegates to handler", "[AlignerTimerControl]")
{
  MockDynamicTimer mock;

  SECTION("arm delegates to handler")
  {
    AlignerTimerControl control(mock);
    auto target = jewels::time::SyncTime{std::chrono::milliseconds{500}};
    REQUIRE(jewels::ok(control.arm(target)));
    REQUIRE(mock.armed_);
    REQUIRE(mock.arm_time_ == target);
  }

  SECTION("disarm delegates to handler")
  {
    mock.armed_ = true;
    AlignerTimerControl control(mock);
    REQUIRE(jewels::ok(control.disarm()));
    REQUIRE_FALSE(mock.armed_);
  }

  SECTION("is_armed delegates to handler (live)")
  {
    AlignerTimerControl control(mock);
    REQUIRE_FALSE(control.is_armed());

    mock.armed_ = true;
    REQUIRE(control.is_armed());
  }

  SECTION("const control can arm and disarm")
  {
    const AlignerTimerControl control(mock);
    auto target = jewels::time::SyncTime{std::chrono::milliseconds{200}};
    REQUIRE(jewels::ok(control.arm(target)));
    REQUIRE(mock.armed_);
    REQUIRE(jewels::ok(control.disarm()));
    REQUIRE_FALSE(mock.armed_);
  }
}

/// Mock handler that always fails arm/disarm.
class FailingMockDynamicTimer
{
public:
  static jewels::BinaryOutcome arm(jewels::time::SyncTime /*trigger_at*/)
  {
    return jewels::failure;
  }

  static jewels::BinaryOutcome disarm()
  {
    return jewels::failure;
  }

  [[nodiscard]] static bool is_armed()
  {
    return false;
  }
};

TEST_CASE("AlignerTimerControl propagates failure from handler", "[AlignerTimerControl]")
{
  FailingMockDynamicTimer failing;
  AlignerTimerControl control(failing);

  SECTION("arm returns failure")
  {
    REQUIRE(jewels::fails(control.arm(jewels::time::SyncTime{std::chrono::milliseconds{100}})));
  }

  SECTION("disarm returns failure")
  {
    REQUIRE(jewels::fails(control.disarm()));
  }
}

} // namespace
} // namespace clockwork::aligner
