// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/repr_iface.hh"
#include "clockwork/tools/channel_spy/channel_spy.hh"
#include "clockwork/tools/channel_spy/channel_spy_config_clk_cc.hh"
#include "clockwork/tools/channel_spy/tests/support/test_helper.hh"
#include "clockwork/tools/channel_spy/tests/support/test_message.hh"
#include "clockwork/tools/channel_spy/tests/support/test_publisher.hh"
#include "clockwork/tools/channel_spy/tests/support/test_support.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork::tools::tests
{
namespace
{

// Channel 1 metadata
constexpr auto channel_name1 = "channel1";
constexpr auto schema_name1 = "schema1";
constexpr std::array schema_definition1 = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};

// Channel 2 metadata
constexpr auto channel_name2 = "channel2";
constexpr auto schema_name2 = "schema2";
constexpr std::array schema_definition2 = {std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};

/// Generate fake test channel spy configuration for testing list channels
/// @return Generated configuration
[[nodiscard]] std::unique_ptr<Tappy<ChannelSpyConfig<>>> gen_fake_channel_spy_config()
{
  auto config = std::make_unique<Tappy<ChannelSpyConfig<>>>();
  auto& channel1 = config->get_underlying_channels().emplace_back();
  channel1.get_underlying_channel_name().set_truncate(channel_name1);
  channel1.get_underlying_schema_name().set_truncate(schema_name1);
  REQUIRE(channel1.get_underlying_schema_definition().try_set(schema_definition1));
  auto& channel2a = config->get_underlying_channels().emplace_back();
  channel2a.get_underlying_channel_name().set_truncate(channel_name2);
  channel2a.get_underlying_schema_name().set_truncate(schema_name2);
  REQUIRE(channel2a.get_underlying_schema_definition().try_set(schema_definition2));
  auto& channel2b = config->get_underlying_channels().emplace_back();
  channel2b.get_underlying_channel_name().set_truncate(channel_name2);
  channel2b.get_underlying_schema_name().set_truncate(schema_name2);
  REQUIRE(channel2b.get_underlying_schema_definition().try_set(schema_definition2));
  return config;
}

TEST_CASE("channels and spy_config")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_dir_path = test_dir.get_path().string();
  const auto socket_ns = jewels::Uuid<void>::random_uuid().to_string();
  const auto config = gen_fake_channel_spy_config();
  support::write_channel_spy_config_file(test_dir_path, socket_ns, *config);

  ChannelSpy spy{test_dir_path, test_dir_path, socket_ns};
  const auto channels = spy.channels();
  REQUIRE(channels.size() == 2U);
  REQUIRE(channels[0U].channel_name == channel_name1);
  REQUIRE(channels[0U].schema_name == schema_name1);
  REQUIRE(std::ranges::equal(channels[0U].schema_definition, schema_definition1));
  REQUIRE(channels[1U].channel_name == channel_name2);
  REQUIRE(channels[1U].schema_name == schema_name2);
  REQUIRE(std::ranges::equal(channels[1U].schema_definition, schema_definition2));

  REQUIRE(*spy.read_channel_spy_config() == *config);
}

TEST_CASE("raw message subscription")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_dir_path = test_dir.get_path().string();
  const auto socket_ns = jewels::Uuid<void>::random_uuid().to_string();
  const auto test_helper =
    support::TestHelper<Tappy<support::TestMessage>>::make_test_helper(test_dir_path, test_dir_path, socket_ns);

  auto publisher = test_helper->open_publisher(0U);

  Tappy<support::TestMessage> actual_message{};
  uint64_t actual_sequence_number{99999U};
  int64_t actual_message_time{99999};

  ChannelSpy spy{test_dir_path, test_dir_path, socket_ns};
  spy.subscribe(
    support::test_channel_name1,
    [&actual_message, &actual_sequence_number, &actual_message_time](
      uint64_t sequence_number,
      int64_t message_time,
      std::span<const std::byte> data,
      const std::function<bool()>& overrun_check_fn)
    {
      actual_sequence_number = sequence_number;
      actual_message_time = message_time;
      REQUIRE(data.size() == sizeof(Tappy<support::TestMessage>));
      std::memcpy(&actual_message, data.data(), data.size());
      REQUIRE(overrun_check_fn());
    });

  Tappy<support::TestMessage> message0;
  message0.get_underlying_message_string().set_truncate("message0");
  REQUIRE(publisher->publish(0, message0));

  spy.run_once();

  REQUIRE(actual_sequence_number == 0U);
  REQUIRE(actual_message_time == 0);
  REQUIRE(actual_message == message0);

  Tappy<support::TestMessage> message1;
  message1.get_underlying_message_string().set_truncate("message1");
  REQUIRE(publisher->publish(1, message1));

  spy.run_once();

  REQUIRE(actual_sequence_number == 1U);
  REQUIRE(actual_message_time == 1);
  REQUIRE(actual_message == message1);
}

TEST_CASE("raw message subscription overruns")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_dir_path = test_dir.get_path().string();
  const auto socket_ns = jewels::Uuid<void>::random_uuid().to_string();
  const auto test_helper =
    support::TestHelper<Tappy<support::TestMessage>>::make_test_helper(test_dir_path, test_dir_path, socket_ns);

  auto publisher = test_helper->open_publisher(0U);

  Tappy<support::TestMessage> message0;
  message0.get_underlying_message_string().set_truncate("message0");
  REQUIRE(publisher->publish(0, message0));

  bool received_callback{false};

  ChannelSpy spy{test_dir_path, test_dir_path, socket_ns};
  spy.subscribe(
    support::test_channel_name1,
    [&received_callback, &publisher, &message0](
      uint64_t, int64_t, std::span<const std::byte>, const std::function<bool()>& overrun_check_fn)
    {
      REQUIRE(overrun_check_fn());
      REQUIRE(publisher->publish(0, message0));
      REQUIRE(overrun_check_fn());
      REQUIRE(publisher->publish(0, message0));
      REQUIRE_FALSE(overrun_check_fn());
      received_callback = true;
    });

  spy.run_once();

  REQUIRE(received_callback);
}

TEST_CASE("deserialized message subscription")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_dir_path = test_dir.get_path().string();
  const auto socket_ns = jewels::Uuid<void>::random_uuid().to_string();
  const auto test_helper =
    support::TestHelper<Tappy<support::TestMessage>>::make_test_helper(test_dir_path, test_dir_path, socket_ns);

  auto publisher = test_helper->open_publisher(0U);

  Tappy<support::TestMessage> actual_message{};
  uint64_t actual_sequence_number{99999U};
  int64_t actual_message_time{99999};

  ChannelSpy spy{test_dir_path, test_dir_path, socket_ns};
  spy.subscribe<Tappy<support::TestMessage>>(
    support::test_channel_name1,
    [&actual_message, &actual_sequence_number, &actual_message_time](
      uint64_t sequence_number, int64_t message_time, const std::shared_ptr<Tappy<support::TestMessage>>& message_ptr)
    {
      actual_sequence_number = sequence_number;
      actual_message_time = message_time;
      actual_message = *message_ptr;
    });

  Tappy<support::TestMessage> message0;
  message0.get_underlying_message_string().set_truncate("message0");
  REQUIRE(publisher->publish(0, message0));

  spy.run_once();

  REQUIRE(actual_sequence_number == 0U);
  REQUIRE(actual_message_time == 0);
  REQUIRE(actual_message == message0);

  Tappy<support::TestMessage> message1;
  message1.get_underlying_message_string().set_truncate("message1");
  REQUIRE(publisher->publish(1, message1));

  spy.run_once();

  REQUIRE(actual_sequence_number == 1U);
  REQUIRE(actual_message_time == 1);
  REQUIRE(actual_message == message1);
}

TEST_CASE("multi-publisher subscriber")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_dir_path = test_dir.get_path().string();
  const auto socket_ns = jewels::Uuid<void>::random_uuid().to_string();
  const auto test_helper =
    support::TestHelper<Tappy<support::TestMessage>>::make_test_helper(test_dir_path, test_dir_path, socket_ns);

  REQUIRE(test_helper->channel_name(1U) == test_helper->channel_name(2U));
  auto publisher2a = test_helper->open_publisher(1U);
  auto publisher2b = test_helper->open_publisher(2U);

  Tappy<support::TestMessage> actual_message{};
  uint64_t actual_sequence_number{99999U};
  int64_t actual_message_time{99999};

  ChannelSpy spy{test_dir_path, test_dir_path, socket_ns};
  spy.subscribe<Tappy<support::TestMessage>>(
    support::test_channel_name2,
    [&actual_message, &actual_sequence_number, &actual_message_time](
      uint64_t sequence_number, int64_t message_time, const std::shared_ptr<Tappy<support::TestMessage>>& message_ptr)
    {
      actual_sequence_number = sequence_number;
      actual_message_time = message_time;
      actual_message = *message_ptr;
    });

  Tappy<support::TestMessage> message0;
  message0.get_underlying_message_string().set_truncate("message0");
  REQUIRE(publisher2a->publish(0, message0));

  spy.run_once();

  REQUIRE(actual_sequence_number == 0U);
  REQUIRE(actual_message_time == 0);
  REQUIRE(actual_message == message0);

  Tappy<support::TestMessage> message1;
  message1.get_underlying_message_string().set_truncate("message1");
  REQUIRE(publisher2b->publish(1, message1));

  spy.run_once();

  REQUIRE(actual_sequence_number == 0U);
  REQUIRE(actual_message_time == 1);
  REQUIRE(actual_message == message1);

  Tappy<support::TestMessage> message2;
  message2.get_underlying_message_string().set_truncate("message2");
  REQUIRE(publisher2a->publish(2, message2));

  spy.run_once();

  REQUIRE(actual_sequence_number == 1U);
  REQUIRE(actual_message_time == 2);
  REQUIRE(actual_message == message2);

  Tappy<support::TestMessage> message3;
  message3.get_underlying_message_string().set_truncate("message3");
  REQUIRE(publisher2b->publish(3, message3));

  spy.run_once();

  REQUIRE(actual_sequence_number == 1U);
  REQUIRE(actual_message_time == 3);
  REQUIRE(actual_message == message3);
}

} // namespace
} // namespace clockwork::tools::tests
