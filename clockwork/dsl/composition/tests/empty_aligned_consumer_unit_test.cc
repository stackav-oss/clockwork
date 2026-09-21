// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/detail.hh"
#include "clockwork/cog/include_common.hh"
#include "clockwork/cog/wrappers/unit_test_cog.hh"
#include "clockwork/common/cog_execution_error_clk_cc.hh"
#include "clockwork/dial/include_common.hh"
#include "clockwork/dsl/composition/tests/support/aligned_consumer_aligner_clk_cc.hh"
#include "clockwork/dsl/composition/tests/support/empty_aligned_consumer_cog_clk_cc.hh"
#include "clockwork/dsl/composition/tests/support/empty_aligned_consumer_cog_clk_cc_test.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <functional>

namespace clockwork::testing::empty_aligned_consumer_unit_test
{
namespace
{

using namespace std::chrono_literals;

TEST_CASE("A permitted empty alignment group supplies empty aligned dials")
{
  empty_aligned_consumer::EmptyAlignedConsumerCogTestWrapper test_cog;
  const auto now = jewels::time::SyncTime{3000ms};
  test_cog.initialize(now);

  test_cog.get_inputs().get_raw_data().publish(
    [](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3100ms}); }, now);
  REQUIRE(test_cog.execute(now));

  {
    const auto& echo = test_cog.get_outputs().get_echo().get_next_message();
    CHECK(echo.get_sensor_obs_time() == jewels::time::SyncTime{3100ms});
    CHECK(echo.get_lidar_count() == 0U);
    CHECK_FALSE(echo.get_has_radar());
  }

  auto aligned = test_cog.get_aligned();
  const auto stale_sensor =
    aligned.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3200ms}); }, now);
  const auto stale_camera =
    aligned.publish_camera([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3210ms}); }, now);
  const auto stale_lidar =
    aligned.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3220ms}); }, now);
  for (uint32_t index = 1U; index <= 20U; ++index)
  {
    aligned.publish_sensor(
      [index](auto& msg)
      { msg.set_observation_time(jewels::time::SyncTime{3200ms} + std::chrono::milliseconds{index}); },
      now);
  }
  auto batch = aligned.make_batch();
  batch.set_sensor(stale_sensor);
  batch.set_camera(stale_camera);
  batch.set_lidar_range(stale_lidar, stale_lidar);
  batch.unset_radar();
  static_cast<void>(batch.publish(now));

  test_cog.get_inputs().get_raw_data().publish(
    [](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3300ms}); }, now);
  auto& unit_test_cog = test_cog.get_unit_test_cog();
  auto throttled_until = jewels::time::SyncTime::min();
  REQUIRE(jewels::ok(unit_test_cog.prepare_for_execution(jewels::Out{throttled_until}, now)));
  const auto stale_result = unit_test_cog.execute(CogExecuteParams{.start_time = now});
  REQUIRE_FALSE(stale_result.has_value());
  CHECK(stale_result.error() == CogExecutionError::alignment_stale);

  throttled_until = jewels::time::SyncTime::min();
  REQUIRE(jewels::ok(unit_test_cog.prepare_for_execution(jewels::Out{throttled_until}, now)));
  REQUIRE(unit_test_cog.execute(CogExecuteParams{.start_time = now}).has_value());

  {
    const auto& echo = test_cog.get_outputs().get_echo().get_next_message();
    CHECK(echo.get_sensor_obs_time() == jewels::time::SyncTime{3300ms});
    CHECK(echo.get_lidar_count() == 0U);
    CHECK_FALSE(echo.get_has_radar());
  }

  const auto sensor =
    aligned.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3400ms}); }, now);
  const auto camera =
    aligned.publish_camera([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3410ms}); }, now);
  const auto lidar =
    aligned.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3420ms}); }, now);
  batch.set_sensor(sensor);
  batch.set_camera(camera);
  batch.set_lidar_range(lidar, lidar);
  static_cast<void>(batch.publish(now));

  test_cog.get_inputs().get_raw_data().publish(
    [](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3500ms}); }, now);
  REQUIRE(test_cog.execute(now));

  {
    const auto& echo = test_cog.get_outputs().get_echo().get_next_message();
    CHECK(echo.get_sensor_obs_time() == jewels::time::SyncTime{3500ms});
    CHECK(echo.get_lidar_count() == 1U);
    CHECK_FALSE(echo.get_has_radar());
  }
}

} // namespace

} // namespace clockwork::testing::empty_aligned_consumer_unit_test
