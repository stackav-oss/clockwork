// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/detail.hh"
#include "clockwork/cog/include_common.hh"
#include "clockwork/cog/wrappers/unit_test_cog.hh"
#include "clockwork/common/cog_execution_error_clk_cc.hh"
#include "clockwork/dial/include_common.hh"
#include "clockwork/dsl/composition/tests/support/combined_aligned_consumer_cog_clk_cc.hh"
#include "clockwork/dsl/composition/tests/support/combined_aligned_consumer_cog_clk_cc_test.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <functional>

namespace clockwork::testing::combined_aligned_consumer_unit_test
{
namespace
{

using namespace std::chrono_literals;
using TestWrapper = combined_aligned_consumer::CombinedAlignedConsumerCogTestWrapper;
using FirstInputs = TestWrapper::CombinedAlignedConsumerCogTestWrapperFirstInputs;
using SecondInputs = TestWrapper::CombinedAlignedConsumerCogTestWrapperSecondInputs;

struct FirstHandles
{
  MessageHandle sensor;
  MessageHandle camera;
  MessageHandle lidar;
};

void publish_first_inputs(jewels::Out<FirstHandles> first_handles_out, FirstInputs& first, jewels::time::SyncTime now)
{
  *first_handles_out = {
    .sensor = first.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3100ms}); }, now),
    .camera = first.publish_camera([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3110ms}); }, now),
    .lidar = first.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3120ms}); }, now),
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

void evict_first_sensor(jewels::Out<MessageHandle> latest_out, FirstInputs& first, jewels::time::SyncTime now)
{
  for (uint32_t index = 1U; index <= 12U; ++index)
  {
    *latest_out = first.publish_sensor(
      [index](auto& msg)
      { msg.set_observation_time(jewels::time::SyncTime{3100ms} + std::chrono::milliseconds{index}); },
      now);
  }
}

TEST_CASE("A stale latest alignment makes a required group pending without reselecting it")
{
  TestWrapper test_cog;
  const auto now = jewels::time::SyncTime{3000ms};
  test_cog.initialize(now);
  auto first = test_cog.get_first();
  auto second = test_cog.get_second();

  FirstHandles first_handles{};
  publish_first_inputs(jewels::Out{first_handles}, first, now);
  const auto stale_sensor = first_handles.sensor;
  MessageHandle latest_sensor{};
  evict_first_sensor(jewels::Out{latest_sensor}, first, now);
  const auto second_input =
    second.publish_secondary([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3200ms}); }, now);

  first_handles.sensor = stale_sensor;
  publish_first_alignment(first, first_handles, now);
  auto second_batch = second.make_batch();
  second_batch.set_secondary(second_input);
  static_cast<void>(second_batch.publish(now));

  auto& unit_test_cog = test_cog.get_unit_test_cog();
  auto throttled_until = jewels::time::SyncTime::min();
  REQUIRE(jewels::ok(unit_test_cog.prepare_for_execution(jewels::Out{throttled_until}, now)));
  const auto stale_result = unit_test_cog.execute(CogExecuteParams{.start_time = now});
  REQUIRE_FALSE(stale_result.has_value());
  CHECK(stale_result.error() == CogExecutionError::alignment_stale);

  // The second alignment keeps the cog ready, but the first group's cursor view is now empty.
  throttled_until = jewels::time::SyncTime::min();
  REQUIRE(jewels::ok(unit_test_cog.prepare_for_execution(jewels::Out{throttled_until}, now)));
  const auto pending_result = unit_test_cog.execute(CogExecuteParams{.start_time = now});
  REQUIRE_FALSE(pending_result.has_value());
  CHECK(pending_result.error() == CogExecutionError::alignment_pending);

  first_handles.sensor = latest_sensor;
  publish_first_alignment(first, first_handles, now);
  REQUIRE(test_cog.execute(now));
}

} // namespace
} // namespace clockwork::testing::combined_aligned_consumer_unit_test
