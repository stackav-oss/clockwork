// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/onboard/clockwork_writer_policy.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/serialization.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/tests/support/test_message_clk_cc.hh"
#include "clockwork/logging/tools/offboard_converter/convert_to_offboard.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace clockwork_logging
{
namespace
{

TEST_CASE("Convert to offboard log format")
{
  constexpr size_t max_write_mib_per_sec = 100U;
  constexpr auto max_log_file_duration = std::chrono::seconds{0};
  const jewels::testing::TmpDirectoryGuard test_dir;

  // Write a simple test log.

  const auto onboard_path = test_dir.get_path() / "onboard";

  auto topics = std::vector<std::string>({"/channel1", "/channel2"});
  auto msgs = std::map<std::string, std::vector<clockwork::Tappy<tests::TestMessage>>>();
  const auto schema_definition = std::string{
    clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_definition.data(),
    clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_definition.size()};

  {
    const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
    onboard::Writer<onboard::ClockworkWriterPolicy<>> writer{
      memory_resource,
      memory_resource,
      max_write_mib_per_sec,
      max_log_file_duration,
      onboard::WriterEnvironment::normal};
    REQUIRE(writer.open_log(onboard_path.string(), "onboard", jewels::time::SteadyClock::now()));
    REQUIRE(writer.add_channel(
      onboard::LoggedChannelMetadata{
        .channel_name = "/channel1",
        .compression_type = CompressionType::none,
        .message_encoding = static_cast<MessageEncoding>(
          clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::message_encoding),
        .channel_type = ChannelType::regular,
        .schema_name = clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_name,
        .schema_encoding =
          static_cast<SchemaEncoding>(clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_encoding),
        .schema_definition = schema_definition,
      },
      jewels::time::SteadyClock::now()));
    REQUIRE(writer.add_channel(
      onboard::LoggedChannelMetadata{
        .channel_name = "/channel2",
        .compression_type = CompressionType::none,
        .message_encoding = static_cast<MessageEncoding>(
          clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::message_encoding),
        .channel_type = ChannelType::regular,
        .schema_name = clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_name,
        .schema_encoding =
          static_cast<SchemaEncoding>(clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_encoding),
        .schema_definition = schema_definition,
      },
      jewels::time::SteadyClock::now()));

    for (int64_t i = 0; i < 10; ++i)
    {
      for (const auto& topic : topics)
      {
        auto msg = clockwork::Tappy<tests::TestMessage>();
        msg.get_underlying_message_string().set_truncate(fmt::format("Test{}", i));

        REQUIRE(writer.log_message(
          onboard::Message{
            .channel_name = topic,
            .sequence_number = 0U,
            .log_time = LogTimestamp(i),
            .message_time = LogTimestamp(i),
            .header = {},
            .data = std::as_bytes(std::span{&msg, 1}),
          },
          false,
          jewels::time::SteadyClock::now()));
        msgs[topic].push_back(msg);
      }
    }
    REQUIRE(writer.close_log(jewels::time::SteadyClock::now()));
    REQUIRE(writer.drain_async_operations());
  }

  const auto offboard_log_path = test_dir.get_path() / "offboard";
  convert_to_offboard(onboard_path.string(), offboard_log_path.string());

  // Check the log topics
  auto reader = make_reader(offboard_log_path.string(), {}, {});

  const auto expected_topics = std::vector<TopicMetadata>(
    {{
       .name = "/channel1",
       .type = std::string{clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_name},
       .message_encoding =
         static_cast<MessageEncoding>(clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::message_encoding),
       .channel_type = ChannelType::regular,
       .schema_encoding =
         static_cast<SchemaEncoding>(clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_encoding),
       .schema_definition = schema_definition,
     },
     {
       .name = "/channel2",
       .type = std::string{clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_name},
       .message_encoding =
         static_cast<MessageEncoding>(clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::message_encoding),
       .channel_type = ChannelType::regular,
       .schema_encoding =
         static_cast<SchemaEncoding>(clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_encoding),
       .schema_definition = schema_definition,
     }});

  auto actual_topics = reader->get_metadata();
  REQUIRE(expected_topics == actual_topics);

  // Read all messages
  REQUIRE(reader->open({}));

  auto actual_msgs = std::map<std::string, std::vector<clockwork::Tappy<tests::TestMessage>>>();
  while (auto msg = reader->next_message())
  {
    REQUIRE(msg);
    deserialize_tachyon(actual_msgs[std::string(msg->topic)].emplace_back(), msg->data);
  }

  REQUIRE(msgs == actual_msgs);
}

} // namespace
} // namespace clockwork_logging
