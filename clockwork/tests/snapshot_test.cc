// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/repr_iface.hh"
#include "clockwork/runners/channel_publisher.hh"
#include "clockwork/test_tools/clockwork_system_runner.hh"
#include "clockwork/tests/support/exec_time.hh"
#include "clockwork/tests/support/snapshot_test_messages.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/utility/fix_clockwork_path.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork::testing
{
namespace
{
/// Utility function for converting MultiMessageInfoData into a tachyon message.
template <typename MessageType>
const MessageType& as_message(MultiMessageInfoData& message_info, std::string_view expected_channel)
{
  REQUIRE(message_info.channel == expected_channel);
  REQUIRE_FALSE(message_info.msgs.empty());
  auto& data = message_info.msgs.front();

  auto message_span = std::span<const std::byte>(data);
  REQUIRE(message_span.size_bytes() >= sizeof(MessageType));

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Required to expose message contents
  return *reinterpret_cast<const MessageType*>(message_span.data());
}
} // namespace

TEST_CASE("Snapshot Integration Test")
{
  const auto process_description_path = jewels::fix_clockwork_path(
    "clockwork/tests/support/"
    "clockwork.clockwork.tests.support.snapshot_test_system.snapshot_test_system.proc.tachyon");

  const auto channel_publisher_config_path = jewels::fix_clockwork_path(
    "clockwork/tests/support/"
    "snapshot_test_system.snapshot_test_system.SnapshotTestCpu_channel_publisher_config.tachyon");

  const auto log_writer_config_path = jewels::fix_clockwork_path(
    "clockwork/tests/support/"
    "snapshot_test_system.snapshot_test_system.SnapshotTestCpu_telemetry_logger_config.tachyon");

  using namespace std::chrono_literals;

  constexpr auto start_time = jewels::time::SyncTime(1000ms);
  constexpr auto end_time = start_time + 450ms;

  auto config = MessageInjectorSystemRunnerConfig{
    .process_description_path = process_description_path,
    .start_time = start_time,
    .end_time = end_time,
    .channel_publisher_config = channel_publisher_config_path,
    .log_writer_config = log_writer_config_path};

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  auto runner = MessageInjectorSystemRunner::create(config, memory_resource);
  REQUIRE(runner);

  const std::string update_channel = "StateUpdateChannel";

  // Inject updates at specific times to track state evolution
  Tappy<StateUpdateMessage> update1;
  update1.set_increment(100);
  runner->add_message(update1, start_time + 25ms, update_channel);

  Tappy<StateUpdateMessage> update2;
  update2.set_increment(200);
  runner->add_message(update2, start_time + 175ms, update_channel);

  REQUIRE(runner->run());

  // Collect all snapshots
  std::vector<int32_t> snapshot_counts;
  auto config_msg = runner->try_pop_message();
  REQUIRE(config_msg);
  const auto& config_snap_msg = as_message<Tappy<SnapshotTestConfig>>(*config_msg, "ConfigSnapshotChannel");
  CHECK(config_snap_msg.get_multiplier() == 2);

  while (auto msg = runner->try_pop_message())
  {
    const auto& state = as_message<Tappy<ExecState>>(*msg, "StateSnapshotChannel");
    snapshot_counts.push_back(state.get_exec_count());
  }

  // Should have 5 state snapshots
  CHECK(snapshot_counts.size() == 5);

  // Snapshots should show monotonically increasing count
  for (size_t i = 1; i < snapshot_counts.size(); ++i)
  {
    INFO("Snapshot " << i - 1 << ": " << snapshot_counts[i - 1]);
    INFO("Snapshot " << i << ": " << snapshot_counts[i]);
    REQUIRE(snapshot_counts[i] > snapshot_counts[i - 1]);
  }

  // Expected values explanation:
  // Cog executes periodically (50ms) and on message arrival. Snapshots taken after first execution >= 100ms interval.
  // t=1025ms: msg1 arrives (increment=100), exec: count=0+1+100*2=201, snapshot #1 (first execution)
  // t=1075ms: periodic exec: count=201+1=202
  // t=1125ms: periodic exec: count=202+1=203, snapshot #2
  // t=1175ms: msg2 arrives (increment=200), exec: count=203+1+200*2=604
  // t=1225ms: periodic exec: count=604+1=605, snapshot #3
  // t=1275ms: periodic exec: count=605+1=606
  // t=1325ms: periodic exec: count=606+1=607, snapshot #4
  // t=1375ms: periodic exec: count=607+1=608
  // t=1425ms: periodic exec: count=608+1=609, snapshot #5
  CHECK(snapshot_counts == std::vector{201, 203, 605, 607, 609});
}
} // namespace clockwork::testing
