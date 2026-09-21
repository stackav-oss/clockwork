// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/repr_iface.hh"
#include "clockwork/runners/channel_publisher.hh"
#include "clockwork/test_tools/clockwork_system_runner.hh"
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

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Required to expose message contents
  return *reinterpret_cast<const MessageType*>(message_span.data());
}
} // namespace

TEST_CASE("Serializable state snapshots restore before cog execution")
{
  const auto process_description_path = jewels::fix_clockwork_path(
    "clockwork/tests/support/"
    "clockwork.clockwork.tests.support.serializable_state_snapshot_system.serializable_state_snapshot_system.proc."
    "tachyon");

  const auto channel_publisher_config_path = jewels::fix_clockwork_path(
    "clockwork/tests/support/"
    "serializable_state_snapshot_system.serializable_state_snapshot_system.SerializableStateSnapshotCpu_channel_"
    "publisher_config.tachyon");

  const auto log_writer_config_path = jewels::fix_clockwork_path(
    "clockwork/tests/support/"
    "serializable_state_snapshot_system.serializable_state_snapshot_system.SerializableStateSnapshotCpu_telemetry_"
    "logger_config.tachyon");

  using namespace std::chrono_literals;
  const auto config = MessageInjectorSystemRunnerConfig{
    .process_description_path = process_description_path,
    .start_time = jewels::time::SyncTime(1000ms),
    .end_time = jewels::time::SyncTime(1010ms),
    .channel_publisher_config = channel_publisher_config_path,
    .log_writer_config = log_writer_config_path};
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  SECTION("snapshot and restore succeed")
  {
    auto runner = MessageInjectorSystemRunner::create(config, memory_resource);
    REQUIRE(runner);

    Tappy<SerializableStateSnapshot> snapshot;
    snapshot.set_value(42);
    runner->add_message(snapshot, config.start_time, "SerializableStateUpdateChannel");
    runner->add_message(snapshot, config.start_time, "SerializableStateRestoreChannel");

    REQUIRE(runner->run());

    bool saw_snapshot = false;
    bool saw_restored_state = false;
    while (auto message = runner->try_pop_message())
    {
      if (message->channel == "SerializableStateSnapshotChannel")
      {
        saw_snapshot |= as_message<Tappy<SerializableStateSnapshot>>(*message, message->channel).get_value() == 42;
      }
      if (message->channel == "SerializableStateObservedChannel")
      {
        saw_restored_state = as_message<Tappy<SerializableStateSnapshot>>(*message, message->channel).get_value() == 42;
      }
    }
    CHECK(saw_snapshot);
    CHECK(saw_restored_state);
  }

  SECTION("deserialization failure aborts initialization")
  {
    auto runner = MessageInjectorSystemRunner::create(config, memory_resource);
    REQUIRE(runner);

    Tappy<SerializableStateSnapshot> invalid_snapshot;
    invalid_snapshot.set_value(-1);
    runner->add_message(invalid_snapshot, config.start_time, "SerializableStateRestoreChannel");

    CHECK(!runner->run());
  }
}
} // namespace clockwork::testing
