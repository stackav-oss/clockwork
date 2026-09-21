// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/include_common.hh"
#include "clockwork/dsl/aligner/gen/tests/support/aligner_test_msgs_clk_cc.hh"
#include "clockwork/dsl/aligner/gen/tests/support/test_aligner_clk_cc.hh"
#include "clockwork/test_tools/clockwork_system_runner.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/utility/fix_clockwork_path.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <memory_resource>
#include <string>

namespace clockwork::aligner_csr_test
{

namespace
{

struct SystemPaths
{
  std::string process_description;
  std::string channel_publisher_config;
  std::string log_writer_config;
};

SystemPaths resolve_paths()
{
  return {
    .process_description = jewels::fix_clockwork_path(
      "clockwork/dsl/aligner/gen/tests/support/"
      "clockwork.clockwork.dsl.aligner.gen.tests.support"
      ".test_system.aligner_test_system.proc.tachyon"),
    .channel_publisher_config = jewels::fix_clockwork_path(
      "clockwork/dsl/aligner/gen/tests/support/"
      "test_system.aligner_test_system.TestCpu_channel_publisher_config.tachyon"),
    .log_writer_config = jewels::fix_clockwork_path(
      "clockwork/dsl/aligner/gen/tests/support/"
      "test_system.aligner_test_system.TestCpu_telemetry_logger_config.tachyon"),
  };
}

using AlignmentMsgList = std::pmr::vector<jewels::memory::pmr_unique_ptr<Tappy<TestAlignerAlignmentMsg>>>;

Tappy<TestSensorMsg> make_sensor_msg(std::chrono::milliseconds observation_time_ms)
{
  Tappy<TestSensorMsg> msg;
  msg.set_observation_time(jewels::time::SyncTime(observation_time_ms));
  return msg;
}

} // anonymous namespace

// Functional test exercising three search types plus batch and optional inputs:
//
//   Level 0 (outermost): pose_lo → FIRST_IN_RANGE (forward scan), batch boundary
//   Level 1:             pose_hi → LAST_IN_RANGE (reverse scan), batch boundary
//   Level 2:             lidar   → LAST_IN_RANGE (reverse scan), reuse (get_view)
//   Level 3:             camera  → NEAREST (bidirectional scan from lidar.obs_time)
//   Level 4:             tick    → ANY_MATCH (forward scan, constraint-filtered)
//   Level 5 (innermost): radar   → ANY_MATCH (forward scan), optional
//
// Constraints:
//   |tick-camera| <=100ms
//   |tick-lidar| <=200ms
//   |tick-radar| <=150ms  (only checked when radar is present)
//   |tick-min(pose)| <=300ms
//   batch_size(pose) in [2, 4]  (auto-generated IndexInView constraints)
//
// Objective: minimize(|camera - lidar|), maximize(lidar).
//
// Each tick injection triggers one cog execution and one alignment.
// Cameras and lidars are pre-loaded so camera NEAREST sees multiple candidates.
// Radar messages produce present/absent results across the three alignments.
// Pose messages are consumed in batches of 2-4 per alignment.
TEST_CASE("Aligner CSR - search type dispatch with NEAREST, LAST_IN_RANGE, reuse, optional, and batch")
{
  const auto paths = resolve_paths();

  using namespace std::chrono_literals;

  constexpr auto start_time = jewels::time::SyncTime(3000ms);
  constexpr auto end_time = start_time + 1000ms;

  auto config = MessageInjectorSystemRunnerConfig{
    .process_description_path = paths.process_description,
    .start_time = start_time,
    .end_time = end_time,
    .channel_publisher_config = paths.channel_publisher_config,
    .log_writer_config = paths.log_writer_config};

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  auto runner = MessageInjectorSystemRunner::create(config, memory_resource);
  REQUIRE(runner);

  // --- Inject camera messages (NEAREST from lidar ref) ---
  //
  // 6 cameras injected early, all present before any tick arrives.
  // NEAREST from lidar(3380) will pick the closest valid camera per alignment.
  //
  //   c0: 3100ms  c1: 3150ms  c2: 3250ms  c3: 3340ms  c4: 3390ms  c5: 3450ms
  runner->add_message(make_sensor_msg(3100ms), start_time + 10ms, "CameraChannel");
  runner->add_message(make_sensor_msg(3150ms), start_time + 11ms, "CameraChannel");
  runner->add_message(make_sensor_msg(3250ms), start_time + 12ms, "CameraChannel");
  runner->add_message(make_sensor_msg(3340ms), start_time + 13ms, "CameraChannel");
  runner->add_message(make_sensor_msg(3390ms), start_time + 14ms, "CameraChannel");
  runner->add_message(make_sensor_msg(3450ms), start_time + 15ms, "CameraChannel");

  // --- Inject lidar messages (LAST_IN_RANGE, reuse) ---
  //
  //   l0: 3050ms  l1: 3240ms  l2: 3380ms
  runner->add_message(make_sensor_msg(3050ms), start_time + 20ms, "LidarChannel");
  runner->add_message(make_sensor_msg(3240ms), start_time + 21ms, "LidarChannel");
  runner->add_message(make_sensor_msg(3380ms), start_time + 22ms, "LidarChannel");

  // --- Inject radar messages (optional, ANY_MATCH with constraints) ---
  //
  //   r0: 3180ms → |t0-r0|=20ms ≤ 150ms (within range for t0 at 3200ms)
  //   r1: 3330ms → |t1-r1|=20ms ≤ 150ms (within range for t1 at 3350ms)
  //
  // After alignment 0 consumes r0, cursor advances past it.
  // After alignment 1 consumes r1, cursor advances past it.
  // Alignment 2 has no radar in cursor_view → absent.
  runner->add_message(make_sensor_msg(3180ms), start_time + 25ms, "RadarChannel");
  runner->add_message(make_sensor_msg(3330ms), start_time + 26ms, "RadarChannel");

  // --- Inject pose messages (batch input, batch_size [2, 4]) ---
  //
  // 10 pose messages spread across the time range.  Each alignment consumes
  // a contiguous batch of 2-4 messages (FIRST_IN_RANGE picks the start,
  // LAST_IN_RANGE picks the end within the batch_size window).
  //
  //   p0: 3050ms  p1: 3100ms  p2: 3150ms  p3: 3200ms
  //   p4: 3250ms  p5: 3300ms  p6: 3350ms  p7: 3400ms
  //   p8: 3450ms  p9: 3500ms
  runner->add_message(make_sensor_msg(3050ms), start_time + 30ms, "PoseChannel");
  runner->add_message(make_sensor_msg(3100ms), start_time + 31ms, "PoseChannel");
  runner->add_message(make_sensor_msg(3150ms), start_time + 32ms, "PoseChannel");
  runner->add_message(make_sensor_msg(3200ms), start_time + 33ms, "PoseChannel");
  runner->add_message(make_sensor_msg(3250ms), start_time + 34ms, "PoseChannel");
  runner->add_message(make_sensor_msg(3300ms), start_time + 35ms, "PoseChannel");
  runner->add_message(make_sensor_msg(3350ms), start_time + 36ms, "PoseChannel");
  runner->add_message(make_sensor_msg(3400ms), start_time + 37ms, "PoseChannel");
  runner->add_message(make_sensor_msg(3450ms), start_time + 38ms, "PoseChannel");
  runner->add_message(make_sensor_msg(3500ms), start_time + 39ms, "PoseChannel");

  // --- Inject tick messages ---
  //
  // Each tick injected at a different time to trigger a separate cog execution.
  //
  //   t0: 3200ms  t1: 3350ms  t2: 3450ms
  runner->add_message(make_sensor_msg(3200ms), start_time + 100ms, "TickChannel");
  runner->add_message(make_sensor_msg(3350ms), start_time + 200ms, "TickChannel");
  runner->add_message(make_sensor_msg(3450ms), start_time + 300ms, "TickChannel");

  REQUIRE(runner->run());

  AlignmentMsgList alignments{memory_resource};
  REQUIRE(jewels::ok(runner->get_messages_from_channel("AlignmentOutputChannel", jewels::Out{alignments})));
  REQUIRE(alignments.size() == 3);

  // --- Alignment 0 (t0 at 3200ms) ---
  //
  //   pose FIRST_IN_RANGE: p0(3050) — first in cursor_view.
  //   pose LAST_IN_RANGE: p3(3200) — latest within batch_size 4 of p0.
  //     Batch = [p0, p1, p2, p3], size 4 (maximum).
  //
  //   lidar LAST_IN_RANGE + BSF objective tie-breaking:
  //     BSF scans all lidar candidates (l2→l1→l0).
  //     l2(3380): camera NEAREST → c2(3250, dist=130ms). |t0-c2|=50ms → MATCH.
  //     l1(3240): camera NEAREST → c2(3250, dist=10ms). |t0-c2|=50ms → MATCH.
  //     Objective minimize(|camera-lidar|): l1 has 10ms vs l2's 130ms → l1 wins.
  //     l0(3050): camera NEAREST → c0(3100, dist=50ms). |camera-lidar|=50ms,
  //       still worse than l1's 10ms → l1 remains best.
  //
  //   camera NEAREST from lidar(3240): binary search in [c0..c5].
  //     c2(3250) is closest feasible at dist=10ms from l1.
  //
  //   Proves NEAREST: c0(dist=140) and c1(dist=90) also pass all constraints,
  //   but c2(dist=10) is closer to the lidar reference.
  //
  //   Proves objective tie-breaking: l2 (latest lidar) would be selected by
  //   LAST_IN_RANGE alone, but minimize(|camera-lidar|) prefers l1 where
  //   camera c2 is much closer.
  //
  //   Proves batch: 4 contiguous pose messages consumed (max batch_size).
  //
  //   Result: tick=0, camera=2, lidar=1, radar=present(0),
  //           pose=[0..3] — p0 through p3.
  {
    const auto& alignment = *alignments[0];
    CHECK(alignment.get_tick_seq() == 0);
    CHECK(alignment.get_camera_seq() == 2);
    CHECK(alignment.get_lidar_seq() == 1);
    CHECK(alignment.get_has_radar());
    CHECK(alignment.get_radar_seq() == 0);
    CHECK(alignment.get_pose_begin_seq() == 0);
    CHECK(alignment.get_pose_end_seq() == 3);
  }

  // --- Alignment 1 (t1 at 3350ms) ---
  //
  //   Camera cursor advanced past c2; cursor_view = [c3, c4, c5].
  //   Lidar reuse: all lidars still in view via get_view().
  //   Radar cursor advanced past r0; cursor_view = [r1].
  //   Pose cursor advanced past p3; cursor_view = [p4, p5, p6, p7, p8, p9].
  //
  //   pose FIRST_IN_RANGE: p4(3250) — first in cursor_view.
  //   pose LAST_IN_RANGE: p7(3400) — latest within batch_size 4 of p4.
  //     Batch = [p4, p5, p6, p7], size 4 (maximum).
  //
  //   lidar LAST_IN_RANGE: l2(3380) again.
  //
  //   camera NEAREST from lidar(3380): cursor_view = [c3(3340), c4(3390), c5(3450)].
  //     Tries c4(3390,dist=10) → tick: |t1-c4|=40 ≤ 100ms, |t1-l2|=30 ≤ 200ms,
  //       |t1-p4|=100 ≤ 300ms → MATCH.
  //       → radar level: tries r1(3330). |t1-r1|=20 ≤ 150ms → radar PRESENT.
  //
  //   Proves NEAREST again: c3(dist=40) also passes constraints (|t1-c3|=10),
  //   but c4(dist=10) is closer to the lidar reference. Forward scan would pick
  //   c3 first. NEAREST selects c4.
  //
  //   Proves objective tie-breaking: l2 wins here because c4 is already very
  //   close (10ms), so minimize(|camera-lidar|) doesn't change the result.
  //
  //   Also proves reuse: l2 is still available despite cursor advance after A0.
  //
  //   Result: tick=1, camera=4, lidar=2, radar=present(1),
  //           pose=[4..7] — p4 through p7.
  {
    const auto& alignment = *alignments[1];
    CHECK(alignment.get_tick_seq() == 1);
    CHECK(alignment.get_camera_seq() == 4);
    CHECK(alignment.get_lidar_seq() == 2);
    CHECK(alignment.get_has_radar());
    CHECK(alignment.get_radar_seq() == 1);
    CHECK(alignment.get_pose_begin_seq() == 4);
    CHECK(alignment.get_pose_end_seq() == 7);
  }

  // --- Alignment 2 (t2 at 3450ms) ---
  //
  //   Camera cursor past c4; cursor_view = [c5].
  //   Radar cursor past r1; cursor_view = [].
  //   Pose cursor past p7; cursor_view = [p8, p9].
  //
  //   pose FIRST_IN_RANGE: p8(3450) — first in cursor_view.
  //   pose LAST_IN_RANGE: p9(3500) — only 2 poses left, matches min batch_size.
  //     Batch = [p8, p9], size 2 (minimum).
  //
  //   lidar LAST_IN_RANGE: l2(3380).
  //   camera NEAREST: only c5(3450). |t2-c5|=0, |t2-l2|=70,
  //     |t2-p8|=0, all within windows → MATCH.
  //     → radar level: cursor_view empty → radar ABSENT.
  //
  //   Proves LAST_IN_RANGE for alignments 1 and 2: l2 (latest) is selected
  //   because the camera available in cursor_view is already close to l2,
  //   so objective tie-breaking doesn't override the LAST_IN_RANGE preference.
  //   (Alignment 0 above demonstrates the case where objectives DO override.)
  //
  //   Proves optional absent: radar data was injected but all consumed;
  //   no radar in cursor_view → alignment proceeds without it.
  //
  //   Proves batch minimum: only 2 poses in cursor_view, which is the minimum
  //   batch_size. The alignment succeeds with the minimum batch.
  //
  //   Result: tick=2, camera=5, lidar=2, radar=absent,
  //           pose=[8..9] — p8 and p9.
  {
    const auto& alignment = *alignments[2];
    CHECK(alignment.get_tick_seq() == 2);
    CHECK(alignment.get_camera_seq() == 5);
    CHECK(alignment.get_lidar_seq() == 2);
    CHECK_FALSE(alignment.get_has_radar());
    CHECK(alignment.get_pose_begin_seq() == 8);
    CHECK(alignment.get_pose_end_seq() == 9);
  }
}

// Functional test for optional input constraint rejection.
//
// Radar messages are injected but all are outside the |tick-radar| <= 150ms
// constraint window.  The present path exhausts all candidates (none pass
// constraints), falls through to the absent path, and the alignment
// succeeds without radar.
TEST_CASE("Aligner CSR - optional input out of constraint range falls back to absent")
{
  const auto paths = resolve_paths();

  using namespace std::chrono_literals;

  constexpr auto start_time = jewels::time::SyncTime(3000ms);
  constexpr auto end_time = start_time + 1000ms;

  auto config = MessageInjectorSystemRunnerConfig{
    .process_description_path = paths.process_description,
    .start_time = start_time,
    .end_time = end_time,
    .channel_publisher_config = paths.channel_publisher_config,
    .log_writer_config = paths.log_writer_config};

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  auto runner = MessageInjectorSystemRunner::create(config, memory_resource);
  REQUIRE(runner);

  // Camera and lidar at 3200ms for simple alignment.
  runner->add_message(make_sensor_msg(3200ms), start_time + 10ms, "CameraChannel");
  runner->add_message(make_sensor_msg(3200ms), start_time + 20ms, "LidarChannel");

  // Radar at 3500ms: |3200 - 3500| = 300ms > 150ms → outside constraint window.
  runner->add_message(make_sensor_msg(3500ms), start_time + 25ms, "RadarChannel");

  // Pose: two messages near the tick, satisfying the minimum batch_size of 2.
  runner->add_message(make_sensor_msg(3100ms), start_time + 30ms, "PoseChannel");
  runner->add_message(make_sensor_msg(3200ms), start_time + 31ms, "PoseChannel");

  // Tick at 3200ms.
  runner->add_message(make_sensor_msg(3200ms), start_time + 100ms, "TickChannel");

  REQUIRE(runner->run());

  AlignmentMsgList alignments{memory_resource};
  REQUIRE(jewels::ok(runner->get_messages_from_channel("AlignmentOutputChannel", jewels::Out{alignments})));
  REQUIRE(alignments.size() == 1);

  {
    const auto& alignment = *alignments[0];
    CHECK(alignment.get_tick_seq() == 0);
    CHECK(alignment.get_camera_seq() == 0);
    CHECK(alignment.get_lidar_seq() == 0);
    // Radar data exists but is outside constraint range → absent.
    CHECK_FALSE(alignment.get_has_radar());
    // Batch of 2 poses (minimum batch_size).
    CHECK(alignment.get_pose_begin_seq() == 0);
    CHECK(alignment.get_pose_end_seq() == 1);
  }
}

} // namespace clockwork::aligner_csr_test
