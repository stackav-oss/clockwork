// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/include_common.hh"
#include "clockwork/dsl/composition/tests/support/aligned_consumer_aligner_clk_cc.hh"
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

namespace clockwork::aligned_consumer_csr_test
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
      "clockwork/dsl/composition/tests/support/"
      "clockwork.clockwork.dsl.composition.tests.support"
      ".aligned_consumer_system.aligned_consumer_system.proc.tachyon"),
    .channel_publisher_config = jewels::fix_clockwork_path(
      "clockwork/dsl/composition/tests/support/"
      "aligned_consumer_system.aligned_consumer_system"
      ".AlignedConsumerTestCpu_channel_publisher_config.tachyon"),
    .log_writer_config = jewels::fix_clockwork_path(
      "clockwork/dsl/composition/tests/support/"
      "aligned_consumer_system.aligned_consumer_system"
      ".AlignedConsumerTestCpu_telemetry_logger_config.tachyon"),
  };
}

using EchoMsgList = std::pmr::vector<jewels::memory::pmr_unique_ptr<Tappy<aligned_consumer_test::ConsumerEchoMsg>>>;

Tappy<aligned_consumer_test::TestMsg> make_msg(std::chrono::milliseconds observation_time_ms)
{
  Tappy<aligned_consumer_test::TestMsg> msg;
  msg.set_observation_time(jewels::time::SyncTime(observation_time_ms));
  return msg;
}

} // anonymous namespace

// Verify that a consumer cog with aligned_inputs correctly resolves
// alignment messages and sees the specific upstream messages the aligner selected.
//
// Topology:
//   SensorChannel ──→ aligner.sensor
//   CameraChannel ──→ aligner.camera     (reuse: true)
//   LidarChannel  ──→ aligner.lidar      (batch_size: [1, 5])
//   RadarChannel  ──→ aligner.radar      (optional, timeout: 200ms)
//                     aligner.alignment ──→ AlignmentChannel ──→ consumer.aligned
//   consumer.echo  ──→ EchoChannel (captured output)
//
// SimpleAligner requires:
//   |sensor.observation_time - camera.observation_time| <= 500ms
//   |sensor.observation_time - lidar.observation_time|  <= 500ms
//   |sensor.observation_time - radar.observation_time|  <= 500ms
//
// Injection timeline:
//   t+10ms:  sensor(obs=3100ms)
//   t+11ms:  lidar(obs=3110ms)
//   t+12ms:  lidar(obs=3120ms)
//   t+13ms:  radar(obs=3130ms)
//   t+20ms:  camera(obs=3150ms) → alignment 1: sensor(3100), camera(3150), lidar batch [3110,3120], radar(3130)
//   t+500ms: sensor(obs=3500ms)
//   t+501ms: lidar(obs=3510ms)
//   t+600ms: camera(obs=3550ms) → alignment 2: sensor(3500), camera reused(3150), lidar batch [3510], radar absent
//
// Expected: consumer executes twice. Echo 1 has lidar_count==2, has_radar=true.
// Echo 2 has lidar_count==1, has_radar=false (radar timed out), camera reused from alignment 1.
TEST_CASE("Aligned consumer CSR - consumer receives resolved aligned messages")
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

  // Alignment 1 inputs
  runner->add_message(make_msg(3100ms), start_time + 10ms, "SensorChannel");
  runner->add_message(make_msg(3110ms), start_time + 11ms, "LidarChannel");
  runner->add_message(make_msg(3120ms), start_time + 12ms, "LidarChannel");
  runner->add_message(make_msg(3130ms), start_time + 13ms, "RadarChannel");
  runner->add_message(make_msg(3150ms), start_time + 20ms, "CameraChannel");

  // Alignment 2 inputs — radar times out
  runner->add_message(make_msg(3500ms), start_time + 500ms, "SensorChannel");
  runner->add_message(make_msg(3510ms), start_time + 501ms, "LidarChannel");
  runner->add_message(make_msg(3550ms), start_time + 600ms, "CameraChannel");

  REQUIRE(runner->run());

  EchoMsgList echoes{memory_resource};
  REQUIRE(jewels::ok(runner->get_messages_from_channel("EchoChannel", jewels::Out{echoes})));

  REQUIRE(echoes.size() == 2);

  // Alignment 1: sensor(3100), camera(3150), lidar batch [3110,3120], radar present
  CHECK(echoes[0]->get_sensor_obs_time() == jewels::time::SyncTime(3100ms));
  CHECK(echoes[0]->get_camera_obs_time() == jewels::time::SyncTime(3150ms));
  CHECK(echoes[0]->get_lidar_count() == 2);
  CHECK(echoes[0]->get_has_radar());

  // Alignment 2: sensor(3500), camera reused (3150), lidar batch [3510], radar absent (timed out)
  CHECK(echoes[1]->get_sensor_obs_time() == jewels::time::SyncTime(3500ms));
  CHECK(echoes[1]->get_camera_obs_time() == jewels::time::SyncTime(3150ms));
  CHECK(echoes[1]->get_lidar_count() == 1);
  CHECK_FALSE(echoes[1]->get_has_radar());
}

} // namespace clockwork::aligned_consumer_csr_test
