// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/detail.hh"
#include "clockwork/cog/include_common.hh"
#include "clockwork/cog/wrappers/unit_test_cog.hh"
#include "clockwork/dial/include_common.hh"
#include "clockwork/dsl/composition/tests/support/aligned_consumer_aligner_clk_cc.hh"
#include "clockwork/dsl/composition/tests/support/aligned_consumer_aligner_clk_cc_test.hh"
#include "clockwork/dsl/composition/tests/support/aligned_consumer_cog_clk_cc_test.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <functional>
#include <optional>

namespace clockwork::testing::aligned_consumer_combo_test
{

namespace
{
using namespace std::chrono_literals;
} // anonymous namespace

// Verify that the combo wrapper correctly wires the aligner and consumer together:
// upstream messages published once are visible to both cogs, the aligner produces
// an alignment message, and the consumer resolves it to echo the correct values.
TEST_CASE("Combo test - aligner + consumer end-to-end")
{
  aligned_consumer::ConsumerCogComboWrapper combo;
  const auto start = jewels::time::SyncTime{3000ms};
  auto now = start;
  combo.initialize(now);

  // --- Alignment 1: all inputs present ---
  now += 10ms;
  combo.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3100ms)); }, now);
  now += 1ms;
  combo.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3110ms)); }, now);
  now += 1ms;
  combo.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3120ms)); }, now);
  now += 1ms;
  combo.publish_radar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3130ms)); }, now);
  now += 7ms;
  combo.publish_camera([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3150ms)); }, now);

  // Execute both — aligner produces alignment, consumer consumes it
  REQUIRE(combo.execute(now));

  {
    const auto& echo = combo.get_consumer().get_outputs().get_echo().get_next_message();
    CHECK(echo.get_sensor_obs_time() == jewels::time::SyncTime(3100ms));
    CHECK(echo.get_camera_obs_time() == jewels::time::SyncTime(3150ms));
    CHECK(echo.get_lidar_count() == 2);
    CHECK(echo.get_has_radar());
  }

  // --- Alignment 2: camera reused, radar absent ---
  // Publish only sensor and lidar; no new camera (reuse), no radar (optional, times out at 200ms).
  now = start + 500ms;
  combo.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3500ms)); }, now);
  now += 1ms;
  combo.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3510ms)); }, now);

  // First execute: aligner sees partial alignment (radar missing), arms timer
  REQUIRE(combo.execute_aligner(now));

  // Advance past the 200ms radar timeout
  now += 201ms;

  // Second execute: timer fires, aligner publishes partial alignment (no radar)
  REQUIRE(combo.execute_aligner(now));

  // Now the consumer can resolve the alignment
  REQUIRE(combo.execute_consumer(now));

  {
    const auto& echo = combo.get_consumer().get_outputs().get_echo().get_next_message();
    CHECK(echo.get_sensor_obs_time() == jewels::time::SyncTime(3500ms));
    CHECK(echo.get_camera_obs_time() == jewels::time::SyncTime(3150ms)); // reused from alignment 1
    CHECK(echo.get_lidar_count() == 1);
    CHECK_FALSE(echo.get_has_radar()); // no radar in constraint window
  }
}

// Demonstrate that step-by-step execution works: execute the aligner alone,
// inspect intermediate state, then execute the consumer separately.
TEST_CASE("Combo test - step-by-step execution")
{
  aligned_consumer::ConsumerCogComboWrapper combo;
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

  // Execute aligner only
  REQUIRE(combo.execute_aligner(now));

  {
    const auto& alignment = combo.get_aligner().get_outputs().get_alignment().get_next_message();
    CHECK(alignment.get_has_radar());
    CHECK(alignment.get_sensor_seq() == 0);
    CHECK(alignment.get_camera_seq() == 0);
  }

  // Now execute consumer using the alignment produced above
  REQUIRE(combo.execute_consumer(now));

  {
    const auto& echo = combo.get_consumer().get_outputs().get_echo().get_next_message();
    CHECK(echo.get_sensor_obs_time() == jewels::time::SyncTime(3100ms));
    CHECK(echo.get_camera_obs_time() == jewels::time::SyncTime(3150ms));
    CHECK(echo.get_lidar_count() == 1);
    CHECK(echo.get_has_radar());
  }
}

// Negative test: aligner executes but does not produce alignment when constraints are not met.
// Consumer should not execute because there's no new alignment message.
TEST_CASE("Combo test - no alignment when constraints not met")
{
  aligned_consumer::ConsumerCogComboWrapper combo;
  const auto start = jewels::time::SyncTime{3000ms};
  auto now = start;
  combo.initialize(now);

  // Publish sensor and camera with observation times differing by >500ms
  // (violates the |sensor - camera| <= 500ms constraint)
  now += 10ms;
  combo.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3000ms)); }, now);
  now += 1ms;
  combo.publish_camera([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(4000ms)); }, now);
  now += 1ms;
  combo.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3100ms)); }, now);

  // Aligner executes (is_ready is true because new messages arrived) but produces no alignment
  REQUIRE(combo.execute_aligner(now));

  // Verify no alignment was produced by checking try_get_next_message returns nullopt
  const auto maybe_alignment = combo.get_aligner().get_outputs().get_alignment().try_get_next_message();
  CHECK_FALSE(maybe_alignment.has_value());

  // Consumer should not execute (no new alignment message)
  CHECK_FALSE(combo.execute_consumer(now));
}

} // namespace clockwork::testing::aligned_consumer_combo_test
