// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_publisher_config_clk_cc.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_playback/end_of_log_clk_cc.hh"
#include "clockwork/logging/log_playback/log_message_fetcher.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/log_uuid.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/logging/writers/tests/support/test_log_writer_config.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/runners/channel_publisher.hh"
#include "clockwork/serialization/py/tests/support/simple_schema_v1_clk_cc.hh"
#include "clockwork/serialization/py/tests/support/simple_schema_v2_clk_cc.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging::tests
{
namespace
{

// Helper to append the end_of_log channel metadata to a ChannelPublisherConfig.
void add_end_of_log_channel(jewels::Out<clockwork::Tappy<ChannelPublisherConfig<>>> config)
{
  using EndOfLogMessageType = clockwork::Tappy<clockwork_logging::EndOfLog>;
  auto& eol_channel = config->get_underlying_channels().emplace_back();
  eol_channel.set_uuid(LogUuid::random_uuid());
  eol_channel.set_num_slots(1U);
  eol_channel.set_message_size(sizeof(EndOfLogMessageType));
  eol_channel.get_underlying_channel_name().set_truncate(end_of_log_channel_name);
  const auto eol_schema_definition =
    std::as_bytes(std::span{clockwork::LoggingTraits<EndOfLogMessageType>::schema_definition});
  eol_channel.get_underlying_schema_definition().insert(
    eol_channel.get_underlying_schema_definition().begin(), eol_schema_definition.begin(), eol_schema_definition.end());
  eol_channel.get_underlying_module_name().set_truncate(clockwork::LoggingTraits<EndOfLogMessageType>::module_name);
  eol_channel.get_underlying_source_file_name().set_truncate(
    clockwork::LoggingTraits<EndOfLogMessageType>::source_file_name);
  eol_channel.get_underlying_class_name().set_truncate(clockwork::LoggingTraits<EndOfLogMessageType>::class_name);
}

TEST_CASE("Test Fetching from a log")
{
  setenv("VEHICLE_ID", "unknown", 1); // NOLINT(concurrency-mt-unsafe) this test runs single-threaded.

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  const auto expected_log_path = (test_dir.get_path() / "test").string();

  const jewels::testing::TmpDirectoryGuard shm_dir;
  const auto publisher_config_ptr = tests::get_test_channel_publisher_config();
  const auto& publisher_config = *publisher_config_ptr;
  const LogTimestamp start_message_time{std::chrono::hours(1)};
  const std::chrono::seconds message_interval(1);
  constexpr size_t messages_per_channel = 10U;

  {
    offboard::Writer writer(memory_resource);
    REQUIRE(writer.open(expected_log_path));

    for (const auto& channel_config : publisher_config.get_channels())
    {
      REQUIRE(
        writer.create_channel<clockwork::Tappy<clockwork::tests::SimpleSchemaV1>>(channel_config.get_channel_name()));
    }

    auto message_time = start_message_time;
    for (uint32_t i = 0U; i < messages_per_channel; ++i)
    {
      for (uint32_t j = 0U; j < publisher_config.get_channels().size(); ++j)
      {
        const auto& channel_config = publisher_config.get_channels()[j];
        clockwork::Tappy<clockwork::tests::SimpleSchemaV1> msg;
        msg.set_integer_field(static_cast<int32_t>((i * publisher_config.get_channels().size()) + j));
        REQUIRE(writer.write(channel_config.get_channel_name(), i, message_time, message_time, msg));
        message_time += message_interval;
      }
    }
    REQUIRE(writer.close());
  }

  SECTION("channel1 isn't ugpraded, channel2 is upgraded to SimpleSchemaV2")
  {
    auto log_fetcher = LogMessageFetcher(
      expected_log_path, jewels::memory::make_non_null_from_ref(publisher_config), {}, memory_resource);

    REQUIRE(log_fetcher.initialize());
    auto message_time = start_message_time;
    for (uint32_t i = 0U; i < messages_per_channel; ++i)
    {
      CAPTURE(i);
      for (uint32_t j = 0U; j < publisher_config.get_channels().size(); ++j)
      {
        CAPTURE(j);
        const auto& channel_config = publisher_config.get_channels()[j];
        const auto maybe_message = log_fetcher.try_fetch_message();
        REQUIRE(maybe_message);
        REQUIRE(maybe_message->channel == channel_config.get_channel_name());
        REQUIRE(maybe_message->msgs.size() == 1U);
        const auto& msg_data = maybe_message->msgs.front();
        if (channel_config.get_channel_name() == "channel1")
        {
          clockwork::Tappy<clockwork::tests::SimpleSchemaV1> msg;
          REQUIRE(msg_data.size() == sizeof(msg));
          std::memcpy(&msg, msg_data.data(), msg_data.size());
          REQUIRE(msg.get_integer_field() == static_cast<int32_t>((i * publisher_config.get_channels().size()) + j));
        }
        else
        {
          clockwork::Tappy<clockwork::tests::SimpleSchemaV2> msg;
          REQUIRE(msg_data.size() == sizeof(msg));
          std::memcpy(&msg, msg_data.data(), msg_data.size());
          REQUIRE(msg.get_integer_field() == static_cast<int64_t>((i * publisher_config.get_channels().size()) + j));
          REQUIRE(msg.get_string_field().empty());
        }
        message_time += message_interval;
      }
    }
    REQUIRE_FALSE(log_fetcher.try_fetch_message());
  }

  SECTION("End of log message NOT emitted when end_of_log channel not configured")
  {
    auto log_fetcher = LogMessageFetcher(
      expected_log_path, jewels::memory::make_non_null_from_ref(publisher_config), {}, memory_resource);
    REQUIRE(log_fetcher.initialize());

    size_t normal_message_count = 0U;
    std::optional<std::pmr::string> unexpected_channel;
    while (auto maybe_msg = log_fetcher.try_fetch_message())
    {
      if (maybe_msg->channel == end_of_log_channel_name)
      {
        unexpected_channel = maybe_msg->channel; // Should never happen in this section
        break;
      }
      ++normal_message_count;
    }
    // We should have read exactly messages_per_channel * publisher_config.get_channels().size() messages.
    REQUIRE(normal_message_count == messages_per_channel * publisher_config.get_channels().size());
    // And we should not have seen an end_of_log message.
    REQUIRE_FALSE(unexpected_channel.has_value());
  }

  SECTION("Invalid Log")
  {
    auto log_fetcher =
      LogMessageFetcher("junk.log", jewels::memory::make_non_null_from_ref(publisher_config), {}, memory_resource);

    REQUIRE_FALSE(log_fetcher.initialize());
  }

  SECTION("End of log message emitted when end_of_log channel is configured")
  {
    auto publisher_config_with_eol_ptr = tests::get_test_channel_publisher_config();
    REQUIRE(publisher_config_with_eol_ptr);
    add_end_of_log_channel(jewels::Out{*publisher_config_with_eol_ptr});

    auto log_fetcher = LogMessageFetcher(
      expected_log_path, jewels::memory::make_non_null_from_ref(*publisher_config_with_eol_ptr), {}, memory_resource);
    REQUIRE(log_fetcher.initialize());

    // Read all real messages (channel1 + channel2) and remember the last real publish time.
    std::optional<jewels::time::SyncTime> last_real_publish_time;
    for (uint32_t i = 0U; i < messages_per_channel; ++i)
    {
      for (uint32_t j = 0U; j < publisher_config.get_channels().size(); ++j)
      {
        CAPTURE(i, j);
        const auto maybe_message = log_fetcher.try_fetch_message();
        REQUIRE(maybe_message);
        REQUIRE((maybe_message->channel == "channel1" || maybe_message->channel == "channel2"));
        last_real_publish_time = maybe_message->time_to_publish; // store publish time
      }
    }

    // Next call should return the synthesized end_of_log message.
    const auto maybe_eol_message = log_fetcher.try_fetch_message();
    REQUIRE(maybe_eol_message);
    REQUIRE(maybe_eol_message->channel == end_of_log_channel_name);
    REQUIRE(maybe_eol_message->msgs.size() == 1U);

    REQUIRE(last_real_publish_time);
    REQUIRE(maybe_eol_message->time_to_publish == *last_real_publish_time);
    const auto& eol_msg_data = maybe_eol_message->msgs.front();
    using EndOfLogMessageType = clockwork::Tappy<clockwork_logging::EndOfLog>;
    REQUIRE(eol_msg_data.size() == sizeof(EndOfLogMessageType));
    EndOfLogMessageType eol_msg;
    std::memcpy(&eol_msg, eol_msg_data.data(), eol_msg_data.size());
    REQUIRE(eol_msg.get_end_of_log());

    // Any further call(s) should return nullopt (no duplicate end_of_log)
    REQUIRE_FALSE(log_fetcher.try_fetch_message());
    REQUIRE_FALSE(log_fetcher.try_fetch_message()); // second consecutive check
  }
}

} // namespace
} // namespace clockwork_logging::tests
