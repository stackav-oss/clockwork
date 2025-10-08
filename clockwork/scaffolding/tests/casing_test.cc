// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description.hh"
#include "clockwork/diagnostics/report.hh"
#include "clockwork/diagnostics/report_definitions.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/shm_subscriber.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tests/support/pub_sub.hh"
#include "clockwork/pinion/tests/support/tmp_shm_namespace.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/casing.hh"
#include "clockwork/scaffolding/scaffolding.hh"
#include "clockwork/scaffolding/tests/support/runtime_tools.hh"
#include "clockwork/scaffolding/tests/support/test_cogs.hh"
#include "clockwork/scaffolding/tests/support/test_io_connections.hh"
#include "clockwork/scaffolding/tests/support/test_msgs.hh"
#include "clockwork/scaffolding/tests/support/test_msgs_proto.pb.h"
#include "clockwork/scaffolding/tests/support/test_msgs_proto_conv.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/uuid/uuid.hh"

#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_test_macros.hpp>
#include <gsl/util>
#include <xxh3.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

namespace clockwork::scaffolding
{
namespace
{
uint64_t fibonacci(uint64_t nth)
{
  uint64_t seq_n1 = 0;
  uint64_t seq_n0 = 1;
  for (size_t idx = 2; idx <= nth; idx++)
  {
    seq_n0 += std::exchange(seq_n1, seq_n0);
  }
  return seq_n0;
}

template <typename Tag>
std::shared_ptr<pinion::ShmSubscriber> make_snoop(
  pinion::ShmChannelFactory& factory, jewels::Uuid<Tag> chan_id, const common::PinionBufferLayoutTap& buffer_desc)
{
  const pinion::BufferLayout layout{
    .num_slots = buffer_desc.get_num_slots(),
    .message_size = buffer_desc.get_message_size(),
  };
  if (auto open = factory.open_subscriber(chan_id.to_string(), "snooper", layout, 0); open)
  {
    return *std::move(open);
  }
  return nullptr;
}

TEST_CASE("autocasing")
{

  using InitCog1Factory = testing::InitCog1Factory;
  using TestCog1Factory = testing::TestCog1Factory;
  using TestCog2Factory = testing::TestCog2Factory;
  using InitCog1Policy = testing::InitCog1Policy;
  using TestCog1Policy = testing::TestCog1Policy;
  using TestCog2Policy = testing::TestCog2Policy;

  const jewels::testing::TmpDirectoryGuard tmpdir;
  const pinion::support::TmpShmNamespace tmpchan;

  const auto proc1_id = jewels::Uuid<common::ProcessInstanceId>::random_uuid();
  const auto cog1_class = TestCog1Factory::type_id;
  const auto cog1_inst = jewels::Uuid<common::CogInstanceId>::random_uuid();
  const auto cog1_ep_config1 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog1_ep_timer1 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog1_ep_state1 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog1_ep_mem1 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog1_ep_mem2 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog1_ep_diag = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog2_class = TestCog2Factory::type_id;
  const auto cog2_inst = jewels::Uuid<common::CogInstanceId>::random_uuid();
  const auto cog2_ep_chan1a = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog2_ep_state1 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog2_ep_state2 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog2_ep_mem1 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog2_ep_diag = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog3_class = InitCog1Factory::type_id;
  const auto cog3_inst = jewels::Uuid<common::CogInstanceId>::random_uuid();
  const auto cog3_ep_config2 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog3_ep_state1 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog3_ep_state2 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto memres1_id = jewels::Uuid<common::MemoryResourceId>::random_uuid();
  const auto memres2_id = jewels::Uuid<common::MemoryResourceId>::random_uuid();
  const auto memres3_id = jewels::Uuid<common::MemoryResourceId>::random_uuid();
  const auto memres4_id = jewels::Uuid<common::MemoryResourceId>::random_uuid();
  const auto config1_path = tmpdir.get_path() / "config1.dat";
  const auto config1_id = jewels::Uuid<common::ConfigInstanceId>::random_uuid();
  const auto config2_path = tmpdir.get_path() / "config2.dat";
  const auto config2_id = jewels::Uuid<common::ConfigInstanceId>::random_uuid();
  const auto state1_id = jewels::Uuid<common::StateInstanceId>::random_uuid();
  const auto state2_id = jewels::Uuid<common::StateInstanceId>::random_uuid();
  const auto chan1_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto chan2_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();

  const auto configured_id = *jewels::Uuid<testing::ConfigId>::from_string("aae951e7-1219-4357-9e7d-e3caa0c64c3f");
  clockwork::testing::proto::Config config1_data;
  config1_data.set_id(configured_id.to_string());
  testing::write_schema(config1_path, config1_data);

  const auto config_group_id = jewels::Uuid<testing::GroupId>::random_uuid();
  Tap<Tachyon<testing::InitConfig>> config2_data;
  config2_data.set_group_id(config_group_id);
  testing::write_schema(config2_path, config2_data);

  common::ProcessDescriptionTap desc;
  auto& cog1_desc = desc.get_underlying_cog_instances().emplace_back();
  cog1_desc.set_cog_class_id(cog1_class);
  cog1_desc.set_cog_instance_id(cog1_inst);
  cog1_desc.get_underlying_instance_path_name().set_truncate("cog1");
  auto& cog1_epmap_1 = cog1_desc.get_underlying_endpoints().emplace_back();
  cog1_epmap_1.set_endpoint_class_id(TestCog1Policy::OutAPolicy::endpoint_id);
  cog1_epmap_1.set_endpoint_instance_id(chan1_id);
  auto& cog1_epmap_2 = cog1_desc.get_underlying_endpoints().emplace_back();
  cog1_epmap_2.set_endpoint_class_id(TestCog1Policy::PeriodicPolicy::endpoint_id);
  cog1_epmap_2.set_endpoint_instance_id(cog1_ep_timer1);
  auto& cog1_epmap_3 = cog1_desc.get_underlying_endpoints().emplace_back();
  cog1_epmap_3.set_endpoint_class_id(TestCog1Policy::CfgAPolicy::endpoint_id);
  cog1_epmap_3.set_endpoint_instance_id(cog1_ep_config1);
  auto& cog1_epmap_4 = cog1_desc.get_underlying_endpoints().emplace_back();
  cog1_epmap_4.set_endpoint_class_id(TestCog1Policy::StateAPolicy::endpoint_id);
  cog1_epmap_4.set_endpoint_instance_id(cog1_ep_state1);
  auto& cog1_epmap_5 = cog1_desc.get_underlying_endpoints().emplace_back();
  cog1_epmap_5.set_endpoint_class_id(TestCog1Policy::Memory1Policy::endpoint_id);
  cog1_epmap_5.set_endpoint_instance_id(cog1_ep_mem1);
  auto& cog1_epmap_6 = cog1_desc.get_underlying_endpoints().emplace_back();
  cog1_epmap_6.set_endpoint_class_id(TestCog1Policy::Memory2Policy::endpoint_id);
  cog1_epmap_6.set_endpoint_instance_id(cog1_ep_mem2);
  auto& cog1_epmap_7 = cog1_desc.get_underlying_endpoints().emplace_back();
  cog1_epmap_7.set_endpoint_class_id(TestCog1Policy::DiagnosticsPolicy::endpoint_id);
  cog1_epmap_7.set_endpoint_instance_id(cog1_ep_diag);

  auto& cog2_desc = desc.get_underlying_cog_instances().emplace_back();
  cog2_desc.set_cog_class_id(cog2_class);
  cog2_desc.set_cog_instance_id(cog2_inst);
  cog2_desc.get_underlying_instance_path_name().set_truncate("cog2");
  auto& cog2_epmap_1 = cog2_desc.get_underlying_endpoints().emplace_back();
  cog2_epmap_1.set_endpoint_class_id(TestCog2Policy::InAPolicy::endpoint_id);
  cog2_epmap_1.set_endpoint_instance_id(cog2_ep_chan1a);
  auto& cog2_epmap_2 = cog2_desc.get_underlying_endpoints().emplace_back();
  cog2_epmap_2.set_endpoint_class_id(TestCog2Policy::OutAPolicy::endpoint_id);
  cog2_epmap_2.set_endpoint_instance_id(chan2_id);
  auto& cog2_epmap_3 = cog2_desc.get_underlying_endpoints().emplace_back();
  cog2_epmap_3.set_endpoint_class_id(TestCog2Policy::StateAPolicy::endpoint_id);
  cog2_epmap_3.set_endpoint_instance_id(cog2_ep_state1);
  auto& cog2_epmap_4 = cog2_desc.get_underlying_endpoints().emplace_back();
  cog2_epmap_4.set_endpoint_class_id(TestCog2Policy::MemoryPolicy::endpoint_id);
  cog2_epmap_4.set_endpoint_instance_id(cog2_ep_mem1);
  auto& cog2_epmap_5 = cog2_desc.get_underlying_endpoints().emplace_back();
  cog2_epmap_5.set_endpoint_class_id(TestCog2Policy::FibPolicy::endpoint_id);
  cog2_epmap_5.set_endpoint_instance_id(cog2_ep_state2);
  auto& cog2_epmap_6 = cog2_desc.get_underlying_endpoints().emplace_back();
  cog2_epmap_6.set_endpoint_class_id(TestCog2Policy::DiagnosticsPolicy::endpoint_id);
  cog2_epmap_6.set_endpoint_instance_id(cog2_ep_diag);

  auto& cog3_desc = desc.get_underlying_cog_instances().emplace_back();
  cog3_desc.set_cog_class_id(cog3_class);
  cog3_desc.set_cog_instance_id(cog3_inst);
  cog3_desc.get_underlying_instance_path_name().set_truncate("init_cog1");
  auto& cog3_epmap_1 = cog3_desc.get_underlying_endpoints().emplace_back();
  cog3_epmap_1.set_endpoint_class_id(InitCog1Policy::ConfigPolicy::endpoint_id);
  cog3_epmap_1.set_endpoint_instance_id(cog3_ep_config2);
  auto& cog3_epmap_2 = cog3_desc.get_underlying_endpoints().emplace_back();
  cog3_epmap_2.set_endpoint_class_id(InitCog1Policy::StatePolicy::endpoint_id);
  cog3_epmap_2.set_endpoint_instance_id(cog3_ep_state1);
  auto& cog3_epmap_3 = cog3_desc.get_underlying_endpoints().emplace_back();
  cog3_epmap_3.set_endpoint_class_id(InitCog1Policy::FibPolicy::endpoint_id);
  cog3_epmap_3.set_endpoint_instance_id(cog3_ep_state2);

  auto& memres1_desc = desc.get_mutable_memory_resource_graph().get_underlying_memory_resources().emplace_back();
  memres1_desc.set_memory_resource_id(memres1_id);
  memres1_desc.get_underlying_instance_path_name().set_truncate("memres1");
  memres1_desc.set_resource_type(common::MemoryResourceType::new_delete);
  memres1_desc.set_resource_max_size(1);
  auto& memres2_desc = desc.get_mutable_memory_resource_graph().get_underlying_memory_resources().emplace_back();
  memres2_desc.set_memory_resource_id(memres2_id);
  memres2_desc.get_underlying_instance_path_name().set_truncate("memres2");
  memres2_desc.set_resource_type(common::MemoryResourceType::new_delete);
  memres2_desc.set_resource_max_size(1);
  auto& memres3_desc = desc.get_mutable_memory_resource_graph().get_underlying_memory_resources().emplace_back();
  memres3_desc.set_memory_resource_id(memres3_id);
  memres3_desc.get_underlying_instance_path_name().set_truncate("memres3");
  memres3_desc.set_resource_type(common::MemoryResourceType::new_delete);
  memres3_desc.set_resource_max_size(1);
  auto& memres4_desc = desc.get_mutable_memory_resource_graph().get_underlying_memory_resources().emplace_back();
  memres4_desc.set_memory_resource_id(memres4_id);
  memres4_desc.get_underlying_instance_path_name().set_truncate("memres4");
  memres4_desc.set_resource_type(common::MemoryResourceType::new_delete);
  memres4_desc.set_resource_max_size(1);

  auto& memres_conn1 = desc.get_mutable_memory_resource_graph().get_underlying_connections().emplace_back();
  memres_conn1.set_memory_resource_id(memres1_id);
  memres_conn1.set_endpoint_id(cog1_ep_mem1);
  auto& memres_conn2 = desc.get_mutable_memory_resource_graph().get_underlying_connections().emplace_back();
  memres_conn2.set_memory_resource_id(memres2_id);
  memres_conn2.set_endpoint_id(cog1_ep_mem2);
  auto& memres_conn3 = desc.get_mutable_memory_resource_graph().get_underlying_connections().emplace_back();
  memres_conn3.set_memory_resource_id(memres3_id);
  memres_conn3.set_endpoint_id(cog2_ep_mem1);

  auto& config1_desc = desc.get_mutable_config_graph().get_underlying_config_instances().emplace_back();
  config1_desc.set_representation_id(Tachyon<testing::Config>::_clockwork_uuid);
  config1_desc.get_mutable_config_instance_id() = config1_id;
  config1_desc.get_underlying_instance_path_name().set_truncate("config1");
  config1_desc.get_underlying_config_file_path().set_truncate((tmpdir.get_path() / config1_path).native());
  auto& config_conn_1 = desc.get_mutable_config_graph().get_underlying_connections().emplace_back();
  config_conn_1.get_mutable_config_id() = config1_id;
  config_conn_1.get_mutable_endpoint_id() = cog1_ep_config1;

  auto& config2_desc = desc.get_mutable_config_graph().get_underlying_config_instances().emplace_back();
  config2_desc.set_representation_id(Tachyon<testing::InitConfig>::_clockwork_uuid);
  config2_desc.get_mutable_config_instance_id() = config2_id;
  config2_desc.get_underlying_instance_path_name().set_truncate("config2");
  config2_desc.get_underlying_config_file_path().set_truncate((tmpdir.get_path() / config2_path).native());
  auto& config2_conn_1 = desc.get_mutable_config_graph().get_underlying_connections().emplace_back();
  config2_conn_1.get_mutable_config_id() = config2_id;
  config2_conn_1.get_mutable_endpoint_id() = cog3_ep_config2;

  auto& state1_desc = desc.get_mutable_state_graph().get_underlying_state_instances().emplace_back();
  state1_desc.set_representation_id(Tachyon<testing::State>::_clockwork_uuid);
  state1_desc.set_state_instance_id(state1_id);
  state1_desc.get_underlying_instance_path_name().set_truncate("state1");
  state1_desc.set_maybe_buffer_layout(
    {{.num_slots = 10, .message_size = sizeof(TestCog1Policy::StateAPolicy::StateType)}});
  auto& state_conn_1 = desc.get_mutable_state_graph().get_underlying_connections().emplace_back();
  state_conn_1.set_state_id(state1_id);
  state_conn_1.set_endpoint_id(cog1_ep_state1);
  auto& state_conn_2 = desc.get_mutable_state_graph().get_underlying_connections().emplace_back();
  state_conn_2.set_state_id(state1_id);
  state_conn_2.set_endpoint_id(cog2_ep_state1);
  auto& state_conn_3 = desc.get_mutable_state_graph().get_underlying_connections().emplace_back();
  state_conn_3.set_state_id(state1_id);
  state_conn_3.set_endpoint_id(cog3_ep_state1);
  auto& state2_desc = desc.get_mutable_state_graph().get_underlying_state_instances().emplace_back();
  state2_desc.set_representation_id(InitCog1Policy::FibPolicy::Factory().id());
  state2_desc.set_state_instance_id(state2_id);
  state2_desc.get_underlying_instance_path_name().set_truncate("state2");
  state2_desc.set_maybe_memory_resource(memres4_id);
  auto& state_conn_4 = desc.get_mutable_state_graph().get_underlying_connections().emplace_back();
  state_conn_4.set_state_id(state2_id);
  state_conn_4.set_endpoint_id(cog2_ep_state2);
  auto& state_conn_5 = desc.get_mutable_state_graph().get_underlying_connections().emplace_back();
  state_conn_5.set_state_id(state2_id);
  state_conn_5.set_endpoint_id(cog3_ep_state2);

  auto& chan1_desc = desc.get_mutable_pubsub_graph().get_underlying_publish_endpoints().emplace_back();
  chan1_desc.set_process_id(proc1_id);
  chan1_desc.set_publisher_id(chan1_id);
  chan1_desc.get_mutable_buffer_layout().set_num_slots(10);
  chan1_desc.get_mutable_buffer_layout().set_message_size(sizeof(TestCog1Policy::OutAPolicy::MsgType));
  chan1_desc.set_num_subscribers(2);
  auto& chan2_desc = desc.get_mutable_pubsub_graph().get_underlying_publish_endpoints().emplace_back();
  chan2_desc.set_process_id(proc1_id);
  chan2_desc.set_publisher_id(chan2_id);
  chan2_desc.get_mutable_buffer_layout().set_num_slots(10);
  chan2_desc.get_mutable_buffer_layout().set_message_size(sizeof(TestCog2Policy::OutAPolicy::MsgType));
  chan2_desc.set_num_subscribers(4);
  auto& chan_cog1_diag = desc.get_mutable_pubsub_graph().get_underlying_publish_endpoints().emplace_back();
  chan_cog1_diag.set_process_id(proc1_id);
  chan_cog1_diag.set_publisher_id(cog1_ep_diag);
  chan_cog1_diag.get_mutable_buffer_layout().set_num_slots(10);
  chan_cog1_diag.get_mutable_buffer_layout().set_message_size(sizeof(diagnostics::ReportTap));
  chan_cog1_diag.set_num_subscribers(1);
  auto& chan_cog2_diag = desc.get_mutable_pubsub_graph().get_underlying_publish_endpoints().emplace_back();
  chan_cog2_diag.set_process_id(proc1_id);
  chan_cog2_diag.set_publisher_id(cog2_ep_diag);
  chan_cog2_diag.get_mutable_buffer_layout().set_num_slots(10);
  chan_cog2_diag.get_mutable_buffer_layout().set_message_size(sizeof(diagnostics::ReportTap));
  chan_cog2_diag.set_num_subscribers(1);

  auto& chan_conn1 = desc.get_mutable_pubsub_graph().get_underlying_connections().emplace_back();
  chan_conn1.set_subscriber_process_id(proc1_id);
  chan_conn1.set_subscriber_id(cog2_ep_chan1a);
  chan_conn1.set_publisher_id(chan1_id);

  auto& timer1_desc = desc.get_underlying_timers().emplace_back();
  timer1_desc.set_timer_id(cog1_ep_timer1);
  timer1_desc.get_underlying_instance_path_name().set_truncate("cog1_timer");

  desc.get_underlying_init_cogs().emplace_back(cog3_inst);

  desc.set_process_id(proc1_id);

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  auto casing = CasingImpl<
    std::tuple<InitCog1Factory, TestCog1Factory, TestCog2Factory>,
    std::tuple<
      ProtoSchema<
        ::clockwork::testing::proto::Config,
        Tachyon<testing::Config>::_clockwork_uuid,
        Tappy<testing::Config>>,
      InitCog1Policy::ConfigPolicy::ConfigType>,
    std::tuple<>>(memres);
  auto channel_factory = tmpchan.make_factory();

  std::shared_ptr<clockwork::pinion::ShmSubscriber> snoop_chan2;
  std::shared_ptr<clockwork::pinion::ShmSubscriber> snoop_state1;
  testing::RunStopper exec(
    [&]()
    {
      if (!snoop_chan2)
      {
        snoop_chan2 = make_snoop(channel_factory, chan2_id, chan2_desc.get_buffer_layout());
      }
      if (!snoop_state1)
      {
        snoop_state1 = make_snoop(channel_factory, state1_id, state1_desc.value_maybe_buffer_layout());
      }
      if (
        !snoop_chan2 ||
        !snoop_state1)
      {
        return;
      }
      if (const auto msg = testing::last<TestCog2Policy::OutAPolicy::MsgType>(snoop_chan2->make_subscriber()); msg)
      {
        CHECK(msg->get_echo_config() == configured_id);
        CHECK(msg->get_group_id() == config_group_id);
        CHECK(msg->get_result() == fibonacci(msg->get_cycle()));
        if (msg->get_cycle() > 10)
        {
          // Snooping on the state is VERY snoopy since it technically hasn't been published yet, but should still exist
          // int the first slot.
          const auto state_it = snoop_state1->make_subscriber().available().begin();
          const auto& state = pinion::MessageCast<const TestCog1Policy::StateAPolicy::StateType>{}(*state_it);
          CHECK(state.get_cycle() > 10);
          // Quit
          exec.set_exit();
        }
      }
    });

  CHECK(run(desc, casing, channel_factory, exec.get_condition()) == EXIT_SUCCESS);
}

TEST_CASE("Test IO connections")
{
  testing::TestIoConnection::reset();

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  auto casing = CasingImpl<
    std::tuple<>,
    std::tuple<>,
    std::tuple<testing::TestIoConnection, testing::TestEPollableIoConnection, testing::TestFailingIoConnection>>(
    memres);

  SECTION("IO connection without endpoint IDs")
  {
    REQUIRE(
      casing.try_instantiate_io_connection(testing::TestIoConnection::uuid, {}, {}, std::nullopt) ==
      jewels::unexpected{AbstractCasing::Error::empty_endpoint_ids});
  }
  SECTION("Valid IO connection - no epollable")
  {
    const auto result = casing.try_instantiate_io_connection(
      testing::TestIoConnection::uuid,
      {},
      jewels::as_single_item_span(Tappy<common::EndpointInstanceDescription>{}),
      std::nullopt);
    REQUIRE(result);
    REQUIRE_FALSE(*result);
  }
  SECTION("Valid IO connection - epollable")
  {
    const auto result = casing.try_instantiate_io_connection(
      testing::TestEPollableIoConnection::uuid,
      {},
      jewels::as_single_item_span(Tappy<common::EndpointInstanceDescription>{}),
      std::nullopt);
    REQUIRE(result);
    REQUIRE(*result);
  }
  SECTION("Invalid ID")
  {
    REQUIRE(
      casing.try_instantiate_io_connection({}, {}, {}, std::nullopt) ==
      jewels::unexpected{AbstractCasing::Error::invalid_class_uuid});
  }
  SECTION("Failing IO Connection")
  {
    REQUIRE(
      casing.try_instantiate_io_connection(testing::TestFailingIoConnection::uuid, {}, {}, std::nullopt) ==
      jewels::unexpected{AbstractCasing::Error::init_failure});
  }

  SECTION("Register IO publishers")
  {
    constexpr pinion::BufferLayout layout{
      .num_slots = 1UL,
      .message_size = sizeof(int),
    };

    auto channel = std::make_unique<InMemoryChannel<int, layout.num_slots>>(memres);

    const auto instance_id =
      jewels::Uuid<common::IoConnectionInstanceId>::from_string("aaaaaaaa-aaaa-aaaa-4312-000000000000");
    REQUIRE(instance_id);

    const auto endpoint_class_id =
      jewels::Uuid<common::EndpointClassId>::from_string("aaaaaaaa-aaaa-aaaa-4312-000000000001");
    REQUIRE(endpoint_class_id);

    const auto endpoint_instance_id =
      jewels::Uuid<common::EndpointInstanceId>::from_string("aaaaaaaa-aaaa-aaaa-4312-000000000002");
    REQUIRE(endpoint_instance_id);

    Tappy<common::EndpointInstanceDescription> endpoint_description{};
    endpoint_description.set_endpoint_class_id(*endpoint_class_id);
    endpoint_description.set_endpoint_instance_id(*endpoint_instance_id);

    REQUIRE_FALSE(casing.try_connect_publisher(*endpoint_instance_id, channel->make_publisher(0UL)));
    REQUIRE(casing.try_instantiate_io_connection(
      testing::TestIoConnection::uuid, *instance_id, jewels::as_single_item_span(endpoint_description), std::nullopt));
    REQUIRE_FALSE(testing::TestIoConnection::publisher_set);
    REQUIRE_FALSE(testing::TestIoConnection::diags_set);
    REQUIRE(casing.try_connect_publisher(*endpoint_instance_id, channel->make_publisher(0UL)));
    REQUIRE(testing::TestIoConnection::publisher_set);
    REQUIRE_FALSE(testing::TestIoConnection::diags_set);

    // Cannot connect to the same IO connection twice.
    REQUIRE(
      casing.try_connect_publisher(*endpoint_instance_id, channel->make_publisher(0UL)) ==
      jewels::unexpected{AbstractCasing::Error::connect_publisher_failure});
  }

  SECTION("Register IO subscribers")
  {
    constexpr pinion::BufferLayout layout{
      .num_slots = 1UL,
      .message_size = sizeof(int),
    };

    auto channel = std::make_unique<InMemoryChannel<int, layout.num_slots>>(memres);

    const auto instance_id =
      jewels::Uuid<common::IoConnectionInstanceId>::from_string("aaaaaaaa-aaaa-aaaa-4312-000000000000");
    REQUIRE(instance_id);

    const auto endpoint_class_id =
      jewels::Uuid<common::EndpointClassId>::from_string("aaaaaaaa-aaaa-aaaa-4312-000000000001");
    REQUIRE(endpoint_class_id);

    const auto endpoint_instance_id =
      jewels::Uuid<common::EndpointInstanceId>::from_string("aaaaaaaa-aaaa-aaaa-4312-000000000002");
    REQUIRE(endpoint_instance_id);

    Tappy<common::EndpointInstanceDescription> endpoint_description{};
    endpoint_description.set_endpoint_class_id(*endpoint_class_id);
    endpoint_description.set_endpoint_instance_id(*endpoint_instance_id);

    REQUIRE_FALSE(casing.try_connect_subscriber(*endpoint_instance_id, channel->make_subscriber()));
    REQUIRE(casing.try_instantiate_io_connection(
      testing::TestIoConnection::uuid, *instance_id, jewels::as_single_item_span(endpoint_description), std::nullopt));
    REQUIRE_FALSE(testing::TestIoConnection::subscriber_set);
    REQUIRE(casing.try_connect_subscriber(*endpoint_instance_id, channel->make_subscriber()));
    REQUIRE(testing::TestIoConnection::subscriber_set);

    // Cannot connect to the same IO connection twice.
    REQUIRE(
      casing.try_connect_subscriber(*endpoint_instance_id, channel->make_subscriber()) ==
      jewels::unexpected{AbstractCasing::Error::connect_subscriber_failure});
  }

  SECTION("Register diagnostics")
  {
    constexpr pinion::BufferLayout layout{
      .num_slots = 1UL,
      .message_size = sizeof(int),
    };

    auto channel = std::make_unique<InMemoryChannel<int, layout.num_slots>>(memres);

    const auto instance_id =
      jewels::Uuid<common::IoConnectionInstanceId>::from_string("aaaaaaaa-aaaa-aaaa-4312-000000000000");
    REQUIRE(instance_id);

    const auto endpoint_class_id =
      jewels::Uuid<common::EndpointClassId>::from_string("aaaaaaaa-aaaa-aaaa-4312-000000000001");
    REQUIRE(endpoint_class_id);

    const auto endpoint_instance_id =
      jewels::Uuid<common::EndpointInstanceId>::from_string("aaaaaaaa-aaaa-aaaa-4312-000000000002");
    REQUIRE(endpoint_instance_id);

    Tappy<common::EndpointInstanceDescription> endpoint_description{};
    endpoint_description.set_endpoint_class_id(*endpoint_class_id);
    endpoint_description.set_endpoint_instance_id(*endpoint_instance_id);

    const auto diags_endpoint_id =
      jewels::Uuid<common::EndpointInstanceId>::from_string("aaaaaaaa-aaaa-aaaa-4312-000000000003");
    REQUIRE(diags_endpoint_id);

    REQUIRE_FALSE(casing.try_connect_publisher(*diags_endpoint_id, channel->make_publisher(0UL)));
    REQUIRE(casing.try_instantiate_io_connection(
      testing::TestIoConnection::uuid,
      *instance_id,
      jewels::as_single_item_span(endpoint_description),
      *diags_endpoint_id));
    REQUIRE_FALSE(testing::TestIoConnection::publisher_set);
    REQUIRE_FALSE(testing::TestIoConnection::diags_set);
    REQUIRE(casing.try_connect_publisher(*diags_endpoint_id, channel->make_publisher(0UL)));
    REQUIRE_FALSE(testing::TestIoConnection::publisher_set);
    REQUIRE(testing::TestIoConnection::diags_set);

    // Cannot connect to the same IO connection twice.
    REQUIRE(
      casing.try_connect_publisher(*diags_endpoint_id, channel->make_publisher(0UL)) ==
      jewels::unexpected{AbstractCasing::Error::connect_publisher_failure});
  }
}

} // namespace
} // namespace clockwork::scaffolding
