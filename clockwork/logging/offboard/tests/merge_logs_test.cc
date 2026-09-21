// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/offboard/amendment_writer.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/merge_logs.hh"
#include "clockwork/logging/offboard/reader.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/v1/log_metadata.pb.h"
#include "clockwork/logging/offboard/v1/log_union.pb.h"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/tests/support/test_message_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fmt/format.h>
#include <google/protobuf/repeated_ptr_field.h>

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace clockwork_logging::offboard
{
namespace
{

using jewels::ok;

TEST_CASE("merge_logs")
{
  constexpr auto source_log1_name = "source_log1";
  constexpr auto source_log2_name = "source_log2";
  constexpr auto source_log3_name = "source_log3";
  constexpr auto dest_log_name = "dest_log";

  const auto recover_metadata = GENERATE(false, true);
  CAPTURE(recover_metadata);

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto chunk_reader_factory = std::make_shared<ChunkReaderWriterFactory<>>(memory_resource);
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto source_log1_path = test_dir.get_path() / source_log1_name;
  const std::string source_log1_path_str{source_log1_path.c_str()};
  const auto source_log2_path = test_dir.get_path() / source_log2_name;
  const std::string source_log2_path_str{source_log2_path.c_str()};
  const auto source_log3_path = test_dir.get_path() / source_log3_name;
  const std::string source_log3_path_str{source_log3_path.c_str()};
  const auto dest_path = test_dir.get_path() / dest_log_name;
  const std::string dest_log_path{dest_path.string_view()};
  Writer writer1{memory_resource};
  Writer writer2{memory_resource};
  Writer writer3{memory_resource};

  constexpr LogTimestamp time1{std::chrono::seconds(1)};
  constexpr LogTimestamp time2{std::chrono::seconds(2)};
  constexpr LogTimestamp time3{std::chrono::seconds(3)};
  constexpr LogTimestamp time4{std::chrono::seconds(4)};

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

  REQUIRE(writer1.open(source_log1_path_str));
  REQUIRE(writer1.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage1>>(channel_name1));
  REQUIRE(writer1.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage2>>(channel_name2));

  REQUIRE(writer1.write(channel_name1, 1U, time1, time1, message1));
  REQUIRE(writer1.write(channel_name2, 2U, time3, time3, message2));

  REQUIRE(writer1.close());

  constexpr auto channel_name3 = "channel3";
  const auto metadata3 = LoggedChannelMetadata{
    .channel_name = channel_name3,
    .message_encoding = MessageEncoding::tachyon,
    .channel_type = ChannelType::regular,
    .schema_name = clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage1>>::schema_name,
    .schema_encoding = SchemaEncoding::clockwork_tachyon,
    .schema_definition =
      std::string_view{
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage1>>::schema_definition.data(),
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage1>>::schema_definition.size()},
  };
  clockwork::Tappy<clockwork_logging::tests::TestMessage1> message3;
  onboard::tests::fill_with_random_bytes(message3.get_mutable_data());

  constexpr auto channel_name4 = "channel4";
  const auto metadata4 = LoggedChannelMetadata{
    .channel_name = channel_name4,
    .message_encoding = MessageEncoding::tachyon,
    .channel_type = ChannelType::regular,
    .schema_name = clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage2>>::schema_name,
    .schema_encoding = SchemaEncoding::clockwork_tachyon,
    .schema_definition =
      std::string_view{
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage2>>::schema_definition.data(),
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage2>>::schema_definition.size()},
  };
  clockwork::Tappy<clockwork_logging::tests::TestMessage2> message4;
  onboard::tests::fill_with_random_bytes(message4.get_mutable_data());

  REQUIRE(writer2.open(source_log2_path_str));
  REQUIRE(writer2.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage1>>(channel_name3));
  REQUIRE(writer2.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage2>>(channel_name4));

  REQUIRE(writer2.write(channel_name3, 3U, time2, time2, message3));
  REQUIRE(writer2.write(channel_name4, 4U, time4, time4, message4));

  REQUIRE(writer2.close());

  constexpr auto channel_name5 = "channel5";
  const auto metadata5 = LoggedChannelMetadata{
    .channel_name = channel_name5,
    .message_encoding = MessageEncoding::tachyon,
    .channel_type = ChannelType::regular,
    .schema_name = clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage3>>::schema_name,
    .schema_encoding = SchemaEncoding::clockwork_tachyon,
    .schema_definition =
      std::string_view{
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage3>>::schema_definition.data(),
        clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage3>>::schema_definition.size()},
  };

  REQUIRE(writer3.open(source_log3_path_str));
  REQUIRE(writer3.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage3>>(channel_name5));
  REQUIRE(writer3.close());

  if (recover_metadata)
  {
    const auto log1_metadata_path = std::filesystem::path{source_log1_path_str} / "stack_log_metadata.pbtxt";
    const auto log2_metadata_path = std::filesystem::path{source_log2_path_str} / "stack_log_metadata.pbtxt";
    const auto log3_metadata_path = std::filesystem::path{source_log3_path_str} / "stack_log_metadata.pbtxt";
    std::filesystem::remove(log1_metadata_path);
    std::filesystem::remove(log2_metadata_path);
    std::filesystem::remove(log3_metadata_path);
  }

  std::vector<std::string_view> source_logs{source_log1_path_str, source_log2_path_str, source_log3_path_str};

  SECTION("Merge entire log")
  {
    REQUIRE(merge_logs(memory_resource, source_logs, dest_log_path));

    Reader reader{memory_resource, dest_log_path, chunk_reader_factory};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 4U);
    REQUIRE((*metadata_result)->at(channel_name1) == metadata1);
    REQUIRE((*metadata_result)->at(channel_name2) == metadata2);
    REQUIRE((*metadata_result)->at(channel_name3) == metadata3);
    REQUIRE((*metadata_result)->at(channel_name4) == metadata4);

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == 4U);
    REQUIRE((*metrics_result)->byte_count == sizeof(message1) + sizeof(message2) + sizeof(message3) + sizeof(message4));
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time1, time4});
    REQUIRE((*metrics_result)->metrics_map.size() == 4U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).byte_count == sizeof(message1));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval == LogInterval{time1, time1});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == sizeof(message2));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time3, time3});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).byte_count == sizeof(message3));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).transmit_time_interval == LogInterval{time2, time2});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name4).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name4).byte_count == sizeof(message4));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name4).transmit_time_interval == LogInterval{time4, time4});

    REQUIRE(reader.open());
    REQUIRE(reader);

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == time1);
    REQUIRE(read_result->transmit_time == time1);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message1));
    REQUIRE(std::memcmp(read_result->data.data(), &message1, sizeof(message1)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name3);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message3));
    REQUIRE(std::memcmp(read_result->data.data(), &message3, sizeof(message3)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time3);
    REQUIRE(read_result->transmit_time == time3);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message2));
    REQUIRE(std::memcmp(read_result->data.data(), &message2, sizeof(message2)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name4);
    REQUIRE(read_result->sequence_number == 4U);
    REQUIRE(read_result->log_time == time4);
    REQUIRE(read_result->transmit_time == time4);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message4));
    REQUIRE(std::memcmp(read_result->data.data(), &message4, sizeof(message4)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Copy with filter")
  {
    SECTION("Filter by time")
    {
      RelativeInterval log_interval{
        .start_offset = time2 - time1,
        .end_offset = time3 - time1,
      };
      REQUIRE(merge_logs(memory_resource, source_logs, dest_log_path, {}, log_interval));
    }

    SECTION("Filter by channel")
    {
      std::pmr::unordered_set<std::pmr::string> desired_channels{channel_name2, channel_name3};
      REQUIRE(merge_logs(memory_resource, source_logs, dest_log_path, desired_channels, {}));
    }

    Reader reader{memory_resource, dest_log_path, chunk_reader_factory};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 2U);
    REQUIRE((*metadata_result)->at(channel_name2) == metadata2);
    REQUIRE((*metadata_result)->at(channel_name3) == metadata3);

    REQUIRE(reader.get_log_interval() == LogInterval{time2, time3});

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == 2U);
    REQUIRE((*metrics_result)->byte_count == sizeof(message2) + sizeof(message3));
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time2, time3});
    REQUIRE((*metrics_result)->metrics_map.size() == 2U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == sizeof(message2));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time3, time3});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).byte_count == sizeof(message3));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).transmit_time_interval == LogInterval{time2, time2});

    REQUIRE(reader.open());
    REQUIRE(reader);

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name3);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message3));
    REQUIRE(std::memcmp(read_result->data.data(), &message3, sizeof(message3)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time3);
    REQUIRE(read_result->transmit_time == time3);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message2));
    REQUIRE(std::memcmp(read_result->data.data(), &message2, sizeof(message2)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Merge unions")
  {
    constexpr auto union1_name = "union_log1";
    constexpr auto union2_name = "union_log2";
    constexpr auto union_merge_name = "union_merge_log";

    const auto union1_path = test_dir.get_path() / union1_name;
    const auto& union1_path_str = union1_path.string();
    const auto union2_path = test_dir.get_path() / union2_name;
    const auto& union2_path_str = union2_path.string();
    const auto union_merge_path = test_dir.get_path() / union_merge_name;

    std::vector<std::string_view> source_logs1{source_log1_path_str, source_log2_path_str};
    std::vector<std::string_view> source_logs2{source_log2_path_str, source_log3_path_str};

    REQUIRE(write_merge_union(memory_resource, source_logs1, union1_path.string()));
    REQUIRE(write_merge_union(memory_resource, source_logs2, union2_path.string()));

    const auto log_union1_path = union1_path / log_union_filename;
    const auto union1_result =
      chunk_reader_factory->read_text_proto<::clockwork::logging::offboard::v1::LogUnion>(log_union1_path.string());
    REQUIRE(union1_result);
    const auto& union1 = union1_result.value();
    REQUIRE(union1.log_union_entry_size() == 2);
    REQUIRE(union1.log_union_entry(0).has_absolute_path());
    REQUIRE(union1.log_union_entry(0).absolute_path() == source_log1_path_str);
    REQUIRE(union1.log_union_entry(1).has_absolute_path());
    REQUIRE(union1.log_union_entry(1).absolute_path() == source_log2_path_str);

    const auto log_union2_path = union2_path / log_union_filename;
    const auto union2_result =
      chunk_reader_factory->read_text_proto<::clockwork::logging::offboard::v1::LogUnion>(log_union2_path.string());
    REQUIRE(union2_result);
    const auto& union2 = union2_result.value();
    REQUIRE(union2.log_union_entry_size() == 2);
    REQUIRE(union2.log_union_entry(0).has_absolute_path());
    REQUIRE(union2.log_union_entry(0).absolute_path() == source_log2_path_str);
    REQUIRE(union2.log_union_entry(1).has_absolute_path());
    REQUIRE(union2.log_union_entry(1).absolute_path() == source_log3_path_str);

    std::vector<std::string_view> union_logs{union1_path_str, union2_path_str};
    REQUIRE(write_merge_union(memory_resource, union_logs, union_merge_path.string()));

    const auto union_merge_result = chunk_reader_factory->read_text_proto<::clockwork::logging::offboard::v1::LogUnion>(
      (union_merge_path / log_union_filename).string());
    REQUIRE(union_merge_result);
    const auto& union_merge = union_merge_result.value();
    REQUIRE(union_merge.log_union_entry_size() == 3);
    REQUIRE(union_merge.log_union_entry(0).has_absolute_path());
    REQUIRE(union_merge.log_union_entry(0).absolute_path() == source_log1_path_str);
    REQUIRE(union_merge.log_union_entry(1).has_absolute_path());
    REQUIRE(union_merge.log_union_entry(1).absolute_path() == source_log2_path_str);
    REQUIRE(union_merge.log_union_entry(2).has_absolute_path());
    REQUIRE(union_merge.log_union_entry(2).absolute_path() == source_log3_path_str);

    Reader reader{memory_resource, union_merge_path.string(), chunk_reader_factory};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 5U);
    REQUIRE((*metadata_result)->at(channel_name1) == metadata1);
    REQUIRE((*metadata_result)->at(channel_name2) == metadata2);
    REQUIRE((*metadata_result)->at(channel_name3) == metadata3);
    REQUIRE((*metadata_result)->at(channel_name4) == metadata4);
    REQUIRE((*metadata_result)->at(channel_name5) == metadata5);

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == 4U);
    REQUIRE((*metrics_result)->byte_count == sizeof(message1) + sizeof(message2) + sizeof(message3) + sizeof(message4));
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time1, time4});
    REQUIRE((*metrics_result)->metrics_map.size() == 4U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).byte_count == sizeof(message1));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval == LogInterval{time1, time1});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == sizeof(message2));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time3, time3});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).byte_count == sizeof(message3));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).transmit_time_interval == LogInterval{time2, time2});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name4).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name4).byte_count == sizeof(message4));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name4).transmit_time_interval == LogInterval{time4, time4});

    REQUIRE(reader.open());
    REQUIRE(reader);

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == time1);
    REQUIRE(read_result->transmit_time == time1);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message1));
    REQUIRE(std::memcmp(read_result->data.data(), &message1, sizeof(message1)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name3);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message3));
    REQUIRE(std::memcmp(read_result->data.data(), &message3, sizeof(message3)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time3);
    REQUIRE(read_result->transmit_time == time3);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message2));
    REQUIRE(std::memcmp(read_result->data.data(), &message2, sizeof(message2)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name4);
    REQUIRE(read_result->sequence_number == 4U);
    REQUIRE(read_result->log_time == time4);
    REQUIRE(read_result->transmit_time == time4);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message4));
    REQUIRE(std::memcmp(read_result->data.data(), &message4, sizeof(message4)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Merge unions of amendments")
  {
    constexpr auto amendment_log1_name = "amendment_log1";
    constexpr auto amendment_log2_name = "amendment_log2";
    constexpr auto amendment_log3_name = "amendment_log3";

    const auto amendment_log1_path = test_dir.get_path() / amendment_log1_name;
    const std::string amendment_log1_path_str{amendment_log1_path.c_str()};
    const auto amendment_log2_path = test_dir.get_path() / amendment_log2_name;
    const std::string amendment_log2_path_str{amendment_log2_path.c_str()};
    const auto amendment_log3_path = test_dir.get_path() / amendment_log3_name;
    const std::string amendment_log3_path_str{amendment_log3_path.c_str()};
    AmendmentWriter amendment_writer1{memory_resource};
    AmendmentWriter amendment_writer2{memory_resource};
    AmendmentWriter amendment_writer3{memory_resource};

    const auto amendment_metadata1 = LoggedChannelMetadata{
      .channel_name = channel_name1,
      .message_encoding = MessageEncoding::tachyon,
      .channel_type = ChannelType::regular,
      .schema_name = clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage2>>::schema_name,
      .schema_encoding = SchemaEncoding::clockwork_tachyon,
      .schema_definition =
        std::string_view{
          clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage2>>::schema_definition.data(),
          clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage2>>::schema_definition.size()},
      .is_amended = true,
    };
    clockwork::Tappy<clockwork_logging::tests::TestMessage2> amendment_message1;
    onboard::tests::fill_with_random_bytes(amendment_message1.get_mutable_data());

    REQUIRE(ok(amendment_writer1.open(amendment_log1_path.string(), fmt::format("../{}", source_log1_name))));
    REQUIRE(
      ok(amendment_writer1.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage2>>(channel_name1)));

    REQUIRE(ok(amendment_writer1.write(channel_name1, 1U, time1, time1, amendment_message1)));

    REQUIRE(ok(amendment_writer1.close()));

    const auto amendment_metadata3 = LoggedChannelMetadata{
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
    clockwork::Tappy<clockwork_logging::tests::TestMessage3> amendment_message3;
    onboard::tests::fill_with_random_bytes(amendment_message3.get_mutable_data());

    REQUIRE(ok(amendment_writer2.open(amendment_log2_path.string(), fmt::format("../{}", source_log2_name))));
    REQUIRE(
      ok(amendment_writer2.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage3>>(channel_name3)));

    REQUIRE(ok(amendment_writer2.write(channel_name3, 3U, time2, time2, amendment_message3)));

    REQUIRE(ok(amendment_writer2.close()));

    const auto amendment_metadata5 = LoggedChannelMetadata{
      .channel_name = channel_name5,
      .message_encoding = MessageEncoding::tachyon,
      .channel_type = ChannelType::regular,
      .schema_name = clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage1>>::schema_name,
      .schema_encoding = SchemaEncoding::clockwork_tachyon,
      .schema_definition =
        std::string_view{
          clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage1>>::schema_definition.data(),
          clockwork::LoggingTraits<clockwork::Tappy<clockwork_logging::tests::TestMessage1>>::schema_definition.size()},
      .is_amended = true,
    };

    REQUIRE(ok(amendment_writer3.open(amendment_log3_path.string(), fmt::format("../{}", source_log3_name))));
    REQUIRE(
      ok(amendment_writer3.create_channel<clockwork::Tappy<clockwork_logging::tests::TestMessage1>>(channel_name5)));
    REQUIRE(ok(amendment_writer3.close()));

    constexpr auto union1_name = "union_log1";
    constexpr auto union2_name = "union_log2";
    constexpr auto union_merge_name = "union_merge_log";

    const auto union1_path = test_dir.get_path() / union1_name;
    const auto& union1_path_str = union1_path.string();
    const auto union2_path = test_dir.get_path() / union2_name;
    const auto& union2_path_str = union2_path.string();
    const auto union_merge_path = test_dir.get_path() / union_merge_name;

    std::vector<std::string_view> source_logs1{amendment_log1_path_str, amendment_log2_path_str};
    std::vector<std::string_view> source_logs2{amendment_log2_path_str, amendment_log3_path_str};

    REQUIRE(write_merge_union(memory_resource, source_logs1, union1_path.string()));
    REQUIRE(write_merge_union(memory_resource, source_logs2, union2_path.string()));

    const auto log_union1_path = union1_path / log_union_filename;
    const auto union1_result =
      chunk_reader_factory->read_text_proto<::clockwork::logging::offboard::v1::LogUnion>(log_union1_path.string());
    REQUIRE(union1_result);
    const auto& union1 = union1_result.value();
    REQUIRE(union1.log_union_entry_size() == 4);
    REQUIRE(union1.log_union_entry(0).has_absolute_path());
    REQUIRE(union1.log_union_entry(0).absolute_path() == amendment_log1_path_str);
    REQUIRE(union1.log_union_entry(0).excluded_channel().empty());
    REQUIRE(union1.log_union_entry(1).has_absolute_path());
    REQUIRE(union1.log_union_entry(1).absolute_path() == amendment_log2_path_str);
    REQUIRE(union1.log_union_entry(1).excluded_channel().empty());
    REQUIRE(union1.log_union_entry(2).has_absolute_path());
    REQUIRE(union1.log_union_entry(2).absolute_path() == source_log1_path_str);
    REQUIRE(union1.log_union_entry(2).excluded_channel_size() == 1);
    REQUIRE(union1.log_union_entry(2).excluded_channel(0) == "channel1");
    REQUIRE(union1.log_union_entry(3).has_absolute_path());
    REQUIRE(union1.log_union_entry(3).absolute_path() == source_log2_path_str);
    REQUIRE(union1.log_union_entry(3).excluded_channel_size() == 1);
    REQUIRE(union1.log_union_entry(3).excluded_channel(0) == "channel3");

    const auto log_union2_path = union2_path / log_union_filename;
    const auto union2_result =
      chunk_reader_factory->read_text_proto<::clockwork::logging::offboard::v1::LogUnion>(log_union2_path.string());
    REQUIRE(union2_result);
    const auto& union2 = union2_result.value();
    REQUIRE(union2.log_union_entry_size() == 4);
    REQUIRE(union2.log_union_entry(0).has_absolute_path());
    REQUIRE(union2.log_union_entry(0).absolute_path() == amendment_log2_path_str);
    REQUIRE(union2.log_union_entry(0).excluded_channel().empty());
    REQUIRE(union2.log_union_entry(1).has_absolute_path());
    REQUIRE(union2.log_union_entry(1).absolute_path() == amendment_log3_path_str);
    REQUIRE(union2.log_union_entry(1).excluded_channel().empty());
    REQUIRE(union2.log_union_entry(2).has_absolute_path());
    REQUIRE(union2.log_union_entry(2).absolute_path() == source_log2_path_str);
    REQUIRE(union2.log_union_entry(2).excluded_channel_size() == 1);
    REQUIRE(union2.log_union_entry(2).excluded_channel(0) == "channel3");
    REQUIRE(union2.log_union_entry(3).has_absolute_path());
    REQUIRE(union2.log_union_entry(3).absolute_path() == source_log3_path_str);
    REQUIRE(union2.log_union_entry(3).excluded_channel_size() == 1);
    REQUIRE(union2.log_union_entry(3).excluded_channel(0) == "channel5");

    std::vector<std::string_view> union_logs{union1_path_str, union2_path_str};
    REQUIRE(write_merge_union(memory_resource, union_logs, union_merge_path.string()));

    const auto union_merge_result = chunk_reader_factory->read_text_proto<::clockwork::logging::offboard::v1::LogUnion>(
      (union_merge_path / log_union_filename).string());
    REQUIRE(union_merge_result);
    const auto& union_merge = union_merge_result.value();
    REQUIRE(union_merge.log_union_entry_size() == 6);
    REQUIRE(union_merge.log_union_entry(0).has_absolute_path());
    REQUIRE(union_merge.log_union_entry(0).absolute_path() == amendment_log1_path_str);
    REQUIRE(union_merge.log_union_entry(0).excluded_channel().empty());
    REQUIRE(union_merge.log_union_entry(1).has_absolute_path());
    REQUIRE(union_merge.log_union_entry(1).absolute_path() == amendment_log2_path_str);
    REQUIRE(union_merge.log_union_entry(1).excluded_channel().empty());
    REQUIRE(union_merge.log_union_entry(2).has_absolute_path());
    REQUIRE(union_merge.log_union_entry(2).absolute_path() == amendment_log3_path_str);
    REQUIRE(union_merge.log_union_entry(2).excluded_channel().empty());
    REQUIRE(union_merge.log_union_entry(3).has_absolute_path());
    REQUIRE(union_merge.log_union_entry(3).absolute_path() == source_log1_path_str);
    REQUIRE(union_merge.log_union_entry(3).excluded_channel_size() == 1);
    REQUIRE(union_merge.log_union_entry(3).excluded_channel(0) == "channel1");
    REQUIRE(union_merge.log_union_entry(4).has_absolute_path());
    REQUIRE(union_merge.log_union_entry(4).absolute_path() == source_log2_path_str);
    REQUIRE(union_merge.log_union_entry(4).excluded_channel_size() == 1);
    REQUIRE(union_merge.log_union_entry(4).excluded_channel(0) == "channel3");
    REQUIRE(union_merge.log_union_entry(5).has_absolute_path());
    REQUIRE(union_merge.log_union_entry(5).absolute_path() == source_log3_path_str);
    REQUIRE(union_merge.log_union_entry(5).excluded_channel_size() == 1);
    REQUIRE(union_merge.log_union_entry(5).excluded_channel(0) == "channel5");

    Reader reader{memory_resource, union_merge_path.string(), chunk_reader_factory};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 5U);
    REQUIRE((*metadata_result)->at(channel_name1) == amendment_metadata1);
    REQUIRE((*metadata_result)->at(channel_name3) == amendment_metadata3);
    REQUIRE((*metadata_result)->at(channel_name4) == metadata4);
    REQUIRE((*metadata_result)->at(channel_name5) == amendment_metadata5);

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == 4U);
    REQUIRE(
      (*metrics_result)->byte_count ==
      sizeof(amendment_message1) + sizeof(message2) + sizeof(amendment_message3) + sizeof(message4));
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time1, time4});
    REQUIRE((*metrics_result)->metrics_map.size() == 4U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).byte_count == sizeof(amendment_message1));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval == LogInterval{time1, time1});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == sizeof(message2));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time3, time3});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).byte_count == sizeof(amendment_message3));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).transmit_time_interval == LogInterval{time2, time2});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name4).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name4).byte_count == sizeof(message4));
    REQUIRE((*metrics_result)->metrics_map.at(channel_name4).transmit_time_interval == LogInterval{time4, time4});

    REQUIRE(reader.open());
    REQUIRE(reader);

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == time1);
    REQUIRE(read_result->transmit_time == time1);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(amendment_message1));
    REQUIRE(std::memcmp(read_result->data.data(), &amendment_message1, sizeof(amendment_message1)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name3);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(amendment_message3));
    REQUIRE(std::memcmp(read_result->data.data(), &amendment_message3, sizeof(amendment_message3)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time3);
    REQUIRE(read_result->transmit_time == time3);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message2));
    REQUIRE(std::memcmp(read_result->data.data(), &message2, sizeof(message2)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name4);
    REQUIRE(read_result->sequence_number == 4U);
    REQUIRE(read_result->log_time == time4);
    REQUIRE(read_result->transmit_time == time4);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == sizeof(message4));
    REQUIRE(std::memcmp(read_result->data.data(), &message4, sizeof(message4)) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }
}

} // namespace
} // namespace clockwork_logging::offboard
