// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/detail.hh"
#include "clockwork/cog/include_common.hh"
#include "clockwork/cog/wrappers/unit_test_cog.hh"
#include "clockwork/dial/include_common.hh"
#include "clockwork/dsl/aligner/gen/tests/support/exact_batch_aligner_clk_cc.hh"
#include "clockwork/dsl/aligner/gen/tests/support/exact_batch_aligner_clk_cc_test.hh"
#include "clockwork/dsl/aligner/gen/tests/support/optional_reference_nearest_aligner_clk_cc.hh"
#include "clockwork/dsl/aligner/gen/tests/support/optional_reference_nearest_aligner_clk_cc_test.hh"
#include "clockwork/dsl/aligner/gen/tests/support/test_aligner_clk_cc.hh"
#include "clockwork/dsl/aligner/gen/tests/support/test_aligner_clk_cc_test.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <functional>
#include <optional>

namespace clockwork::aligner_csr_test
{

namespace
{

using OptionalReferenceNearestWrapper = OptionalReferenceNearestAlignerTestWrapper;
using TestWrapper = TestAlignerTestWrapper;

testing::MessageHandle publish_sensor(auto&& input, std::chrono::milliseconds obs_time, auto& now)
{
  now += std::chrono::milliseconds{1};
  return input.publish([obs_time](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(obs_time)); }, now);
}

} // anonymous namespace

// Unit test exercising identical message sequences and expected outputs as the
// CSR integration test (aligner_csr_test.cc, test case 1).
//
// Exercises three search types plus batch and optional inputs:
//   pose_lo  → FIRST_IN_RANGE (forward scan), batch boundary
//   pose_hi  → LAST_IN_RANGE (reverse scan), batch boundary
//   lidar    → LAST_IN_RANGE (reverse scan), reuse (get_view)
//   camera   → NEAREST (bidirectional scan from lidar.obs_time)
//   tick     → ANY_MATCH (forward scan, constraint-filtered)
//   radar    → ANY_MATCH (forward scan), optional
//
// Results must match the CSR test exactly.
TEST_CASE("Aligner unit test - search type dispatch with NEAREST, LAST_IN_RANGE, reuse, optional, and batch")
{
  using namespace std::chrono_literals;

  TestWrapper test_cog;
  auto now = jewels::time::SyncTime{3000ms};
  test_cog.initialize(now);

  auto inputs = test_cog.get_inputs();

  // --- Publish camera messages (NEAREST from lidar ref) ---
  publish_sensor(inputs.get_camera(), 3100ms, now); // c0: 3100ms
  publish_sensor(inputs.get_camera(), 3150ms, now); // c1: 3150ms
  auto cam2 = publish_sensor(inputs.get_camera(), 3250ms, now);
  publish_sensor(inputs.get_camera(), 3340ms, now); // c3: 3340ms
  auto cam4 = publish_sensor(inputs.get_camera(), 3390ms, now);
  auto cam5 = publish_sensor(inputs.get_camera(), 3450ms, now);

  // --- Publish lidar messages (LAST_IN_RANGE, reuse) ---
  publish_sensor(inputs.get_lidar(), 3050ms, now); // l0: 3050ms
  auto lid1 = publish_sensor(inputs.get_lidar(), 3240ms, now);
  auto lid2 = publish_sensor(inputs.get_lidar(), 3380ms, now);

  // --- Publish radar messages (optional, ANY_MATCH with constraints) ---
  //   r0: 3180ms  r1: 3330ms
  auto rad0 = publish_sensor(inputs.get_radar(), 3180ms, now);
  auto rad1 = publish_sensor(inputs.get_radar(), 3330ms, now);

  // --- Publish pose messages (batch input, batch_size [2, 4]) ---
  auto pose0 = publish_sensor(inputs.get_pose(), 3050ms, now);
  publish_sensor(inputs.get_pose(), 3100ms, now); // p1: 3100ms
  publish_sensor(inputs.get_pose(), 3150ms, now); // p2: 3150ms
  auto pose3 = publish_sensor(inputs.get_pose(), 3200ms, now);
  auto pose4 = publish_sensor(inputs.get_pose(), 3250ms, now);
  publish_sensor(inputs.get_pose(), 3300ms, now); // p5: 3300ms
  publish_sensor(inputs.get_pose(), 3350ms, now); // p6: 3350ms
  auto pose7 = publish_sensor(inputs.get_pose(), 3400ms, now);
  auto pose8 = publish_sensor(inputs.get_pose(), 3450ms, now);
  auto pose9 = publish_sensor(inputs.get_pose(), 3500ms, now);

  // --- Tick 0 at obs_time 3200ms ---
  now = jewels::time::SyncTime{3100ms};
  auto tick0 =
    inputs.get_tick().publish([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3200ms)); }, now);
  REQUIRE(test_cog.execute(now));

  // --- Alignment 0 ---
  //   tick=0, camera=2, lidar=1, radar=present(0), pose=[0,3]
  {
    const auto& alignment = test_cog.get_outputs().get_alignment().get_next_message();
    CHECK(alignment.get_tick_seq() == tick0.seqno);
    CHECK(alignment.get_camera_seq() == cam2.seqno);
    CHECK(alignment.get_lidar_seq() == lid1.seqno);
    CHECK(alignment.get_has_radar());
    CHECK(alignment.get_radar_seq() == rad0.seqno);
    CHECK(alignment.get_pose_begin_seq() == pose0.seqno);
    CHECK(alignment.get_pose_end_seq() == pose3.seqno);
  }

  // --- Tick 1 at obs_time 3350ms ---
  now = jewels::time::SyncTime{3200ms};
  auto tick1 =
    inputs.get_tick().publish([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3350ms)); }, now);
  REQUIRE(test_cog.execute(now));

  // --- Alignment 1 ---
  //   tick=1, camera=4, lidar=2, radar=present(1), pose=[4,7]
  {
    const auto& alignment = test_cog.get_outputs().get_alignment().get_next_message();
    CHECK(alignment.get_tick_seq() == tick1.seqno);
    CHECK(alignment.get_camera_seq() == cam4.seqno);
    CHECK(alignment.get_lidar_seq() == lid2.seqno);
    CHECK(alignment.get_has_radar());
    CHECK(alignment.get_radar_seq() == rad1.seqno);
    CHECK(alignment.get_pose_begin_seq() == pose4.seqno);
    CHECK(alignment.get_pose_end_seq() == pose7.seqno);
  }

  // --- Tick 2 at obs_time 3450ms ---
  // Radar cursor is exhausted → absent alignment. The aligner arms a timeout
  // timer on the first execute and publishes on the second execute after the
  // timer fires.
  now = jewels::time::SyncTime{3300ms};
  auto tick2 =
    inputs.get_tick().publish([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3450ms)); }, now);
  REQUIRE(test_cog.execute(now));

  // Advance past the optional timeout deadline (200ms) so the timer fires.
  now = jewels::time::SyncTime{3600ms};
  REQUIRE(test_cog.execute(now));

  // --- Alignment 2 ---
  //   tick=2, camera=5, lidar=2, radar=absent, pose=[8,9]
  {
    const auto& alignment = test_cog.get_outputs().get_alignment().get_next_message();
    CHECK(alignment.get_tick_seq() == tick2.seqno);
    CHECK(alignment.get_camera_seq() == cam5.seqno);
    CHECK(alignment.get_lidar_seq() == lid2.seqno);
    CHECK_FALSE(alignment.get_has_radar());
    CHECK(alignment.get_pose_begin_seq() == pose8.seqno);
    CHECK(alignment.get_pose_end_seq() == pose9.seqno);
  }
}

TEST_CASE("Aligner unit test - maximum-size batch may fill the input view")
{
  using namespace std::chrono_literals;

  ExactBatchAlignerTestWrapper test_cog;
  auto now = jewels::time::SyncTime{1000ms};
  test_cog.initialize(now);

  auto inputs = test_cog.get_inputs();
  auto first = publish_sensor(inputs.get_samples(), 1000ms, now);
  publish_sensor(inputs.get_samples(), 1010ms, now);
  publish_sensor(inputs.get_samples(), 1020ms, now);
  auto last = publish_sensor(inputs.get_samples(), 1030ms, now);

  REQUIRE(test_cog.execute(now));

  const auto& alignment = test_cog.get_outputs().get_alignment().get_next_message();
  CHECK(alignment.get_samples_begin_seq() == first.seqno);
  CHECK(alignment.get_samples_end_seq() == last.seqno);
}

TEST_CASE("Aligner unit test - NEAREST search can use an optional reference input")
{
  using namespace std::chrono_literals;

  OptionalReferenceNearestWrapper test_cog;
  auto now = jewels::time::SyncTime{1000ms};
  test_cog.initialize(now);

  auto inputs = test_cog.get_inputs();

  auto anchor = publish_sensor(inputs.get_anchor(), 1000ms, now);
  publish_sensor(inputs.get_target(), 900ms, now);
  auto target_nearest = publish_sensor(inputs.get_target(), 980ms, now);
  publish_sensor(inputs.get_target(), 1040ms, now);
  auto tick = publish_sensor(inputs.get_tick(), 1000ms, now);

  REQUIRE(test_cog.execute(now));

  const auto& alignment = test_cog.get_outputs().get_alignment().get_next_message();
  CHECK(alignment.get_tick_seq() == tick.seqno);
  CHECK(alignment.get_has_anchor());
  CHECK(alignment.get_anchor_seq() == anchor.seqno);
  CHECK_FALSE(alignment.get_has_fallback_anchor());
  CHECK(alignment.get_has_target());
  CHECK(alignment.get_target_seq() == target_nearest.seqno);
}

TEST_CASE("Aligner unit test - NEAREST search uses a later optional reference when the first reference is absent")
{
  using namespace std::chrono_literals;

  OptionalReferenceNearestWrapper test_cog;
  auto now = jewels::time::SyncTime{1000ms};
  test_cog.initialize(now);

  auto inputs = test_cog.get_inputs();

  auto fallback_anchor = publish_sensor(inputs.get_fallback_anchor(), 1000ms, now);
  publish_sensor(inputs.get_target(), 900ms, now);
  auto target_nearest = publish_sensor(inputs.get_target(), 980ms, now);
  publish_sensor(inputs.get_target(), 1040ms, now);
  auto tick = publish_sensor(inputs.get_tick(), 1000ms, now);

  REQUIRE(test_cog.execute(now));

  const auto& alignment = test_cog.get_outputs().get_alignment().get_next_message();
  CHECK(alignment.get_tick_seq() == tick.seqno);
  CHECK_FALSE(alignment.get_has_anchor());
  CHECK(alignment.get_has_fallback_anchor());
  CHECK(alignment.get_fallback_anchor_seq() == fallback_anchor.seqno);
  CHECK(alignment.get_has_target());
  CHECK(alignment.get_target_seq() == target_nearest.seqno);
}

TEST_CASE("Aligner unit test - NEAREST search handles an absent optional reference input")
{
  using namespace std::chrono_literals;

  OptionalReferenceNearestWrapper test_cog;
  auto now = jewels::time::SyncTime{1000ms};
  test_cog.initialize(now);

  auto inputs = test_cog.get_inputs();

  auto target = publish_sensor(inputs.get_target(), 980ms, now);
  publish_sensor(inputs.get_target(), 1040ms, now);
  auto tick = publish_sensor(inputs.get_tick(), 1000ms, now);

  REQUIRE(test_cog.execute(now));

  const auto& alignment = test_cog.get_outputs().get_alignment().get_next_message();
  CHECK(alignment.get_tick_seq() == tick.seqno);
  CHECK_FALSE(alignment.get_has_anchor());
  CHECK_FALSE(alignment.get_has_fallback_anchor());
  CHECK(alignment.get_has_target());
  CHECK(alignment.get_target_seq() == target.seqno);
}

// Unit test for optional input constraint rejection.
// Mirrors CSR test case 2: radar is outside constraint range → absent.
TEST_CASE("Aligner unit test - optional input out of constraint range falls back to absent")
{
  using namespace std::chrono_literals;

  TestWrapper test_cog;
  auto now = jewels::time::SyncTime{3000ms};
  test_cog.initialize(now);

  auto inputs = test_cog.get_inputs();

  // Camera and lidar at 3200ms.
  auto cam0 = publish_sensor(inputs.get_camera(), 3200ms, now);
  auto lid0 = publish_sensor(inputs.get_lidar(), 3200ms, now);

  // Radar at 3500ms: |3200-3500| = 300ms > 150ms → outside constraint window.
  publish_sensor(inputs.get_radar(), 3500ms, now);

  // Pose: two messages near the tick, satisfying the minimum batch_size of 2.
  auto pose0 = publish_sensor(inputs.get_pose(), 3100ms, now);
  auto pose1 = publish_sensor(inputs.get_pose(), 3200ms, now);

  // Tick at obs_time 3200ms.
  // Radar is outside constraint range → absent. The aligner arms a timeout
  // timer on the first execute and publishes on the second execute after the
  // timer fires.
  now = jewels::time::SyncTime{3100ms};
  auto tick0 =
    inputs.get_tick().publish([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3200ms)); }, now);
  REQUIRE(test_cog.execute(now));

  // Advance past the optional timeout deadline (200ms) so the timer fires.
  now = jewels::time::SyncTime{3400ms};
  REQUIRE(test_cog.execute(now));

  {
    const auto& alignment = test_cog.get_outputs().get_alignment().get_next_message();
    CHECK(alignment.get_tick_seq() == tick0.seqno);
    CHECK(alignment.get_camera_seq() == cam0.seqno);
    CHECK(alignment.get_lidar_seq() == lid0.seqno);
    // Radar data exists but is outside constraint range → absent.
    CHECK_FALSE(alignment.get_has_radar());
    // Batch of 2 poses (minimum batch_size).
    CHECK(alignment.get_pose_begin_seq() == pose0.seqno);
    CHECK(alignment.get_pose_end_seq() == pose1.seqno);
  }
}

TEST_CASE("Aligner unit test - stale optional timeout does not publish the next partial alignment")
{
  using namespace std::chrono_literals;

  TestWrapper test_cog;
  auto now = jewels::time::SyncTime{1000ms};
  test_cog.initialize(now);

  auto inputs = test_cog.get_inputs();

  // 1. Publish a complete candidate except for the timeout optional.
  auto cam0 = publish_sensor(inputs.get_camera(), 1000ms, now);
  auto lid0 = publish_sensor(inputs.get_lidar(), 1000ms, now);
  auto pose0 = publish_sensor(inputs.get_pose(), 900ms, now);
  auto pose1 = publish_sensor(inputs.get_pose(), 1000ms, now);
  auto tick0 = publish_sensor(inputs.get_tick(), 1000ms, now);

  // 2. Execute once to arm the optional wait timer without publishing.
  const auto wait_start = jewels::time::SyncTime{2000ms};
  REQUIRE(test_cog.execute(wait_start));
  CHECK_FALSE(test_cog.get_outputs().get_alignment().try_get_next_message().has_value());

  // 3. Make the pending candidate complete before the timeout deadline.
  now = wait_start + 100ms;
  auto rad0 = publish_sensor(inputs.get_radar(), 1000ms, now);

  // 4. Snapshot conditions just before timeout, then fire the timer before execution.
  auto& unit_test_cog = test_cog.get_unit_test_cog();
  const auto timeout_deadline = wait_start + 200ms;
  auto throttled_until = jewels::time::SyncTime::min();
  REQUIRE(jewels::ok(unit_test_cog.prepare_for_execution(jewels::Out{throttled_until}, timeout_deadline - 1ns)));
  unit_test_cog.notify_expired_timers_for_test(timeout_deadline);
  REQUIRE(unit_test_cog.execute(CogExecuteParams{.start_time = timeout_deadline + 1ns}));

  // 5. The first execution publishes the complete alignment with the optional present.
  {
    const auto& alignment = test_cog.get_outputs().get_alignment().get_next_message();
    CHECK(alignment.get_tick_seq() == tick0.seqno);
    CHECK(alignment.get_camera_seq() == cam0.seqno);
    CHECK(alignment.get_lidar_seq() == lid0.seqno);
    CHECK(alignment.get_has_radar());
    CHECK(alignment.get_radar_seq() == rad0.seqno);
    CHECK(alignment.get_pose_begin_seq() == pose0.seqno);
    CHECK(alignment.get_pose_end_seq() == pose1.seqno);
  }

  // 6. Publish the next candidate without the timeout optional.
  now = timeout_deadline + 2ms;
  auto cam1 = publish_sensor(inputs.get_camera(), 1200ms, now);
  auto lid1 = publish_sensor(inputs.get_lidar(), 1200ms, now);
  auto pose2 = publish_sensor(inputs.get_pose(), 1100ms, now);
  auto pose3 = publish_sensor(inputs.get_pose(), 1200ms, now);
  auto tick1 = publish_sensor(inputs.get_tick(), 1200ms, now);

  // 7. The stale timer must not publish this new partial alignment immediately.
  const auto next_execute_time = timeout_deadline + 10ms;
  REQUIRE(test_cog.execute(next_execute_time));
  CHECK_FALSE(test_cog.get_outputs().get_alignment().try_get_next_message().has_value());

  // 8. Once this candidate's own timeout expires, it may publish without the optional.
  REQUIRE(test_cog.execute(next_execute_time + 201ms));

  {
    const auto& alignment = test_cog.get_outputs().get_alignment().get_next_message();
    CHECK(alignment.get_tick_seq() == tick1.seqno);
    CHECK(alignment.get_camera_seq() == cam1.seqno);
    CHECK(alignment.get_lidar_seq() == lid1.seqno);
    CHECK_FALSE(alignment.get_has_radar());
    CHECK(alignment.get_pose_begin_seq() == pose2.seqno);
    CHECK(alignment.get_pose_end_seq() == pose3.seqno);
  }
}

// End-to-end test for ``is_non_decreasing`` on a NEAREST search target.
// Feeds duplicate camera timestamps (legal under is_non_decreasing, illegal
// under is_strictly_increasing) and verifies the aligner picks a valid
// nearest-match candidate for each alignment.
TEST_CASE("Aligner unit test - is_non_decreasing target tolerates duplicate timestamps")
{
  using namespace std::chrono_literals;

  TestWrapper test_cog;
  auto now = jewels::time::SyncTime{3000ms};
  test_cog.initialize(now);

  auto inputs = test_cog.get_inputs();

  // Camera with duplicate observation_times. ND legalizes the duplicates;
  // NEAREST over a duplicate run picks a candidate within the narrowed
  // window.
  publish_sensor(inputs.get_camera(), 3180ms, now);
  auto cam_dup_a = publish_sensor(inputs.get_camera(), 3200ms, now);
  auto cam_dup_b = publish_sensor(inputs.get_camera(), 3200ms, now);
  auto cam_dup_c = publish_sensor(inputs.get_camera(), 3200ms, now);
  publish_sensor(inputs.get_camera(), 3260ms, now);

  // Lidar reference at 3200ms — tied with the duplicate camera run.
  auto lid0 = publish_sensor(inputs.get_lidar(), 3200ms, now);

  // Radar within constraint range.
  auto rad0 = publish_sensor(inputs.get_radar(), 3200ms, now);

  // Minimal pose batch (2 messages).
  auto pose0 = publish_sensor(inputs.get_pose(), 3100ms, now);
  auto pose1 = publish_sensor(inputs.get_pose(), 3200ms, now);

  // Tick at obs_time 3200ms — lidar picks 3200ms, camera nearest is one of
  // the tied duplicates at 3200ms.
  now = jewels::time::SyncTime{3100ms};
  auto tick0 =
    inputs.get_tick().publish([](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3200ms)); }, now);
  REQUIRE(test_cog.execute(now));

  {
    const auto& alignment = test_cog.get_outputs().get_alignment().get_next_message();
    CHECK(alignment.get_tick_seq() == tick0.seqno);
    CHECK(alignment.get_lidar_seq() == lid0.seqno);
    // Camera match must be one of the three tied duplicates at 3200ms.
    const auto cam_seq = alignment.get_camera_seq();
    CHECK((cam_seq == cam_dup_a.seqno || cam_seq == cam_dup_b.seqno || cam_seq == cam_dup_c.seqno));
    CHECK(alignment.get_has_radar());
    CHECK(alignment.get_radar_seq() == rad0.seqno);
    CHECK(alignment.get_pose_begin_seq() == pose0.seqno);
    CHECK(alignment.get_pose_end_seq() == pose1.seqno);
  }
}

} // namespace clockwork::aligner_csr_test
