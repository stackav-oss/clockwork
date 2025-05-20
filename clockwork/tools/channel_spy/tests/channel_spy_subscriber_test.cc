// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/repr_iface.hh"
#include "clockwork/tools/channel_spy/channel_spy_subscriber.hh"
#include "clockwork/tools/channel_spy/tests/support/test_message.hh"
#include "clockwork/tools/channel_spy/tests/support/test_publisher.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace clockwork::tools::tests
{
namespace
{

constexpr auto channel_name = "3c0c456d-f5d5-4630-a836-484a9662f629";
constexpr size_t num_slots = 2U;

TEST_CASE("ChannelSpySubscriber")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_dir_path = test_dir.get_path().string();
  const auto socket_ns = jewels::Uuid<void>::random_uuid().to_string();

  SECTION("No publisher")
  {
    REQUIRE_THROWS(ChannelSpySubscriber::make_subscriber<Tappy<support::TestMessage>>(
      test_dir_path,
      socket_ns,
      channel_name,
      num_slots,
      [](uint64_t, int64_t, std::unique_ptr<Tappy<support::TestMessage>>) {}));
  }

  std::vector<uint64_t> actual_sequence_numbers;
  std::vector<int64_t> actual_message_times;
  std::vector<Tappy<support::TestMessage>> actual_messages;
  std::vector<uint64_t> expected_sequence_numbers;
  std::vector<int64_t> expected_message_times;
  std::vector<Tappy<support::TestMessage>> expected_messages;

  auto publisher_result =
    support::TestPublisher<Tappy<support::TestMessage>>::open(test_dir_path, socket_ns, channel_name, num_slots);
  REQUIRE(publisher_result);
  auto subscriber = ChannelSpySubscriber::make_subscriber<Tappy<support::TestMessage>>(
    test_dir_path,
    socket_ns,
    channel_name,
    num_slots,
    [&actual_sequence_numbers, &actual_message_times, &actual_messages](
      uint64_t sequence_number, int64_t message_time, std::unique_ptr<Tappy<support::TestMessage>> message_ptr)
    {
      actual_sequence_numbers.emplace_back(sequence_number);
      actual_message_times.emplace_back(message_time);
      actual_messages.emplace_back(*message_ptr);
    });

  SECTION("Subscriber receives all messages when it keeps up")
  {
    subscriber->poll();

    Tappy<support::TestMessage> message0{};
    message0.get_underlying_message_string().set_truncate("message0");
    expected_messages.emplace_back(message0);
    expected_sequence_numbers.emplace_back(0U);
    expected_message_times.emplace_back(0);
    REQUIRE(publisher_result->publish(0, message0));

    subscriber->poll();

    Tappy<support::TestMessage> message1{};
    message1.get_underlying_message_string().set_truncate("message1");
    expected_messages.emplace_back(message1);
    expected_sequence_numbers.emplace_back(1U);
    expected_message_times.emplace_back(1);
    REQUIRE(publisher_result->publish(1, message1));

    subscriber->poll();

    Tappy<support::TestMessage> message2{};
    message2.get_underlying_message_string().set_truncate("message2");
    expected_messages.emplace_back(message2);
    expected_sequence_numbers.emplace_back(2U);
    expected_message_times.emplace_back(2);
    REQUIRE(publisher_result->publish(2, message2));

    subscriber->poll();

    Tappy<support::TestMessage> message3{};
    message3.get_underlying_message_string().set_truncate("message3");
    expected_messages.emplace_back(message3);
    expected_sequence_numbers.emplace_back(3U);
    expected_message_times.emplace_back(3);
    REQUIRE(publisher_result->publish(3, message3));

    subscriber->poll();

    CHECK(actual_messages == expected_messages);
    REQUIRE(actual_message_times == expected_message_times);
    REQUIRE(actual_sequence_numbers == expected_sequence_numbers);
  }

  SECTION("Subscriber receives newest message when it falls behind")
  {
    subscriber->poll();

    Tappy<support::TestMessage> message0{};
    message0.get_underlying_message_string().set_truncate("message0");
    REQUIRE(publisher_result->publish(0, message0));

    Tappy<support::TestMessage> message1{};
    message1.get_underlying_message_string().set_truncate("message1");
    expected_messages.emplace_back(message1);
    expected_sequence_numbers.emplace_back(1U);
    expected_message_times.emplace_back(1);
    REQUIRE(publisher_result->publish(1, message1));

    subscriber->poll();

    Tappy<support::TestMessage> message2{};
    message2.get_underlying_message_string().set_truncate("message2");
    REQUIRE(publisher_result->publish(2, message2));

    Tappy<support::TestMessage> message3{};
    message3.get_underlying_message_string().set_truncate("message3");
    expected_messages.emplace_back(message3);
    expected_sequence_numbers.emplace_back(3U);
    expected_message_times.emplace_back(3);
    REQUIRE(publisher_result->publish(3, message3));

    subscriber->poll();
    subscriber->poll();

    REQUIRE(actual_messages == expected_messages);
    REQUIRE(actual_message_times == expected_message_times);
    REQUIRE(actual_sequence_numbers == expected_sequence_numbers);
  }
}

} // namespace
} // namespace clockwork::tools::tests
