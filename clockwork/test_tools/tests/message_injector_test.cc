// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/repr_iface.hh"
#include "clockwork/runners/channel_publisher.hh"
#include "clockwork/test_tools/clockwork_system_runner.hh"
#include "clockwork/test_tools/tests/support/addition_message.hh"
#include "clockwork/test_tools/tests/support/sum_message.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/utility/fix_clockwork_path.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>

namespace clockwork
{
namespace
{
/// Utility function for getting converting MultiMessageInfoData into a tachyon message.
template <typename MessageType>
jewels::expected<std::shared_ptr<MessageType>, jewels::MonoError> as_message(MultiMessageInfoData& message_info)
{
  if (message_info.msgs.empty())
  {
    jewels::log_cerr_error("MultiMessageInfoData message queue was empty trying to extract a messsage");
    return jewels::unexpected(jewels::MonoError{});
  }

  auto& data = message_info.msgs.front();

  auto message_span = std::span<const std::byte>(data);
  if (message_span.size_bytes() != sizeof(MessageType))
  {
    jewels::log_cerr_error("Size mismatch attempting to extract a message");
    return jewels::unexpected(jewels::MonoError{});
  }

  auto retval = std::make_shared<MessageType>();
  std::memcpy(retval.get(), message_span.data(), sizeof(MessageType));

  return retval;
}
} // namespace

TEST_CASE("Message Injector System Runner")
{
  const auto process_description_path = jewels::fix_clockwork_path(
    "clockwork/test_tools/tests/support/"
    "clockwork.clockwork.test_tools.tests.support.addition_test_system_"
    "synthetic.addition_system_synthetic.proc.tachyon");

  const auto channel_publisher_config_path = jewels::fix_clockwork_path(
    "clockwork/test_tools/tests/support/"
    "addition_test_system_synthetic.addition_system_synthetic.TestCpu_channel_publisher_config.tachyon");

  const auto log_writer_config_path = jewels::fix_clockwork_path(
    "clockwork/test_tools/tests/support/"
    "addition_test_system_synthetic.addition_system_synthetic.TestCpu_telemetry_logger_config.tachyon");

  using namespace std::chrono_literals;

  constexpr auto start_time = jewels::time::SyncTime(3000ms);
  constexpr auto end_time = start_time + 300ms;

  auto config = MessageInjectorSystemRunnerConfig{
    .process_description_path = process_description_path,
    .start_time = start_time,
    .end_time = end_time,
    .channel_publisher_config = channel_publisher_config_path,
    .log_writer_config = log_writer_config_path};

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  auto injector_system_runner = MessageInjectorSystemRunner::create(config, memory_resource);
  REQUIRE(injector_system_runner);

  Tappy<system_runner::AdditionMessage> addition_message1;
  addition_message1.set_addend_one(2);
  addition_message1.set_addend_two(3);

  Tappy<system_runner::AdditionMessage> addition_message2;
  addition_message2.set_addend_one(42);
  addition_message2.set_addend_two(467);
  const std::string channel_name = "AdditionChannelIn";

  SECTION("Add in order")
  {
    injector_system_runner->add_message(addition_message1, start_time + std::chrono::milliseconds(1), channel_name);
    injector_system_runner->add_message(addition_message2, start_time + std::chrono::milliseconds(10), channel_name);
    REQUIRE(injector_system_runner->run());

    auto message1 = injector_system_runner->try_pop_message();
    REQUIRE(message1);
    auto maybe_sum_message1 = as_message<Tappy<system_runner::SumMessage>>(*message1);
    REQUIRE(maybe_sum_message1);
    auto sum_message1 = *maybe_sum_message1;
    REQUIRE(addition_message1.get_addend_one() + addition_message1.get_addend_two() == sum_message1->get_sum());

    auto message2 = injector_system_runner->try_pop_message();
    REQUIRE(message2);
    auto maybe_sum_message2 = as_message<Tappy<system_runner::SumMessage>>(*message2);
    REQUIRE(maybe_sum_message2);
    auto sum_message2 = *maybe_sum_message2;
    REQUIRE(addition_message2.get_addend_one() + addition_message2.get_addend_two() == sum_message2->get_sum());
  }

  SECTION("Add out of order")
  {
    injector_system_runner->add_message(addition_message2, start_time + std::chrono::milliseconds(10), channel_name);
    injector_system_runner->add_message(addition_message1, start_time + std::chrono::milliseconds(1), channel_name);
    REQUIRE(injector_system_runner->run());

    auto message1 = injector_system_runner->try_pop_message();
    REQUIRE(message1);
    auto maybe_sum_message1 = as_message<Tappy<system_runner::SumMessage>>(*message1);
    REQUIRE(maybe_sum_message1);
    auto sum_message1 = *maybe_sum_message1;
    REQUIRE(addition_message1.get_addend_one() + addition_message1.get_addend_two() == sum_message1->get_sum());

    auto message2 = injector_system_runner->try_pop_message();
    REQUIRE(message2);
    auto maybe_sum_message2 = as_message<Tappy<system_runner::SumMessage>>(*message2);
    REQUIRE(maybe_sum_message2);
    auto sum_message2 = *maybe_sum_message2;
    REQUIRE(addition_message2.get_addend_one() + addition_message2.get_addend_two() == sum_message2->get_sum());
  }
}
} // namespace clockwork
