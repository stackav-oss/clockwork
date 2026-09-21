// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/detail.hh"
#include "clockwork/cog/include_common.hh"
#include "clockwork/cog/wrappers/unit_test_cog.hh"
#include "clockwork/dial/include_common.hh"
#include "clockwork/dsl/composition/tests/support/aligned_consumer_aligner_clk_cc.hh"
#include "clockwork/dsl/composition/tests/support/aligned_consumer_aligner_clk_cc_test.hh"
#include "clockwork/dsl/composition/tests/support/aligned_noinput_consumer_cog_clk_cc_test.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <functional>
#include <optional>

namespace clockwork::testing::aligned_consumer::noinput_combo_test
{

namespace
{
using namespace std::chrono_literals;
} // anonymous namespace

TEST_CASE("No-input consumer combo - end-to-end alignment and consumer execution")
{
  NoInputConsumerCogComboWrapper combo;
  const auto start = jewels::time::SyncTime{3000ms};
  auto now = start;
  combo.initialize(now);

  now += 10ms;
  combo.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3100ms)); }, now);
  now += 1ms;
  combo.publish_camera([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3150ms)); }, now);
  now += 1ms;
  combo.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3110ms)); }, now);
  now += 1ms;
  combo.publish_radar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3130ms)); }, now);

  REQUIRE(combo.execute(now));

  const auto& echo = combo.get_consumer().get_outputs().get_echo().get_next_message();
  CHECK(echo.get_sensor_obs_time() == jewels::time::SyncTime(3100ms));
  CHECK(echo.get_camera_obs_time() == jewels::time::SyncTime(3150ms));
  CHECK(echo.get_lidar_count() == 1);
  CHECK(echo.get_has_radar());
}

TEST_CASE("No-input consumer combo - step-by-step execution")
{
  NoInputConsumerCogComboWrapper combo;
  const auto start = jewels::time::SyncTime{3000ms};
  auto now = start;
  combo.initialize(now);

  now += 10ms;
  combo.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3100ms)); }, now);
  now += 1ms;
  combo.publish_camera([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3150ms)); }, now);
  now += 1ms;
  combo.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3110ms)); }, now);
  now += 1ms;
  combo.publish_radar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3130ms)); }, now);

  REQUIRE(combo.execute_aligner(now));

  {
    const auto& alignment = combo.get_aligner().get_outputs().get_alignment().get_next_message();
    CHECK(alignment.get_has_radar());
    CHECK(alignment.get_sensor_seq() == 0);
  }

  REQUIRE(combo.execute_consumer(now));

  {
    const auto& echo = combo.get_consumer().get_outputs().get_echo().get_next_message();
    CHECK(echo.get_sensor_obs_time() == jewels::time::SyncTime(3100ms));
    CHECK(echo.get_camera_obs_time() == jewels::time::SyncTime(3150ms));
    CHECK(echo.get_lidar_count() == 1);
    CHECK(echo.get_has_radar());
  }
}

TEST_CASE("No-input consumer combo - no alignment when constraints not met")
{
  NoInputConsumerCogComboWrapper combo;
  const auto start = jewels::time::SyncTime{3000ms};
  auto now = start;
  combo.initialize(now);

  now += 10ms;
  combo.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3000ms)); }, now);
  now += 1ms;
  combo.publish_camera([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(4000ms)); }, now);
  now += 1ms;
  combo.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3100ms)); }, now);

  REQUIRE(combo.execute_aligner(now));
  CHECK_FALSE(combo.get_aligner().get_outputs().get_alignment().try_get_next_message().has_value());
  CHECK_FALSE(combo.execute_consumer(now));
}

} // namespace clockwork::testing::aligned_consumer::noinput_combo_test
