// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/offboard/amendment_writer.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/reader.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/tests/support/test_message_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fmt/format.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>

namespace clockwork_logging::offboard
{
namespace
{

using jewels::ok;

TEST_CASE("Write amendment")
{
  const auto use_relative_path = GENERATE(true, false);
  CAPTURE(use_relative_path);

  constexpr auto amended_log_name = "amended_log";
  constexpr auto amendment_log_name = "amendment_log";

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto amended_log_path = test_dir.get_path() / amended_log_name;
  const auto amendment_log_path = test_dir.get_path() / amendment_log_name;
  Writer amended_writer{memory_resource};

  constexpr LogTimestamp time1{std::chrono::seconds(1)};
  constexpr LogTimestamp time2{std::chrono::seconds(2)};
  constexpr LogTimestamp time3{std::chrono::seconds(3)};
  constexpr LogTimestamp time4{std::chrono::seconds(4)};
  constexpr LogTimestamp time5{std::chrono::seconds(5)};

  constexpr auto channel_name1 = "channel1";
  const auto metadata1 = LoggedChannelMetadata{
    .channel_name = channel_name1,
    .message_encoding = MessageEncoding::tachyon,
    .channel_type = ChannelType::regular,
    .schema_name = clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage1>>::schema_name,
    .schema_encoding = SchemaEncoding::clockwork_tachyon,
    .schema_definition =
      std::string_view{
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage1>>::schema_definition.data(),
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage1>>::schema_definition.size()},
  };
  clockwork::Tappy<clockwork_logging::tests::TestMessage1> message1;
  onboard::tests::fill_with_random_bytes(message1.get_mutable_data());

  constexpr auto channel_name2 = "channel2";
  const auto metadata2 = LoggedChannelMetadata{
    .channel_name = channel_name2,
    .message_encoding = MessageEncoding::tachyon,
    .channel_type = ChannelType::regular,
    .schema_name = clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage2>>::schema_name,
    .schema_encoding = SchemaEncoding::clockwork_tachyon,
    .schema_definition =
      std::string_view{
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage2>>::schema_definition.data(),
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage2>>::schema_definition.size()},
  };
  clockwork::Tappy<clockwork_logging::tests::TestMessage2> message2;
  onboard::tests::fill_with_random_bytes(message2.get_mutable_data());

  REQUIRE(amended_writer.open(amended_log_path.string()));
  REQUIRE(amended_writer.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage1>>(channel_name1));
  REQUIRE(amended_writer.create_channel(metadata2));

  REQUIRE(amended_writer.write(channel_name1, 1U, time1, time1, message1, false));
  REQUIRE(amended_writer.write(channel_name2, 2U, time2, time2, message2, false));
  REQUIRE(amended_writer.close());

  AmendmentWriter amendment_writer{memory_resource};
  const auto amended_log_path_str =
    use_relative_path ? fmt::format("../{}", amended_log_name) : std::string{amended_log_path.string()};

  SECTION("Empty amendment")
  {
    REQUIRE(ok(amendment_writer.open(amendment_log_path.string(), amended_log_path_str)));
    REQUIRE(ok(amendment_writer.close()));

    const auto chunk_reader_writer_factory = std::make_shared<ChunkReaderWriterFactory<>>(memory_resource);
    Reader reader{memory_resource, amendment_log_path.string(), chunk_reader_writer_factory};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 2U);
    REQUIRE((*metadata_result)->at(channel_name1) == metadata1);
    REQUIRE((*metadata_result)->at(channel_name2) == metadata2);

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == 2U);
    REQUIRE((*metrics_result)->byte_count == sizeof(message1) + sizeof(message2));
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time1, time2});
    REQUIRE((*metrics_result)->metrics_map.size() == 2U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).byte_count == sizeof(message1));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval == LogInterval{time1, time1});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == sizeof(message2));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time2, time2});

    REQUIRE(reader.open());
    REQUIRE(reader);

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == time1);
    REQUIRE(read_result->transmit_time == time1);
    REQUIRE(read_result->header.empty());
    REQUIRE_FALSE(read_result->is_lite_compressed);
    REQUIRE(read_result->data.size() == sizeof(message1));
    REQUIRE(std::memcmp(read_result->data.data(), &message1, sizeof(message1)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.empty());
    REQUIRE_FALSE(read_result->is_lite_compressed);
    REQUIRE(read_result->data.size() == sizeof(message2));
    REQUIRE(std::memcmp(read_result->data.data(), &message2, sizeof(message2)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Amendment adds new channel")
  {
    constexpr auto channel_name3 = "channel3";
    const auto metadata3 = LoggedChannelMetadata{
      .channel_name = channel_name3,
      .message_encoding = MessageEncoding::tachyon,
      .channel_type = ChannelType::regular,
      .schema_name = clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage3>>::schema_name,
      .schema_encoding = SchemaEncoding::clockwork_tachyon,
      .schema_definition =
        std::string_view{
          clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage3>>::schema_definition.data(),
          clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage3>>::schema_definition.size()},
      .is_amended = true,
    };
    clockwork::Tappy<clockwork_logging::tests::TestMessage3> message3;
    onboard::tests::fill_with_random_bytes(message3.get_mutable_data());

    REQUIRE(ok(amendment_writer.open(amendment_log_path.string(), amended_log_path_str)));
    REQUIRE(
      ok(amendment_writer.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage3>>(channel_name3)));

    REQUIRE(ok(amendment_writer.write(channel_name3, 3U, time3, time3, message3, false)));
    const std::span<const std::byte> message3_span = std::as_bytes(jewels::as_single_item_span(message3));
    REQUIRE(ok(amendment_writer.write(
      LoggedMessage{
        .channel_name = channel_name3,
        .sequence_number = 4U,
        .log_time = time4,
        .transmit_time = time4,
        .data = message3_span,
        .is_repeated_persistent = false,
        .message_encoding = static_cast<MessageEncoding>(
          clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage3>>::message_encoding),
      })));
    REQUIRE(ok(amendment_writer.write(
      ZeroCopyLoggedMessage{
        .channel_name = channel_name3,
        .sequence_number = 5U,
        .log_time = time5,
        .transmit_time = time5,
        .data = jewels::as_single_item_span(message3_span),
        .is_repeated_persistent = false,
        .message_encoding = static_cast<MessageEncoding>(
          clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage3>>::message_encoding),
      })));

    REQUIRE(ok(amendment_writer.close()));

    const auto chunk_reader_writer_factory = std::make_shared<ChunkReaderWriterFactory<>>(memory_resource);
    Reader reader{memory_resource, amendment_log_path.string(), chunk_reader_writer_factory};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 3U);
    REQUIRE((*metadata_result)->at(channel_name1) == metadata1);
    REQUIRE((*metadata_result)->at(channel_name2) == metadata2);
    REQUIRE((*metadata_result)->at(channel_name3) == metadata3);

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == 5U);
    REQUIRE((*metrics_result)->byte_count == sizeof(message1) + sizeof(message2) + (3U * sizeof(message3)));
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time1, time5});
    REQUIRE((*metrics_result)->metrics_map.size() == 3U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).byte_count == sizeof(message1));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval == LogInterval{time1, time1});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == sizeof(message2));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time2, time2});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).message_count == 3U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).byte_count == 3U * sizeof(message3));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).transmit_time_interval == LogInterval{time3, time5});

    REQUIRE(reader.open());
    REQUIRE(reader);

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == time1);
    REQUIRE(read_result->transmit_time == time1);
    REQUIRE(read_result->header.empty());
    REQUIRE_FALSE(read_result->is_lite_compressed);
    REQUIRE(read_result->data.size() == sizeof(message1));
    REQUIRE(std::memcmp(read_result->data.data(), &message1, sizeof(message1)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.empty());
    REQUIRE_FALSE(read_result->is_lite_compressed);
    REQUIRE(read_result->data.size() == sizeof(message2));
    REQUIRE(std::memcmp(read_result->data.data(), &message2, sizeof(message2)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name3);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == time3);
    REQUIRE(read_result->transmit_time == time3);
    REQUIRE(read_result->header.empty());
    REQUIRE_FALSE(read_result->is_lite_compressed);
    REQUIRE(read_result->data.size() == sizeof(message3));
    REQUIRE(std::memcmp(read_result->data.data(), &message3, sizeof(message3)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name3);
    REQUIRE(read_result->sequence_number == 4U);
    REQUIRE(read_result->log_time == time4);
    REQUIRE(read_result->transmit_time == time4);
    REQUIRE(read_result->header.empty());
    REQUIRE_FALSE(read_result->is_lite_compressed);
    REQUIRE(read_result->data.size() == sizeof(message3));
    REQUIRE(std::memcmp(read_result->data.data(), &message3, sizeof(message3)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name3);
    REQUIRE(read_result->sequence_number == 5U);
    REQUIRE(read_result->log_time == time5);
    REQUIRE(read_result->transmit_time == time5);
    REQUIRE(read_result->header.empty());
    REQUIRE_FALSE(read_result->is_lite_compressed);
    REQUIRE(read_result->data.size() == sizeof(message3));
    REQUIRE(std::memcmp(read_result->data.data(), &message3, sizeof(message3)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Amendment replaces existing channel")
  {
    const auto metadata3 = LoggedChannelMetadata{
      .channel_name = channel_name1,
      .message_encoding = MessageEncoding::tachyon,
      .channel_type = ChannelType::regular,
      .schema_name = clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage3>>::schema_name,
      .schema_encoding = SchemaEncoding::clockwork_tachyon,
      .schema_definition =
        std::string_view{
          clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage3>>::schema_definition.data(),
          clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage3>>::schema_definition.size()},
      .is_amended = true,
    };
    clockwork::Tappy<clockwork_logging::tests::TestMessage3> message3;
    onboard::tests::fill_with_random_bytes(message3.get_mutable_data());

    REQUIRE(ok(amendment_writer.open(amendment_log_path.string(), amended_log_path_str)));
    REQUIRE(
      ok(amendment_writer.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage3>>(channel_name1)));

    REQUIRE(ok(amendment_writer.write(channel_name1, 3U, time3, time3, message3, false)));

    REQUIRE(ok(amendment_writer.close()));

    const auto chunk_reader_writer_factory = std::make_shared<ChunkReaderWriterFactory<>>(memory_resource);
    Reader reader{memory_resource, amendment_log_path.string(), chunk_reader_writer_factory};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 2U);
    REQUIRE((*metadata_result)->at(channel_name1) == metadata3);
    REQUIRE((*metadata_result)->at(channel_name2) == metadata2);

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == 2U);
    REQUIRE((*metrics_result)->byte_count == sizeof(message2) + sizeof(message3));
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time2, time3});
    REQUIRE((*metrics_result)->metrics_map.size() == 2U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).byte_count == sizeof(message3));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval == LogInterval{time3, time3});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == sizeof(message2));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time2, time2});

    REQUIRE(reader.open());
    REQUIRE(reader);

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.empty());
    REQUIRE_FALSE(read_result->is_lite_compressed);
    REQUIRE(read_result->data.size() == sizeof(message2));
    REQUIRE(std::memcmp(read_result->data.data(), &message2, sizeof(message2)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == time3);
    REQUIRE(read_result->transmit_time == time3);
    REQUIRE(read_result->header.empty());
    REQUIRE_FALSE(read_result->is_lite_compressed);
    REQUIRE(read_result->data.size() == sizeof(message3));
    REQUIRE(std::memcmp(read_result->data.data(), &message3, sizeof(message3)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Open failures")
  {
    SECTION("Log already open")
    {
      REQUIRE(ok(amendment_writer.open(amendment_log_path.string(), amended_log_path_str)));
      REQUIRE(amendment_writer.open(amendment_log_path.string(), amended_log_path_str).get() == LogError::already_open);
    }

    SECTION("Invalid amendment log URI")
    {
      REQUIRE(amendment_writer.open("s3://host_but_no_path", amended_log_path_str).get() == LogError::invalid_log_uri);
    }

    SECTION("Invalid amended log URI")
    {
      REQUIRE(
        amendment_writer.open(amended_log_path.string(), "s3://host_but_no_path").get() == LogError::invalid_log_uri);
    }

    SECTION("Amendment directory does not exist")
    {
      REQUIRE(
        amendment_writer
          .open(amendment_log_path.string(), use_relative_path ? "../__NO_SUCH_DIRECTORY__" : "/__NO_SUCH_DIRECTORY__")
          .get() == LogError::no_such_file_or_directory);
    }

    SECTION("Amendment log already exists")
    {
      REQUIRE(
        amendment_writer.open(amended_log_path.string(), amended_log_path_str).get() == LogError::log_already_exists);
    }
  }

  SECTION("Close fails")
  {
    REQUIRE(amendment_writer.close().get() == LogError::not_open);
  }

  SECTION("Create channel fails")
  {
    SECTION("Create channel with metadata")
    {
      REQUIRE(ok(amendment_writer.open(amendment_log_path.string(), amended_log_path_str)));
      REQUIRE(ok(amendment_writer.create_channel(metadata2)));
      REQUIRE(amendment_writer.create_channel(metadata2).get() == LogError::channel_already_exists);
    }

    SECTION("Create channel from type")
    {
      REQUIRE(ok(amendment_writer.open(amendment_log_path.string(), amended_log_path_str)));
      REQUIRE(
        ok(amendment_writer.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage1>>(channel_name1)));
      REQUIRE(
        amendment_writer.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage1>>(channel_name1)
          .get() == LogError::channel_already_exists);
    }
  }

  SECTION("Write fails")
  {
    SECTION("Write LoggedMessage")
    {
      REQUIRE(amendment_writer.write(LoggedMessage{}).get() == LogError::not_open);
    }

    SECTION("Write ZeroCopyLoggedMessage")
    {
      REQUIRE(amendment_writer.write(ZeroCopyLoggedMessage{}).get() == LogError::not_open);
    }

    SECTION("Write TappyType")
    {
      REQUIRE(amendment_writer.write(channel_name1, 1U, time1, time1, message1, false).get() == LogError::not_open);
    }
  }
}

} // namespace
} // namespace clockwork_logging::offboard
