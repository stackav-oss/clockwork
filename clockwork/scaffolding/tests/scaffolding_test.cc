// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/common/tests/support/fake_cog.hh"
#include "clockwork/io/network_var_packet_clk_cc.hh" // IWYU pragma: keep
#include "clockwork/io/var_packet_clk_cc.hh"
#include "clockwork/logging/channel_publisher_config_clk_cc.hh" // IWYU pragma: keep
#include "clockwork/logging/log_writer_config_clk_cc.hh"        // IWYU pragma: keep
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/detail/socket_payload.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/incoming_udp.hh"
#include "clockwork/pinion/io_connection.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/outgoing_udp.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/sock_opt.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tests/support/pub_sub.hh"
#include "clockwork/pinion/tests/support/sockets.hh"
#include "clockwork/pinion/tests/support/tmp_shm_namespace.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/end_process_exception.hh"
#include "clockwork/scaffolding/scaffolding.hh"
#include "clockwork/scaffolding/tests/support/mock_casing.hh"
#include "clockwork/scaffolding/tests/support/runtime_tools.hh"
#include "clockwork/tags.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/bits.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/scope_guard/scope_guard.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>
#include <trompeloeil/catch2.hpp> // IWYU pragma: keep (registers catch2 as the error handler)
#include <trompeloeil/matcher/any.hpp>
#include <trompeloeil/mock.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace clockwork
{

template <>
struct Tachyon<int>
{
  int x;
};
template <>
struct Tap<Tachyon<int>> : Tachyon<int>
{
};

} // namespace clockwork

namespace clockwork::scaffolding
{
namespace
{

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) For testing purposes only
std::mutex catch_mutex;

// NOLINTNEXTLINE(fuchsia-multiple-inheritance)  Test fixture, inheriting from abstract interfaces
class Cog1 : public AbstractCog, public pinion::Observer
{
  static constexpr auto exec_timeout = std::chrono::milliseconds(500);

public:
  explicit Cog1(const std::shared_ptr<AbstractCogQueue>& queue)
    : AbstractCog(jewels::memory::make_non_null_from_ref(*queue))
  {
  }
  [[nodiscard]] std::string_view get_name() const override
  {
    return "clockwork::scaffolding::Cog1";
  }
  jewels::expected<void, jewels::MonoError> prime(jewels::time::SyncTime start_time) override
  {
    if (timer->start(start_time + 50ms, 50ms))
    {
      return {};
    }
    return jewels::unexpected{jewels::MonoError{}};
  }
  CogPrepareOutcome prepare_for_execution(
    jewels::Out<jewels::time::SyncTime> /*throttled_until_out*/, jewels::time::SyncTime /*current_time*/) override
  {
    return CogPrepareResult::ready;
  }
  jewels::expected<void, CogExecutionError> execute(CogExecuteParams /*params*/) override
  {
    const std::unique_lock lock{mutex};
    const std::unique_lock catch_lock{catch_mutex};
    REQUIRE(publisher);
    testing::publish(*publisher, uint32_t{1});
    REQUIRE(timer->start(jewels::time::SyncClock::now() + exec_timeout, exec_timeout));
    return {};
  }
  void notify(const Event& /*event*/) override
  {
    add_to_ready_queue({});
  }
  std::mutex mutex;
  std::optional<pinion::PublisherHandle> publisher;
  std::shared_ptr<AbstractTimer> timer;
};

// NOLINTNEXTLINE(fuchsia-multiple-inheritance)  Test fixture, inheriting from abstract interfaces
class Cog2 : public AbstractCog, public pinion::Observer
{
public:
  explicit Cog2(const std::shared_ptr<AbstractCogQueue>& queue)
    : AbstractCog(jewels::memory::make_non_null_from_ref(*queue))
  {
  }
  [[nodiscard]] std::string_view get_name() const override
  {
    return "clockwork::scaffolding::Cog1";
  }
  jewels::expected<void, jewels::MonoError> prime(jewels::time::SyncTime /*start_time*/) override
  {
    return {};
  }
  CogPrepareOutcome prepare_for_execution(
    jewels::Out<jewels::time::SyncTime> /*throttled_until_out*/, jewels::time::SyncTime /*current_time*/) override
  {
    return CogPrepareResult::ready;
  }
  jewels::expected<void, CogExecutionError> execute(CogExecuteParams /*params*/) override
  {
    const std::unique_lock lock{mutex};
    const std::unique_lock catch_lock{catch_mutex};
    REQUIRE(subscriber);
    CHECK(testing::dump<uint32_t>(subscriber) == std::vector<uint32_t>({1}));
    run_count++;
    return {};
  }
  void notify(const Event& /*event*/) override
  {
    add_to_ready_queue({});
  }
  std::mutex mutex;
  std::shared_ptr<pinion::AbstractChannel> subscriber;
  std::atomic<uint32_t> run_count{0};
};

// NOLINTNEXTLINE(fuchsia-multiple-inheritance) For testing purposes only
class Cog2EndProcess : public Cog2
{
public:
  explicit Cog2EndProcess(int exit_code, const std::shared_ptr<AbstractCogQueue>& queue)
    : Cog2(queue), exit_code_(exit_code)
  {
  }
  [[nodiscard]] std::string_view get_name() const override
  {
    return "clockwork::scaffolding::Cog2EndProcess";
  }
  jewels::expected<void, CogExecutionError> execute(CogExecuteParams /*params*/) override
  {
    run_count++;
    throw EndProcessException(exit_code_);
  }
  int exit_code_;
};

// NOLINTNEXTLINE(fuchsia-multiple-inheritance) Test fixture, inheriting from abstract interfaces
class InitCog : public AbstractCog, public pinion::Observer
{
public:
  explicit InitCog(const std::shared_ptr<AbstractCogQueue>& queue)
    : AbstractCog(jewels::memory::make_non_null_from_ref(*queue))
  {
  }
  [[nodiscard]] std::string_view get_name() const override
  {
    return "clockwork::scaffolding::InitCog";
  }
  jewels::expected<void, jewels::MonoError> prime(jewels::time::SyncTime /*start_time*/) override
  {
    return {};
  }
  CogPrepareOutcome prepare_for_execution(
    jewels::Out<jewels::time::SyncTime> /*throttled_until_out*/, jewels::time::SyncTime /*current_time*/) override
  {
    return CogPrepareResult::ready;
  }
  jewels::expected<void, CogExecutionError> execute(CogExecuteParams /*params*/) override
  {
    run_count++;
    return {};
  }
  void notify(const Event& /*event*/) override {}
  std::atomic<uint32_t> run_count{0};
};

TEST_CASE("scaffolding_run")
{
  using testing::FakeCog;
  using testing::FakeObserver;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard tmpdir;
  const pinion::support::TmpShmNamespace tmpchan;

  const auto proc1_id = jewels::Uuid<common::ProcessInstanceId>::random_uuid();
  const auto cog1_class = jewels::Uuid<common::CogClassId>::random_uuid();
  const auto cog1_inst = jewels::Uuid<common::CogInstanceId>::random_uuid();
  const auto cog1_ep_state1 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog1_ep_config1 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog1_ep_timer1 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog2_class = jewels::Uuid<common::CogClassId>::random_uuid();
  const auto cog2_inst = jewels::Uuid<common::CogInstanceId>::random_uuid();
  const auto cog2_ep_state1 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog2_ep_chan1 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto cog3_class = jewels::Uuid<common::CogClassId>::random_uuid();
  const auto cog3_inst = jewels::Uuid<common::CogInstanceId>::random_uuid();
  const auto state1_id = jewels::Uuid<common::StateInstanceId>::random_uuid();
  const auto state1_schema = jewels::Uuid<RepresentationTag>::random_uuid();
  const auto state1_memres = jewels::Uuid<common::MemoryResourceId>::random_uuid();
  const auto config1_id = jewels::Uuid<common::ConfigInstanceId>::random_uuid();
  const auto config1_schema = jewels::Uuid<RepresentationTag>::random_uuid();
  const auto chan1_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();

  testing::write_schema(tmpdir.get_path() / "config1.dat", Tappy<int>{1});

  Tappy<common::ProcessDescription<>> desc;
  auto& cog1_desc = desc.get_underlying_cog_instances().emplace_back();
  cog1_desc.get_mutable_cog_class_id() = cog1_class;
  cog1_desc.get_mutable_cog_instance_id() = cog1_inst;
  cog1_desc.get_mutable_endpoints() = {};
  cog1_desc.get_underlying_instance_path_name().set_truncate("cog1");
  auto& cog2_desc = desc.get_underlying_cog_instances().emplace_back();
  cog2_desc.get_mutable_cog_class_id() = cog2_class;
  cog2_desc.get_mutable_cog_instance_id() = cog2_inst;
  cog2_desc.get_mutable_endpoints() = {};
  cog2_desc.get_underlying_instance_path_name().set_truncate("cog2");
  auto& cog3_desc = desc.get_underlying_cog_instances().emplace_back();
  cog3_desc.get_mutable_cog_class_id() = cog3_class;
  cog3_desc.get_mutable_cog_instance_id() = cog3_inst;
  cog3_desc.get_mutable_endpoints() = {};
  cog3_desc.get_underlying_instance_path_name().set_truncate("initcog");

  auto& state1_memres_desc = desc.get_mutable_memory_resource_graph().get_underlying_memory_resources().emplace_back();
  state1_memres_desc.get_mutable_memory_resource_id() = state1_memres;
  state1_memres_desc.get_underlying_instance_path_name().set_truncate("state1_memres");
  state1_memres_desc.get_mutable_resource_type() = common::MemoryResourceType::new_delete;
  state1_memres_desc.get_mutable_resource_max_size() = 1;

  auto& state1_desc = desc.get_mutable_state_graph().get_underlying_state_instances().emplace_back();
  state1_desc.get_mutable_representation_id() = state1_schema;
  state1_desc.get_mutable_state_instance_id() = state1_id;
  state1_desc.get_underlying_instance_path_name().set_truncate("state1");
  state1_desc.reset_maybe_buffer_layout();
  state1_desc.set_maybe_memory_resource(state1_memres);
  auto& state_conn1 = desc.get_mutable_state_graph().get_underlying_connections().emplace_back();
  state_conn1.get_mutable_state_id() = state1_id;
  state_conn1.get_mutable_endpoint_id() = cog1_ep_state1;
  auto& state_conn2 = desc.get_mutable_state_graph().get_underlying_connections().emplace_back();
  state_conn2.get_mutable_state_id() = state1_id;
  state_conn2.get_mutable_endpoint_id() = cog2_ep_state1;

  // Create data source for config
  auto& data_source1 = desc.get_underlying_data_sources().emplace_back();
  data_source1.get_mutable_representation_id() = config1_schema;
  data_source1.get_mutable_data_source_type() = common::DataSourceType::file;
  data_source1.get_underlying_source_path_or_name().set_truncate(tmpdir.get_path() / "config1.dat");

  auto& config1_desc = desc.get_mutable_config_graph().get_underlying_config_instances().emplace_back();
  config1_desc.get_mutable_config_instance_id() = config1_id;
  config1_desc.get_underlying_instance_path_name().set_truncate("config1");
  config1_desc.set_init_data_source(0);
  auto& config_conn1 = desc.get_mutable_config_graph().get_underlying_connections().emplace_back();
  config_conn1.get_mutable_config_id() = config1_id;
  config_conn1.get_mutable_endpoint_id() = cog1_ep_config1;

  auto& chan1_desc = desc.get_mutable_pubsub_graph().get_underlying_publish_endpoints().emplace_back();
  chan1_desc.get_mutable_process_id() = proc1_id;
  chan1_desc.get_mutable_publisher_id() = chan1_id;
  chan1_desc.get_mutable_buffer_layout().get_mutable_num_slots() = 1;
  chan1_desc.get_mutable_buffer_layout().get_mutable_message_size() = sizeof(uint32_t);
  chan1_desc.get_mutable_num_subscribers() = 1;

  auto& chan_conn1 = desc.get_mutable_pubsub_graph().get_underlying_connections().emplace_back();
  chan_conn1.get_mutable_subscriber_process_id() = proc1_id;
  chan_conn1.get_mutable_subscriber_id() = cog2_ep_chan1;
  chan_conn1.get_mutable_publisher_id() = chan1_id;

  auto& timer1_desc = desc.get_underlying_timers().emplace_back();
  timer1_desc.get_mutable_timer_id() = cog1_ep_timer1;
  timer1_desc.get_underlying_instance_path_name().set_truncate("cog1_timer");

  desc.get_underlying_init_cogs().emplace_back(cog3_inst);

  desc.get_mutable_process_id() = proc1_id;

  MockCasing casing;
  std::mutex init_mutex;
  std::unique_lock init_lock(init_mutex);
  std::shared_ptr<Cog1> cog1;
  std::shared_ptr<Cog2> cog2;
  std::shared_ptr<InitCog> cog3;
  using RetT = jewels::expected<void, AbstractCasing::Error>;
  using CogRetT = jewels::expected<std::shared_ptr<AbstractCog>, AbstractCasing::Error>;
  using SubRetT = jewels::expected<std::shared_ptr<pinion::Observer>, AbstractCasing::Error>;
  using ValidRetT = jewels::expected<void, jewels::MonoError>;
  REQUIRE_CALL(casing, try_instantiate_state(state1_id, state1_schema, ANY(jewels::memory::MemoryResource)))
    .RETURN(RetT{});
  REQUIRE_CALL(casing, try_instantiate_config(config1_id, config1_schema, ::trompeloeil::_, ::trompeloeil::_))
    .RETURN(RetT{});
  REQUIRE_CALL(casing, try_instantiate_cog(cog1_desc, ::trompeloeil::_, ::trompeloeil::_))
    .LR_RETURN(CogRetT{cog1 = std::make_shared<Cog1>(_2)});
  REQUIRE_CALL(casing, try_instantiate_cog(cog3_desc, ::trompeloeil::_, ::trompeloeil::_))
    .LR_RETURN(CogRetT{cog3 = std::make_shared<InitCog>(_2)});
  REQUIRE_CALL(casing, try_connect_config(cog1_ep_config1, config1_id)).RETURN(RetT{});
  REQUIRE_CALL(casing, try_connect_state(cog1_ep_state1, state1_id, true)).RETURN(RetT{});
  REQUIRE_CALL(casing, try_connect_state(cog2_ep_state1, state1_id, true)).RETURN(RetT{});
  REQUIRE_CALL(casing, try_connect_subscriber(cog2_ep_chan1, ::trompeloeil::_))
    .LR_SIDE_EFFECT(cog2->subscriber = std::move(_2))
    .LR_RETURN(SubRetT{cog2});
  REQUIRE_CALL(casing, try_connect_publisher(chan1_id, ::trompeloeil::_))
    .LR_SIDE_EFFECT(cog1->publisher.emplace(std::move(_2)))
    .RETURN(RetT{});
  REQUIRE_CALL(casing, try_connect_timer(cog1_ep_timer1, ::trompeloeil::_))
    .LR_SIDE_EFFECT(cog1->timer = _2)
    .LR_RETURN(SubRetT{cog1});
  REQUIRE_CALL(casing, finalize()).LR_SIDE_EFFECT(init_lock.unlock()).RETURN(ValidRetT{});
  REQUIRE_CALL(casing, shutdown());

  auto channel_factory = tmpchan.make_factory();

  constexpr uint32_t timer_cycles = 4;

  auto do_run = [&init_mutex, &cog2, &init_lock, &desc, &casing, &channel_factory](bool deterministic)
  {
    testing::RunStopper exec(
      [&init_mutex, &cog2](auto& exec)
      {
        const std::unique_lock lock(init_mutex);
        if (cog2->run_count >= timer_cycles)
        {
          exec.set_exit();
        }
      });

    const jewels::ScopeGuard ud_cleanup{[&init_lock]
                                        {
                                          if (init_lock)
                                          {
                                            init_lock.unlock();
                                          }
                                        }};

    if (!deterministic)
    {
      return run(desc, casing, channel_factory, exec.get_condition());
    }
    const ExecutionParams params{
      .start_time = jewels::time::SyncTime(std::chrono::nanoseconds(1000000000)),
      .end_time = jewels::time::SyncTime(std::chrono::nanoseconds(21000000000))};
    return run_deterministic(desc, casing, channel_factory, exec.get_condition(), params);
  };

  SECTION("normal")
  {
    REQUIRE_CALL(casing, try_instantiate_cog(cog2_desc, ::trompeloeil::_, ::trompeloeil::_))
      .LR_RETURN(CogRetT{cog2 = std::make_shared<Cog2>(_2)});
    CHECK(do_run(false) == EXIT_SUCCESS);
    CHECK(cog2->run_count >= timer_cycles);
    CHECK(cog3->run_count == 1);
  }

  SECTION("forced exit")
  {
    constexpr uint32_t exit_code = 123;
    REQUIRE_CALL(casing, try_instantiate_cog(cog2_desc, ::trompeloeil::_, ::trompeloeil::_))
      .LR_RETURN(CogRetT{cog2 = std::make_shared<Cog2EndProcess>(exit_code, _2)});
    CHECK_THROWS(do_run(true) == exit_code);
    CHECK(cog2->run_count == 1);
    CHECK(cog3->run_count == 1);
  }
}

using UdpMsg = Tappy<io::VarPacket<sizeof(int)>>;

TEST_CASE("Testing IO connections using round-trip UDP")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  const pinion::support::TmpShmNamespace tmpchan;

  const auto incoming_udp_class =
    jewels::Uuid<common::IoConnectionClassId>::from_string("eeeeeeee-eeee-eeee-8879-000000000001");
  REQUIRE(incoming_udp_class);
  const auto outgoing_udp_class =
    jewels::Uuid<common::IoConnectionClassId>::from_string("eeeeeeee-eeee-eeee-8879-000000000002");
  REQUIRE(outgoing_udp_class);

  const auto incoming_udp_inst =
    jewels::Uuid<common::IoConnectionInstanceId>::from_string("eeeeeeee-eeee-eeee-8879-000000000003");
  REQUIRE(incoming_udp_inst);
  const auto outgoing_udp_inst =
    jewels::Uuid<common::IoConnectionInstanceId>::from_string("eeeeeeee-eeee-eeee-8879-000000000004");

  const auto incoming_udp_endpoint_class =
    jewels::Uuid<common::EndpointClassId>::from_string("eeeeeeee-eeee-eeee-8879-000000000005");
  REQUIRE(incoming_udp_endpoint_class);
  const auto incoming_udp_endpoint_instance =
    jewels::Uuid<common::EndpointInstanceId>::from_string("eeeeeeee-eeee-eeee-8879-000000000006");
  REQUIRE(incoming_udp_endpoint_instance);

  Tappy<common::EndpointInstanceDescription> incoming_udp_endpoint{};
  incoming_udp_endpoint.set_endpoint_class_id(*incoming_udp_endpoint_class);
  incoming_udp_endpoint.set_endpoint_instance_id(*incoming_udp_endpoint_instance);

  const auto outgoing_udp_endpoint_class =
    jewels::Uuid<common::EndpointClassId>::from_string("eeeeeeee-eeee-eeee-8879-000000000007");
  REQUIRE(outgoing_udp_endpoint_class);
  const auto outgoing_udp_endpoint_instance =
    jewels::Uuid<common::EndpointInstanceId>::from_string("eeeeeeee-eeee-eeee-8879-000000000008");
  REQUIRE(outgoing_udp_endpoint_instance);

  Tappy<common::EndpointInstanceDescription> outgoing_udp_endpoint{};
  outgoing_udp_endpoint.set_endpoint_class_id(*outgoing_udp_endpoint_class);
  outgoing_udp_endpoint.set_endpoint_instance_id(*outgoing_udp_endpoint_instance);

  Tappy<common::ProcessDescription<>> desc;
  auto& incoming_io_connection_desc = desc.get_underlying_io_connections().emplace_back();
  incoming_io_connection_desc.set_class_id(*incoming_udp_class);
  incoming_io_connection_desc.set_instance_id(*incoming_udp_inst);
  REQUIRE(
    incoming_io_connection_desc.get_underlying_endpoints().try_set(jewels::as_single_item_span(incoming_udp_endpoint)));
  auto& outgoing_io_connection_desc = desc.get_underlying_io_connections().emplace_back();
  outgoing_io_connection_desc.set_class_id(*outgoing_udp_class);
  outgoing_io_connection_desc.set_instance_id(*outgoing_udp_inst);
  REQUIRE(
    outgoing_io_connection_desc.get_underlying_endpoints().try_set(jewels::as_single_item_span(outgoing_udp_endpoint)));

  auto& chan_desc = desc.get_mutable_pubsub_graph().get_underlying_publish_endpoints().emplace_back();
  chan_desc.get_mutable_publisher_id() = *incoming_udp_endpoint_instance;
  chan_desc.get_mutable_buffer_layout().get_mutable_num_slots() = 10;
  chan_desc.get_mutable_buffer_layout().get_mutable_message_size() = sizeof(UdpMsg);
  chan_desc.get_mutable_num_subscribers() = 1;

  auto& conn = desc.get_mutable_pubsub_graph().get_underlying_connections().emplace_back();
  conn.get_mutable_subscriber_id() = *outgoing_udp_endpoint_instance;
  conn.get_mutable_publisher_id() = *incoming_udp_endpoint_instance;

  auto channel_factory = tmpchan.make_factory();

  using Msg = Tachyon<io::VarPacket<sizeof(uint32_t)>>;
  MockCasing casing;

  const std::pmr::string host{"127.0.0.1"};
  const uint16_t dynamic_port{0U};
  const auto batch_size{1UL};

  // the use of NonNullSharedPtr prevents safe teardown of the system because these become entangled with the channels,
  // which only live during `run()`.  Thus we must copy them into normal shared_ptr and destruct the NonNullSharedPtr
  // so that these can be safely deleted.

  std::shared_ptr<pinion::IncomingUdp<Msg>> incoming_udp;
  {
    auto maybe_incoming_udp = pinion::IncomingUdp<Msg>::try_make(
      memres, *incoming_udp_endpoint_class, {.host = host, .port = dynamic_port}, batch_size);
    REQUIRE(maybe_incoming_udp);
    incoming_udp = *std::move(maybe_incoming_udp);
  }

  auto assigned_addr = support::get_assigned_addr(incoming_udp->fd());
  REQUIRE(assigned_addr);

  auto receiver = support::Receiver::try_make(std::string{host}, dynamic_port);
  REQUIRE(receiver);

  std::shared_ptr<pinion::OutgoingUdp<Msg>> outgoing_udp;
  {
    auto receiver_port = receiver->port();
    REQUIRE(receiver_port);
    auto maybe_outgoing_udp =
      pinion::OutgoingUdp<Msg>::try_make(memres, *outgoing_udp_endpoint_class, {.host = host, .port = *receiver_port});
    REQUIRE(maybe_outgoing_udp);
    outgoing_udp = *std::move(maybe_outgoing_udp);
  }

  using RetT = jewels::expected<void, AbstractCasing::Error>;
  using SubRetT = jewels::expected<std::shared_ptr<pinion::Observer>, AbstractCasing::Error>;
  using ValidRetT = jewels::expected<void, jewels::MonoError>;
  using IoConnRetT = jewels::expected<std::shared_ptr<EPollable>, AbstractCasing::Error>;
  REQUIRE_CALL(
    casing,
    try_instantiate_io_connection(
      *incoming_udp_class, *incoming_udp_inst, jewels::as_single_item_span(incoming_udp_endpoint), std::nullopt))
    .LR_RETURN(IoConnRetT{std::shared_ptr<EPollable>{incoming_udp}});
  REQUIRE_CALL(
    casing,
    try_instantiate_io_connection(
      *outgoing_udp_class, *outgoing_udp_inst, jewels::as_single_item_span(outgoing_udp_endpoint), std::nullopt))
    .LR_RETURN(IoConnRetT{std::shared_ptr<EPollable>{}});
  REQUIRE_CALL(casing, try_connect_subscriber(*outgoing_udp_endpoint_instance, ::trompeloeil::_))
    .LR_SIDE_EFFECT(std::ignore = outgoing_udp->connect_subscriber(*outgoing_udp_endpoint_class, std::move(_2)))
    .LR_RETURN(SubRetT{outgoing_udp});
  REQUIRE_CALL(casing, try_connect_publisher(*incoming_udp_endpoint_instance, ::trompeloeil::_))
    .LR_SIDE_EFFECT(std::ignore = incoming_udp->connect_publisher(*incoming_udp_endpoint_class, std::move(_2)))
    .RETURN(RetT{});
  REQUIRE_CALL(casing, finalize()).RETURN(ValidRetT{});
  REQUIRE_CALL(casing, shutdown()).LR_SIDE_EFFECT(incoming_udp.reset()).LR_SIDE_EFFECT(outgoing_udp.reset());

  constexpr uint32_t payload{123};
  REQUIRE(support::send_to(as_bytes(jewels::as_single_item_span(payload)), incoming_udp->fd(), *assigned_addr));

  uint32_t read_payload{0};
  REQUIRE(read_payload != payload);

  testing::RunStopper exec{[&receiver, &read_payload](auto& exec)
                           {
                             const auto read_bytes = receiver->read<sizeof(payload)>();
                             if (read_bytes)
                             {
                               read_payload = jewels::memory::bit_cast_to<uint32_t>(std::span{*read_bytes});
                               exec.set_exit();
                             }
                           }};

  REQUIRE(run(desc, casing, channel_factory, exec.get_condition()) == EXIT_SUCCESS);

  REQUIRE(payload == read_payload);
}

} // namespace

} // namespace clockwork::scaffolding
