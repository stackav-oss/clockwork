// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/platform_diagnostics_config_clk_cc.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/common/simplelaunch_runner_config_clk_cc.hh"
#include "clockwork/diagnostics/report_clk_cc.hh"
#include "clockwork/diagnostics/report_definitions.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/channel_config_clk_cc.hh"
#include "clockwork/pinion/meta_channel_factory.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/simplelaunch/simplelaunch_runner.hh"
#include "jewels/simplelaunch/simplelaunch_status_clk_cc.hh"
#include "jewels/simplelaunch/v1/config.pb.h"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <google/protobuf/text_format.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace jewels::simplelaunch
{

TEST_CASE("SimplelaunchRunner")
{
  constexpr auto status_channel_name = "/simplelaunch_status";
  const auto status_channel_uuid = Uuid<clockwork::common::EndpointInstanceId>::random_uuid();
  constexpr auto num_slots = 3;
  clockwork::pinion::BufferLayout status_buffer_layout{
    .num_slots = num_slots,
    .message_size = sizeof(clockwork::Tappy<SimplelaunchRunnerStatus>),
    .is_published_once = false,
  };

  const auto runner_config_ptr = std::make_shared<clockwork::Tappy<SimplelaunchRunnerConfig>>();
  runner_config_ptr->get_underlying_host_name().set_truncate("HOST");
  auto& status_publish_endpoint = runner_config_ptr->get_mutable_status_publish_endpoint();
  status_publish_endpoint.set_process_id(Uuid<clockwork::common::ProcessInstanceId>::random_uuid());
  status_publish_endpoint.set_publisher_id(status_channel_uuid);
  status_publish_endpoint.get_mutable_buffer_layout().set_num_slots(status_buffer_layout.num_slots);
  status_publish_endpoint.get_mutable_buffer_layout().set_message_size(status_buffer_layout.message_size);
  status_publish_endpoint.get_mutable_buffer_layout().set_is_published_once(status_buffer_layout.is_published_once);
  status_publish_endpoint.set_num_subscribers(1);
  status_publish_endpoint.get_underlying_channel_name().set_truncate(status_channel_name);
  status_publish_endpoint.set_is_bulk_data(false);
  status_publish_endpoint.set_channel_type(clockwork::pinion::ChannelType::shared_memory);

  const memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const testing::TmpDirectoryGuard pinion_tmp_dir;
  const auto pinion_dir = pinion_tmp_dir.get_path().string();
  const testing::TmpDirectoryGuard logging_tmp_dir;
  const auto& logging_dir = logging_tmp_dir.get_path();
  const auto socket_ns = jewels::Uuid<void>::random_uuid().to_string();

  auto factory_result = clockwork::pinion::MetaChannelFactory::make(memory_resource, {}, {}, {}, socket_ns, pinion_dir);
  REQUIRE(factory_result);
  const auto factory_ptr = std::make_shared<clockwork::pinion::MetaChannelFactory>(std::move(factory_result).value());

  SECTION("Pre-launch failure")
  {
    ::jewels::simplelaunch::v1::Config config;

    SimplelaunchRunner runner{SimplelaunchRunner::CtorParams{
      .memory_resource = memory_resource,
      .config = config,
      .runner_config_ptr = runner_config_ptr,
      .channel_factory_ptr = factory_ptr,
      .pre_launch_results = {{"prelaunch1", true}, {"prelaunch2", false}},
      .logging_directory = logging_dir,
      .listen_host = "127.0.0.1",
      .listen_port = 0,
    }};

    REQUIRE(ok(runner.initialize()));

    auto status_subscriber_result =
      factory_ptr->open_subscriber(status_channel_uuid.to_string(), status_channel_name, status_buffer_layout, 1U);
    REQUIRE(status_subscriber_result);
    auto& status_subscriber = status_subscriber_result.value();

    runner.run_for(std::chrono::seconds(1));

    auto status_message_range_result =
      clockwork::pinion::to_message_range<const clockwork::Tappy<SimplelaunchRunnerStatus>>(
        status_subscriber->available());
    REQUIRE(status_message_range_result);
    REQUIRE_FALSE(status_message_range_result->empty());
    const auto& status_message = status_message_range_result->back();
    REQUIRE(status_message.get_host_name() == "HOST");
    REQUIRE(status_message.get_process_info().empty());
    REQUIRE(status_message.get_pre_launch_info().size() == 2);
    REQUIRE(status_message.get_pre_launch_info()[0].get_name() == "prelaunch1");
    REQUIRE(status_message.get_pre_launch_info()[0].get_succeeded());
    REQUIRE(status_message.get_pre_launch_info()[1].get_name() == "prelaunch2");
    REQUIRE_FALSE(status_message.get_pre_launch_info()[1].get_succeeded());
  }
}

} // namespace jewels::simplelaunch
