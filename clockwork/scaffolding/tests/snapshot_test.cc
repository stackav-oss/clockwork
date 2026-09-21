// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/shm_channel.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/tests/support/tmp_shm_namespace.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/snapshots.hh"
#include "clockwork/scaffolding/tests/support/mock_casing.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/new_delete_memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <trompeloeil/catch2.hpp> // IWYU pragma: keep (registers catch2 as the error handler)
#include <trompeloeil/mock.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace clockwork::scaffolding
{
namespace
{

TEST_CASE("setup_snapshot_configs")
{
  const pinion::support::TmpShmNamespace tmp_namespace;
  auto channel_factory = tmp_namespace.make_factory();

  jewels::memory::NewDeleteMemoryResource memory(0, "snapshot_test_memres");
  const jewels::memory::MemoryResource memres{&memory};

  // Create endpoint IDs (use EndpointInstanceId as per schema)
  const auto state_endpoint_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto config_endpoint_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto state_snapshot_publisher_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto config_snapshot_publisher_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();

  // Create snapshot publisher channels
  constexpr size_t message_size = 256;
  constexpr uint64_t num_slots = 5;
  const pinion::BufferLayout layout{
    .num_slots = num_slots,
    .message_size = message_size,
    .is_published_once = false,
  };

  auto state_snapshot_channel =
    channel_factory.open_publisher(state_snapshot_publisher_id.to_string(), "/state_snapshot", layout, 1).value();
  auto config_snapshot_channel =
    channel_factory.open_publisher(config_snapshot_publisher_id.to_string(), "/config_snapshot", layout, 1).value();

  // Create snapshot configs
  std::vector<Tappy<common::SnapshotConfig>> snapshot_configs;

  // State snapshot (TakeSnapshots) - has interval set
  using namespace std::chrono_literals;
  snapshot_configs.emplace_back();
  snapshot_configs.back().set_endpoint_id(state_endpoint_id);
  snapshot_configs.back().set_snapshot_publisher_id(state_snapshot_publisher_id);
  snapshot_configs.back().set_interval(1ms);

  // Config snapshot (SnapshotOnce) - no interval or cycles
  snapshot_configs.emplace_back();
  snapshot_configs.back().set_endpoint_id(config_endpoint_id);
  snapshot_configs.back().set_snapshot_publisher_id(config_snapshot_publisher_id);

  MockCasing casing;

  SECTION("success case")
  {
    // Expect try_configure_snapshot to be called for both configs
    REQUIRE_CALL(casing, try_configure_snapshot(snapshot_configs[0]))
      .RETURN(AbstractCasing::SnapshotConfigResult::success);
    REQUIRE_CALL(casing, try_configure_snapshot(snapshot_configs[1]))
      .RETURN(AbstractCasing::SnapshotConfigResult::success);

    auto result = setup_snapshot_configs(snapshot_configs, casing);
    REQUIRE(jewels::ok(result));
  }

  SECTION("casing error")
  {
    REQUIRE_CALL(casing, try_configure_snapshot(snapshot_configs[0]))
      .RETURN(AbstractCasing::SnapshotConfigResult::endpoint_not_found);

    auto result = setup_snapshot_configs(snapshot_configs, casing);
    REQUIRE(jewels::fails(result));
  }
}

} // namespace
} // namespace clockwork::scaffolding
