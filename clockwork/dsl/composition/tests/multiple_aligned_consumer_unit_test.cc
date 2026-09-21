// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/detail.hh"
#include "clockwork/cog/include_common.hh"
#include "clockwork/cog/wrappers/unit_test_cog.hh"
#include "clockwork/dial/include_common.hh"
#include "clockwork/dsl/composition/tests/support/aligned_consumer_aligner_clk_cc.hh"
#include "clockwork/dsl/composition/tests/support/multiple_aligned_consumer_cog_clk_cc.hh"
#include "clockwork/dsl/composition/tests/support/multiple_aligned_consumer_cog_clk_cc_test.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <functional>

namespace clockwork::testing::multiple_aligned_consumer_unit_test
{
namespace
{

using namespace std::chrono_literals;
using TestWrapper = multiple_aligned_consumer::MultipleAlignedConsumerCogTestWrapper;
using FirstInputs = TestWrapper::MultipleAlignedConsumerCogTestWrapperFirstInputs;
using SecondInputs = TestWrapper::MultipleAlignedConsumerCogTestWrapperSecondInputs;

bool prepare_fails(TestWrapper& test_cog, const jewels::time::SyncTime now)
{
  auto throttled_until = jewels::time::SyncTime::min();
  return jewels::fails(test_cog.get_unit_test_cog().prepare_for_execution(jewels::Out{throttled_until}, now));
}

struct FirstHandles
{
  MessageHandle sensor;
  MessageHandle camera;
  MessageHandle lidar;
};

FirstHandles publish_first_inputs(FirstInputs& first, jewels::time::SyncTime now, jewels::time::SyncTime sensor_time)
{
  return {
    .sensor = first.publish_sensor([sensor_time](auto& msg) { msg.set_observation_time(sensor_time); }, now),
    .camera = first.publish_camera([sensor_time](auto& msg) { msg.set_observation_time(sensor_time + 1ms); }, now),
    .lidar = first.publish_lidar([sensor_time](auto& msg) { msg.set_observation_time(sensor_time + 2ms); }, now),
  };
}

void publish_first_alignment(FirstInputs& first, const FirstHandles& handles, jewels::time::SyncTime now)
{
  auto batch = first.make_batch();
  batch.set_sensor(handles.sensor);
  batch.set_camera(handles.camera);
  batch.set_lidar_range(handles.lidar, handles.lidar);
  batch.unset_radar();
  static_cast<void>(batch.publish(now));
}

MessageHandle
publish_second_input(SecondInputs& second, jewels::time::SyncTime now, jewels::time::SyncTime observation_time)
{
  return second.publish_secondary([observation_time](auto& msg) { msg.set_observation_time(observation_time); }, now);
}

void publish_second_alignment(SecondInputs& second, MessageHandle handle, jewels::time::SyncTime now)
{
  auto batch = second.make_batch();
  batch.set_secondary(handle);
  static_cast<void>(batch.publish(now));
}

MessageHandle evict_first_sensor(FirstInputs& first, jewels::time::SyncTime now, jewels::time::SyncTime base_time)
{
  MessageHandle latest{};
  for (uint32_t index = 1U; index <= 12U; ++index)
  {
    latest = first.publish_sensor(
      [base_time, index](auto& msg) { msg.set_observation_time(base_time + std::chrono::milliseconds{index}); }, now);
  }
  return latest;
}

MessageHandle evict_second_input(SecondInputs& second, jewels::time::SyncTime now, jewels::time::SyncTime base_time)
{
  MessageHandle latest{};
  for (uint32_t index = 1U; index <= 12U; ++index)
  {
    latest = publish_second_input(second, now, base_time + std::chrono::milliseconds{index});
  }
  return latest;
}

void check_echo(TestWrapper& test_cog, jewels::time::SyncTime first_time, jewels::time::SyncTime second_time)
{
  const auto& echo = test_cog.get_outputs().get_echo().get_next_message();
  CHECK(echo.get_sensor_obs_time() == first_time);
  CHECK(echo.get_camera_obs_time() == second_time);
}

TEST_CASE("Multiple aligned groups resolve together")
{
  TestWrapper test_cog;
  const auto now = jewels::time::SyncTime{3000ms};
  test_cog.initialize(now);
  auto first = test_cog.get_first();
  auto second = test_cog.get_second();

  const auto first_handles = publish_first_inputs(first, now, jewels::time::SyncTime{3100ms});
  const auto second_handle = publish_second_input(second, now, jewels::time::SyncTime{3200ms});
  publish_first_alignment(first, first_handles, now);
  publish_second_alignment(second, second_handle, now);

  REQUIRE(test_cog.execute(now));
  check_echo(test_cog, jewels::time::SyncTime{3100ms}, jewels::time::SyncTime{3200ms});
}

TEST_CASE("A stale first alignment preserves the second alignment")
{
  TestWrapper test_cog;
  const auto now = jewels::time::SyncTime{3000ms};
  test_cog.initialize(now);
  auto first = test_cog.get_first();
  auto second = test_cog.get_second();

  auto first_handles = publish_first_inputs(first, now, jewels::time::SyncTime{3100ms});
  const auto stale_sensor = first_handles.sensor;
  const auto latest_sensor = evict_first_sensor(first, now, jewels::time::SyncTime{3100ms});
  const auto second_handle = publish_second_input(second, now, jewels::time::SyncTime{3200ms});
  first_handles.sensor = stale_sensor;
  publish_first_alignment(first, first_handles, now);
  publish_second_alignment(second, second_handle, now);

  CHECK_FALSE(test_cog.execute(now));
  REQUIRE(prepare_fails(test_cog, now));

  first_handles.sensor = latest_sensor;
  publish_first_alignment(first, first_handles, now);
  REQUIRE(test_cog.execute(now));
  check_echo(test_cog, jewels::time::SyncTime{3112ms}, jewels::time::SyncTime{3200ms});
}

TEST_CASE("A stale second alignment preserves the first alignment")
{
  TestWrapper test_cog;
  const auto now = jewels::time::SyncTime{3000ms};
  test_cog.initialize(now);
  auto first = test_cog.get_first();
  auto second = test_cog.get_second();

  const auto first_handles = publish_first_inputs(first, now, jewels::time::SyncTime{3100ms});
  const auto stale_second = publish_second_input(second, now, jewels::time::SyncTime{3200ms});
  const auto latest_second = evict_second_input(second, now, jewels::time::SyncTime{3200ms});
  publish_first_alignment(first, first_handles, now);
  publish_second_alignment(second, stale_second, now);

  CHECK_FALSE(test_cog.execute(now));
  REQUIRE(prepare_fails(test_cog, now));

  publish_second_alignment(second, latest_second, now);
  REQUIRE(test_cog.execute(now));
  check_echo(test_cog, jewels::time::SyncTime{3100ms}, jewels::time::SyncTime{3212ms});
}

TEST_CASE("A pending alignment preserves both alignment messages")
{
  SECTION("first group pending")
  {
    TestWrapper test_cog;
    const auto now = jewels::time::SyncTime{3000ms};
    test_cog.initialize(now);
    auto first = test_cog.get_first();
    auto second = test_cog.get_second();

    auto first_handles = publish_first_inputs(first, now, jewels::time::SyncTime{3100ms});
    first_handles.sensor = MessageHandle{1U};
    const auto second_handle = publish_second_input(second, now, jewels::time::SyncTime{3200ms});
    publish_first_alignment(first, first_handles, now);
    publish_second_alignment(second, second_handle, now);

    CHECK_FALSE(test_cog.execute(now));
    first.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3110ms}); }, now);
    REQUIRE(test_cog.execute(now));
    check_echo(test_cog, jewels::time::SyncTime{3110ms}, jewels::time::SyncTime{3200ms});
  }

  SECTION("second group pending")
  {
    TestWrapper test_cog;
    const auto now = jewels::time::SyncTime{3000ms};
    test_cog.initialize(now);
    auto first = test_cog.get_first();
    auto second = test_cog.get_second();

    const auto first_handles = publish_first_inputs(first, now, jewels::time::SyncTime{3100ms});
    publish_first_alignment(first, first_handles, now);
    publish_second_alignment(second, MessageHandle{0U}, now);

    CHECK_FALSE(test_cog.execute(now));
    publish_second_input(second, now, jewels::time::SyncTime{3200ms});
    REQUIRE(test_cog.execute(now));
    check_echo(test_cog, jewels::time::SyncTime{3100ms}, jewels::time::SyncTime{3200ms});
  }
}

TEST_CASE("A stale second alignment is consumed while the first alignment is pending")
{
  TestWrapper test_cog;
  const auto now = jewels::time::SyncTime{3000ms};
  test_cog.initialize(now);
  auto first = test_cog.get_first();
  auto second = test_cog.get_second();

  auto first_handles = publish_first_inputs(first, now, jewels::time::SyncTime{3100ms});
  first_handles.sensor = MessageHandle{1U};
  const auto stale_second = publish_second_input(second, now, jewels::time::SyncTime{3200ms});
  const auto latest_second = evict_second_input(second, now, jewels::time::SyncTime{3200ms});
  publish_first_alignment(first, first_handles, now);
  publish_second_alignment(second, stale_second, now);

  CHECK_FALSE(test_cog.execute(now));
  REQUIRE(prepare_fails(test_cog, now));
  first.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3110ms}); }, now);
  CHECK_FALSE(test_cog.execute(now));

  publish_second_alignment(second, latest_second, now);
  REQUIRE(test_cog.execute(now));
  check_echo(test_cog, jewels::time::SyncTime{3110ms}, jewels::time::SyncTime{3212ms});
}

TEST_CASE("Two stale alignment groups are consumed one at a time")
{
  TestWrapper test_cog;
  const auto now = jewels::time::SyncTime{3000ms};
  test_cog.initialize(now);
  auto first = test_cog.get_first();
  auto second = test_cog.get_second();

  auto first_handles = publish_first_inputs(first, now, jewels::time::SyncTime{3100ms});
  const auto stale_sensor = first_handles.sensor;
  const auto latest_sensor = evict_first_sensor(first, now, jewels::time::SyncTime{3100ms});
  const auto stale_second = publish_second_input(second, now, jewels::time::SyncTime{3200ms});
  const auto latest_second = evict_second_input(second, now, jewels::time::SyncTime{3200ms});
  first_handles.sensor = stale_sensor;
  publish_first_alignment(first, first_handles, now);
  publish_second_alignment(second, stale_second, now);

  CHECK_FALSE(test_cog.execute(now));
  REQUIRE(prepare_fails(test_cog, now));

  first_handles.sensor = latest_sensor;
  publish_first_alignment(first, first_handles, now);
  CHECK_FALSE(test_cog.execute(now));
  REQUIRE(prepare_fails(test_cog, now));

  publish_second_alignment(second, latest_second, now);
  REQUIRE(test_cog.execute(now));
  check_echo(test_cog, jewels::time::SyncTime{3112ms}, jewels::time::SyncTime{3212ms});
}

} // namespace
} // namespace clockwork::testing::multiple_aligned_consumer_unit_test
