// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/logging/channel_publisher_config_clk_cc.hh"
#include "clockwork/logging/writers/tests/support/test_log_writer_config.hh"
#include "clockwork/logging/writers/tests/support/test_publisher.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/runners/channel_publisher.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>
#include <xxh3.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace clockwork
{
void fill_with_random_bytes(std::span<std::byte> buffer)
{
  std::random_device random_device;
  std::mt19937 gen(random_device());
  std::uniform_int_distribution<uint8_t> distrib(
    std::numeric_limits<uint8_t>::min(), std::numeric_limits<uint8_t>::max());
  for (auto& value : buffer)
  {
    value = static_cast<std::byte>(distrib(gen));
    // 'X' is used to corrupt log files
    while (value == std::byte{'X'})
    {
      value = static_cast<std::byte>(distrib(gen));
    }
  }
}

class MockObserver : public clockwork::pinion::Observer
{
public:
  /// Notify the observer of a new message on a channel.
  /// @param event struct containing details of the event that triggered the notification
  void notify(const Event& /*event*/) override
  {
    ++num_notify_calls;
  }
  int num_notify_calls = 0;
};

class TestMessageFetcher : public MessageFetcher
{
public:
  std::optional<MultiMessageInfoData> try_fetch_message() override
  {
    if (message_index_ >= messages_.size())
    {
      return std::nullopt;
    }
    auto ret_msg = messages_.at(message_index_);
    ++message_index_;
    return ret_msg;
  }

  jewels::expected<void, jewels::MonoError> initialize() override
  {
    return {};
  }

  jewels::BinaryOutcome reset() noexcept override
  {
    message_index_ = 0;
    return jewels::success;
  }

  void add_message(MultiMessageInfoData message_info)
  {
    messages_.emplace_back(std::move(message_info));
  }

private:
  std::vector<MultiMessageInfoData> messages_;
  std::size_t message_index_ = 0;
};
/// Pinion unix domain socket namespace
constexpr auto pinion_namespace = "logging_test";

using ChannelUuid = jewels::Uuid<::clockwork::common::EndpointInstanceId>;
TEST_CASE("Test publishing from a log")
{
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());

  const jewels::testing::TmpDirectoryGuard shm_dir;
  auto publisher_config_ptr = clockwork_logging::tests::get_test_channel_publisher_config();
  auto& publisher_config = *publisher_config_ptr;
  const jewels::time::SyncTime message_time{std::chrono::hours(1)};
  std::vector<MockObserver> observers;
  observers.reserve(publisher_config.get_channels().size());

  ShmPublisherMap channel_map;
  std::vector<clockwork_logging::tests::TestPublisher> test_publishers;
  test_publishers.reserve(publisher_config.get_channels().size());

  for (auto& channel_config : publisher_config.get_mutable_channels())
  {
    // This test wants the channel names to be unique
    channel_config.get_underlying_channel_name().set_truncate(channel_config.get_uuid().to_string());

    auto open_result = clockwork_logging::tests::TestPublisher::try_open(
      memory_resource,
      shm_dir.get_path().string(),
      pinion_namespace,
      channel_config.get_uuid().to_string(),
      channel_config.get_channel_name(),
      channel_config.get_num_slots(),
      channel_config.get_message_size());
    REQUIRE(open_result);
    test_publishers.emplace_back(std::move(open_result).value());
    observers.emplace_back();
    auto& publisher = test_publishers.back();
    auto endpoint_uuid_string = channel_config.get_uuid().to_string();
    auto endpoint_uuid = ChannelUuid::from_string(endpoint_uuid_string);
    REQUIRE(endpoint_uuid);
    REQUIRE(publisher.underlying_publisher());
    channel_map.emplace(endpoint_uuid.value(), publisher.underlying_publisher());
    auto& observer = observers.back();
    REQUIRE(publisher.underlying_publisher()->add_observer(jewels::memory::make_non_null_from_ref(observer)));
  }

  auto message_fetcher = jewels::memory::make_shared<TestMessageFetcher>();
  std::vector<std::vector<std::byte>> underlying_data;
  constexpr size_t messages_per_channel = 10U;
  for (uint32_t i = 0U; i < messages_per_channel; ++i)
  {
    for (uint32_t j = 0U; j < publisher_config.get_channels().size(); ++j)
    {
      const auto& channel_config = publisher_config.get_channels()[j];
      std::vector<std::byte> data(channel_config.get_message_size());
      fill_with_random_bytes(data);
      auto logged_message = MultiMessageInfoData{
        .time_to_publish = message_time,
        .msgs = {{std::pmr::vector<std::byte>(data.begin(), data.end(), memory_resource)}, memory_resource},
        .channel = std::pmr::string(channel_config.get_channel_name(), memory_resource),
      };
      underlying_data.emplace_back(std::move(data));
      message_fetcher->add_message(std::move(logged_message));
    }
  }

  auto channel_publisher = clockwork::ChannelPublisher(
    memory_resource, jewels::memory::make_non_null_from_ref(publisher_config), message_fetcher, channel_map, false);

  REQUIRE(channel_publisher.initialize());
  while (channel_publisher.messages_remaining())
  {
    REQUIRE(channel_publisher.publish_next_message());
  }
  for (const auto& observer : observers)
  {
    REQUIRE(observer.num_notify_calls == messages_per_channel);
  }
}
} // namespace clockwork
