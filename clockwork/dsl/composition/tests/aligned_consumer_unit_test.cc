// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/include_common.hh"
#include "clockwork/cog/wrappers/unit_test_cog.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/dsl/composition/tests/support/aligned_consumer_aligner_clk_cc.hh"
#include "clockwork/dsl/composition/tests/support/aligned_consumer_cog_clk_cc_test.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <functional>

namespace clockwork::testing::aligned_consumer_unit_test
{

namespace
{
using namespace std::chrono_literals;
} // anonymous namespace

// Verify that a consumer cog with aligned_inputs correctly resolves alignment messages
// and sees the upstream messages the aligner selected, using the generated test wrapper.
//
// Replicates the CSR oracle from aligned_consumer_csr_test.cc:
//   Alignment 1: all inputs present, lidar batch of 2, radar present
//   Alignment 2: camera reused, radar absent, lidar batch of 1
TEST_CASE("Consumer unit test - aligned inputs resolve correctly")
{
  aligned_consumer::ConsumerCogTestWrapper test_cog;
  const auto start = jewels::time::SyncTime{3000ms};
  auto now = start;
  test_cog.initialize(now);

  auto aligned = test_cog.get_aligned();

  // --- Publish upstream data for alignment 1 ---
  auto sensor1 =
    aligned.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3100ms)); }, now);
  auto cam1 = aligned.publish_camera([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3150ms)); }, now);
  auto lid1 = aligned.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3110ms)); }, now);
  auto lid2 = aligned.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3120ms)); }, now);
  auto rad1 = aligned.publish_radar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3130ms)); }, now);

  // --- Alignment 1: all present, lidar batch of 2 ---
  auto batch = aligned.make_batch();
  batch.set_sensor(sensor1);
  batch.set_camera(cam1);
  batch.set_lidar_range(lid1, lid2);
  batch.set_radar(rad1);
  static_cast<void>(batch.publish(now));
  REQUIRE(test_cog.execute(now));

  {
    const auto& echo = test_cog.get_outputs().get_echo().get_next_message();
    CHECK(echo.get_sensor_obs_time() == jewels::time::SyncTime(3100ms));
    CHECK(echo.get_camera_obs_time() == jewels::time::SyncTime(3150ms));
    CHECK(echo.get_lidar_count() == 2);
    CHECK(echo.get_has_radar());
  }

  // --- Publish upstream data for alignment 2 ---
  now += 500ms;
  auto sensor2 =
    aligned.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3500ms)); }, now);
  auto lid3 = aligned.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3510ms)); }, now);

  // --- Alignment 2: camera reused (persists from batch), radar absent ---
  batch.set_sensor(sensor2);
  batch.set_lidar_range(lid3, lid3);
  batch.unset_radar();
  static_cast<void>(batch.publish(now));
  REQUIRE(test_cog.execute(now));

  {
    const auto& echo = test_cog.get_outputs().get_echo().get_next_message();
    CHECK(echo.get_sensor_obs_time() == jewels::time::SyncTime(3500ms));
    CHECK(echo.get_camera_obs_time() == jewels::time::SyncTime(3150ms)); // reused
    CHECK(echo.get_lidar_count() == 1);
    CHECK_FALSE(echo.get_has_radar());
  }
}

// Stale alignment: the alignment message references a sensor seqno that has been evicted
// from the input view because too many newer messages arrived. The cog should skip the
// stale alignment (advancing the alignment cursor) and then succeed on a subsequent valid one.
TEST_CASE("Consumer unit test - stale alignment is skipped and recovered")
{
  aligned_consumer::ConsumerCogTestWrapper test_cog;
  const auto start = jewels::time::SyncTime{3000ms};
  auto now = start;
  test_cog.initialize(now);

  auto aligned = test_cog.get_aligned();

  // Publish the first sensor message, then 20 more to fill and overflow the 20-slot consumer sensor buffer.
  auto sensor_evicted =
    aligned.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3100ms)); }, now);
  testing::MessageHandle sensor_latest{};
  for (uint32_t i = 1; i <= 20; ++i)
  {
    sensor_latest = aligned.publish_sensor(
      [&](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3100ms) + std::chrono::milliseconds(i)); }, now);
  }

  auto cam1 = aligned.publish_camera([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3150ms)); }, now);
  auto lid1 = aligned.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3110ms)); }, now);

  // Alignment referencing the evicted sensor — should be stale
  auto batch_stale = aligned.make_batch();
  batch_stale.set_sensor(sensor_evicted);
  batch_stale.set_camera(cam1);
  batch_stale.set_lidar_range(lid1, lid1);
  batch_stale.unset_radar();
  static_cast<void>(batch_stale.publish(now));
  CHECK_FALSE(test_cog.execute(now));

  // Alignment referencing valid (latest) sensor — should succeed
  auto batch_valid = aligned.make_batch();
  batch_valid.set_sensor(sensor_latest);
  batch_valid.set_camera(cam1);
  batch_valid.set_lidar_range(lid1, lid1);
  batch_valid.unset_radar();
  static_cast<void>(batch_valid.publish(now));
  REQUIRE(test_cog.execute(now));

  {
    const auto& echo = test_cog.get_outputs().get_echo().get_next_message();
    CHECK(echo.get_sensor_obs_time() == jewels::time::SyncTime(3120ms));
    CHECK(echo.get_lidar_count() == 1);
    CHECK_FALSE(echo.get_has_radar());
  }
}

TEST_CASE("Consumer unit test - latest stale alignment does not fall back to an older alignment")
{
  aligned_consumer::ConsumerCogTestWrapper test_cog;
  const auto now = jewels::time::SyncTime{3000ms};
  test_cog.initialize(now);

  auto aligned = test_cog.get_aligned();
  const auto sensor_evicted =
    aligned.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3100ms}); }, now);
  testing::MessageHandle sensor_latest{};
  for (uint32_t index = 1U; index <= 20U; ++index)
  {
    sensor_latest = aligned.publish_sensor(
      [index](auto& msg)
      { msg.set_observation_time(jewels::time::SyncTime{3100ms} + std::chrono::milliseconds{index}); },
      now);
  }
  const auto camera =
    aligned.publish_camera([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3150ms}); }, now);
  const auto lidar =
    aligned.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{3110ms}); }, now);

  auto batch = aligned.make_batch();
  batch.set_sensor(sensor_latest);
  batch.set_camera(camera);
  batch.set_lidar_range(lidar, lidar);
  batch.unset_radar();
  static_cast<void>(batch.publish(now));

  batch.set_sensor(sensor_evicted);
  static_cast<void>(batch.publish(now));

  // The newer alignment is stale. The older resolvable alignment must not be selected instead.
  CHECK_FALSE(test_cog.execute(now));

  batch.set_sensor(sensor_latest);
  static_cast<void>(batch.publish(now));
  REQUIRE(test_cog.execute(now));

  const auto& echo = test_cog.get_outputs().get_echo().get_next_message();
  CHECK(echo.get_sensor_obs_time() == jewels::time::SyncTime{3120ms});
}

// Pending alignment: the alignment message references a sensor seqno that hasn't arrived
// yet. The cog should fail to execute, but the alignment message stays "new" so that
// re-executing after the data arrives succeeds without a new alignment message.
TEST_CASE("Consumer unit test - pending alignment resolves after data arrives")
{
  aligned_consumer::ConsumerCogTestWrapper test_cog;
  const auto start = jewels::time::SyncTime{3000ms};
  auto now = start;
  test_cog.initialize(now);

  auto aligned = test_cog.get_aligned();

  auto cam1 = aligned.publish_camera([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3150ms)); }, now);
  auto lid1 = aligned.publish_lidar([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3110ms)); }, now);

  // Alignment referencing sensor seqno 0 which hasn't been published yet
  auto batch_pending = aligned.make_batch();
  batch_pending.set_sensor(testing::MessageHandle{0});
  batch_pending.set_camera(cam1);
  batch_pending.set_lidar_range(lid1, lid1);
  batch_pending.unset_radar();
  static_cast<void>(batch_pending.publish(now));
  CHECK_FALSE(test_cog.execute(now));

  // Now publish the sensor — the same seqno 0 the alignment references
  aligned.publish_sensor([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3100ms)); }, now);

  // Re-execute: the alignment message is still "new", and sensor seqno 0 now exists
  REQUIRE(test_cog.execute(now));

  {
    const auto& echo = test_cog.get_outputs().get_echo().get_next_message();
    CHECK(echo.get_sensor_obs_time() == jewels::time::SyncTime(3100ms));
    CHECK(echo.get_camera_obs_time() == jewels::time::SyncTime(3150ms));
    CHECK(echo.get_lidar_count() == 1);
    CHECK_FALSE(echo.get_has_radar());
  }
}

} // namespace clockwork::testing::aligned_consumer_unit_test
