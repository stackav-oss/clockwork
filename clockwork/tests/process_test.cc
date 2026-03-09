// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/shm_subscriber.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tests/support/pub_sub.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/end_process_exception.hh"
#include "clockwork/scaffolding/main_impl.hh"
#include "clockwork/scaffolding/tests/support/runtime_tools.hh"
#include "clockwork/scaffolding/tests/support/test_cogs_dial.hh"
#include "clockwork/scaffolding/tests/support/test_msgs_clk_cc.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/utility/fix_clockwork_path.hh"
#include "jewels/uuid/uuid.hh"

#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace clockwork::testing
{
std::atomic<int> cog1_exit_code = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables) test code

void execute_cog(InitCog1Dial& dial)
{
  jewels::log_cerr_info("cog INIT exec");
  dial.get_states().get_state().set_cycle(2);
  dial.get_states().get_state().set_group_id(dial.get_configs().get_config().get_group_id());
  dial.get_states().get_fib().map[0] = 0;
  dial.get_states().get_fib().map[1] = 1;
}

void execute_cog(TestCog1Dial& dial)
{
  if (auto exit_code = cog1_exit_code.load(); exit_code != 0)
  {
    throw scaffolding::EndProcessException(exit_code);
  }
  uint64_t counter = dial.get_states().get_state_a().get_cycle();
  jewels::log_cerr_info("cog 111 exec {}", counter);
  dial.get_outputs().get_out_a().message().set_cycle(counter);
  dial.get_outputs().get_out_a().message().set_config(dial.get_configs().get_cfg_a().get_id());
  dial.get_outputs().get_out_a().mark_for_publish();
  dial.get_states().get_state_a().set_cycle(counter + 1);
}

void execute_cog(TestCog2Dial& dial)
{
  auto first = dial.get_inputs().get_in_a().get_first_new();
  if (first != dial.get_inputs().get_in_a().end())
  {
    const auto cycle = first->get_cycle();
    auto& fib = dial.get_states().get_fib().map;
    auto result = fib[cycle] = fib.at(cycle - 1) + fib.at(cycle - 2);
    jewels::log_cerr_info("cog 222 exec {} -> {}", cycle, result);
    dial.get_outputs().get_out_a().message().set_cycle(cycle);
    dial.get_outputs().get_out_a().message().set_echo_config(first->get_config());
    dial.get_outputs().get_out_a().message().set_group_id(dial.get_states().get_state_a().get_group_id());
    dial.get_outputs().get_out_a().message().set_result(result);
    dial.get_outputs().get_out_a().mark_for_publish();
  }
}

} // namespace clockwork::testing

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

TEST_CASE("clockwork_integration")
{
  constexpr auto test_system_config_id =
    *jewels::Uuid<testing::ConfigId>::from_string("d54c1111-9ce5-4cba-9fb1-8287fa554c2b");
  const jewels::testing::TmpDirectoryGuard tmpdir;
  const std::string arg_pd_file_name = jewels::fix_clockwork_path(
    "clockwork/tests/support/clockwork.clockwork.tests.support.test_system.test_system.proc.tachyon");
  const std::string arg_pinion_dir{tmpdir.get_path().c_str()};
  const std::string arg_pinion_ns = jewels::Uuid<int>::random_uuid().to_string();
  std::vector<const char*> args = {
    {"integration_test_bin",
     "--pinion-dir",
     arg_pinion_dir.c_str(),
     "--pinion-ns",
     arg_pinion_ns.c_str(),
     arg_pd_file_name.c_str()}};

  auto channel_factory =
    pinion::ShmChannelFactory::make(
      jewels::memory::MemoryResource(std::pmr::get_default_resource()), arg_pinion_ns, arg_pinion_dir)
      .value();

  // The process description file is too big to fit on the stack.
  auto pd_ptr = std::make_unique<Tappy<common::ProcessDescription<>>>();
  auto& desc = *pd_ptr;

  // Load the pd file to lookup UUIDs
  auto pd_file = jewels::filesystem::File::open(arg_pd_file_name);
  REQUIRE(pd_file);
  auto pd_file_read = pd_file->pread(std::as_writable_bytes(jewels::as_single_item_span(desc)));
  REQUIRE(pd_file_read);
  REQUIRE(pd_file_read.value() == sizeof(desc));

  const Tappy<common::PublishEndpoint<>>* chan2_desc = nullptr;
  const Tappy<common::StateInstanceDescription<>>* state_desc = nullptr;
  for (const auto& pub_desc : desc.get_pubsub_graph().get_publish_endpoints())
  {
    if (pub_desc.get_buffer_layout().get_message_size() == sizeof(Tappy<testing::Message2>))
    {
      chan2_desc = &pub_desc;
    }
  }
  for (const auto& x_desc : desc.get_state_graph().get_state_instances())
  {
    if (x_desc.get_representation_id() == Tachyon<testing::State>::_clockwork_uuid)
    {
      state_desc = &x_desc;
    }
  }
  REQUIRE(chan2_desc);
  REQUIRE(state_desc);

  constexpr int32_t expected_cycles = 10;

  std::shared_ptr<clockwork::pinion::ShmSubscriber> snoop_chan2;
  std::shared_ptr<clockwork::pinion::ShmSubscriber> snoop_state1;
  testing::RunStopper exec(
    [&snoop_chan2, &snoop_state1, &channel_factory, &chan2_desc, &state_desc, &exec, &test_system_config_id]()
    {
      if (!snoop_chan2)
      {
        snoop_chan2 =
          testing::make_snooper(channel_factory, chan2_desc->get_publisher_id(), chan2_desc->get_buffer_layout());
      }
      if (!snoop_state1)
      {
        snoop_state1 = testing::make_snooper(
          channel_factory, state_desc->get_state_instance_id(), state_desc->value_maybe_buffer_layout());
      }
      if (!snoop_chan2 || !snoop_state1)
      {
        return;
      }
      if (const auto msg = testing::last<Tappy<testing::Message2>>(snoop_chan2->make_subscriber()); msg)
      {
        CHECK(msg->get_echo_config() == test_system_config_id);
        CHECK(
          msg->get_group_id() == jewels::Uuid<testing::GroupId>::from_string("fca314a1-f4d0-4679-bef8-29778adad50c"));
        CHECK(msg->get_result() == fibonacci(msg->get_cycle()));
        if (msg->get_cycle() > expected_cycles)
        {
          const auto msg_history = testing::dump<Tappy<testing::Message2>>(snoop_chan2->make_subscriber());
          CHECK(msg_history[0].get_cycle() == 2);
          // State technically hasn't been published yet, but should
          // still exist in the first slot.
          const auto state_it = snoop_state1->make_subscriber().available().begin();
          const auto& state = pinion::MessageCast<const Tappy<testing::State>>{}(*state_it);
          CHECK(state.get_cycle() > 10);
          exec.set_exit();
        }
      }
    });

  SECTION("success")
  {
    CHECK(main(static_cast<int>(args.size()), args.data(), exec.get_condition()) == EXIT_SUCCESS);
  }
  SECTION("exit with code")
  {
    args.push_back("--deterministic-runner");
    args.push_back("--sim-start-time-ns");
    args.push_back("1000000000");
    args.push_back("--sim-end-time-ns");
    args.push_back("9000000000");
    constexpr int test_code = 123;
    testing::cog1_exit_code = test_code;
    CHECK(main(static_cast<int>(args.size()), args.data(), exec.get_condition()) == test_code);
  }
}

} // namespace
} // namespace clockwork::scaffolding
