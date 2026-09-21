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

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
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

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Test code only
  return *reinterpret_cast<const MessageType*>(message_span.data());
}
} // namespace

TEST_CASE("Restore Integration Test - Tachyon Log-Based State Restoration with default fallback")
{
  const auto process_description_path = jewels::fix_clockwork_path(
    "clockwork/tests/support/"
    "clockwork.clockwork.tests.support.restore_log_state_default_system.restore_log_state_default_system.proc.tachyon");

  const auto channel_publisher_config_path = jewels::fix_clockwork_path(
    "clockwork/tests/support/"
    "restore_log_state_default_system.restore_log_state_default_system."
    "RestoreLogStateDefaultCpu_channel_publisher_"
    "config."
    "tachyon");

  const auto log_writer_config_path = jewels::fix_clockwork_path(
    "clockwork/tests/support/"
    "restore_log_state_default_system.restore_log_state_default_system."
    "RestoreLogStateDefaultCpu_telemetry_logger_"
    "config.tachyon");

  using namespace std::chrono_literals;

  constexpr auto start_time = jewels::time::SyncTime(1000ms);
  constexpr auto end_time = start_time + 1ms;

  auto config = MessageInjectorSystemRunnerConfig{
    .process_description_path = process_description_path,
    .start_time = start_time,
    .end_time = end_time,
    .channel_publisher_config = channel_publisher_config_path,
    .log_writer_config = log_writer_config_path};

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  auto runner = MessageInjectorSystemRunner::create(config, memory_resource);
  REQUIRE(runner);

  REQUIRE(runner->run());

  auto state_msg = runner->try_pop_message();
  REQUIRE(state_msg);
  const auto& state_snap_msg = as_message<Tappy<ExecState>>(*state_msg, "StateSnapshotChannel");
  CHECK(state_snap_msg.get_exec_count() == 0);

  auto config_msg = runner->try_pop_message();
  REQUIRE(config_msg);
  const auto& config_snap_msg = as_message<Tappy<SnapshotTestConfig>>(*config_msg, "ConfigSnapshotChannel");
  CHECK(config_snap_msg.get_multiplier() == 2);
}
} // namespace clockwork::testing
