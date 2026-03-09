// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/merge_logs.hh"
#include "clockwork/logging/offboard/reader.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/v1/log_union.pb.h"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
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

TEST_CASE("merge_logs")
{
  constexpr auto source_log1_name = "source_log1";
  constexpr auto source_log2_name = "source_log2";
  constexpr auto source_log3_name = "source_log3";
  constexpr auto dest_log_name = "dest_log";

  const auto recover_metadata = GENERATE(false, true);
  CAPTURE(recover_metadata);

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
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
  constexpr auto metadata1 = LoggedChannelMetadata{
    .channel_name = channel_name1,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::regular,
    .schema_name = "schema1",
    .schema_encoding = SchemaEncoding::unspecified,
    .schema_definition = "Schema definition 1",
  };
  constexpr auto header1_size = 123U;
  std::vector<std::byte> header1(header1_size);
  onboard::tests::fill_with_random_bytes(header1);
  constexpr auto data1_size = 1234U;
  std::vector<std::byte> data1(data1_size);
  onboard::tests::fill_with_random_bytes(data1);

  constexpr auto channel_name2 = "channel2";
  constexpr auto metadata2 = LoggedChannelMetadata{
    .channel_name = channel_name2,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::persistent,
    .schema_name = "schema2",
    .schema_encoding = SchemaEncoding::unspecified,
    .schema_definition = "Schema definition 2",
  };
  constexpr auto header2_size = 234U;
  std::vector<std::byte> header2(header2_size);
  onboard::tests::fill_with_random_bytes(header2);
  constexpr auto data2_size = 2345U;
  std::vector<std::byte> data2(data2_size);
  onboard::tests::fill_with_random_bytes(data2);

  REQUIRE(writer1.open(source_log1_path_str));
  REQUIRE(writer1.create_channel(metadata1));
  REQUIRE(writer1.create_channel(metadata2));

  REQUIRE(writer1.write(
    LoggedMessage{
      .channel_name = channel_name1,
      .sequence_number = 1U,
      .log_time = time1,
      .transmit_time = time1,
      .header = header1,
      .data = data1,
      .is_repeated_persistent = false,
    }));

  REQUIRE(writer1.write(
    LoggedMessage{
      .channel_name = channel_name2,
      .sequence_number = 2U,
      .log_time = time3,
      .transmit_time = time3,
      .header = header2,
      .data = data2,
      .is_repeated_persistent = false,
    }));

  REQUIRE(writer1.close());

  constexpr auto channel_name3 = "channel3";
  constexpr auto metadata3 = LoggedChannelMetadata{
    .channel_name = channel_name3,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::regular,
    .schema_name = "schema3",
    .schema_encoding = SchemaEncoding::unspecified,
    .schema_definition = "Schema definition 3",
  };
  constexpr auto header3_size = 124U;
  std::vector<std::byte> header3(header3_size);
  onboard::tests::fill_with_random_bytes(header3);
  constexpr auto data3_size = 1235U;
  std::vector<std::byte> data3(data3_size);
  onboard::tests::fill_with_random_bytes(data3);

  constexpr auto channel_name4 = "channel4";
  constexpr auto metadata4 = LoggedChannelMetadata{
    .channel_name = channel_name4,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::persistent,
    .schema_name = "schema4",
    .schema_encoding = SchemaEncoding::unspecified,
    .schema_definition = "schema definition 4",
  };
  constexpr auto header4_size = 235U;
  std::vector<std::byte> header4(header4_size);
  onboard::tests::fill_with_random_bytes(header4);
  constexpr auto data4_size = 2346U;
  std::vector<std::byte> data4(data4_size);
  onboard::tests::fill_with_random_bytes(data4);

  REQUIRE(writer2.open(source_log2_path_str));
  REQUIRE(writer2.create_channel(metadata3));
  REQUIRE(writer2.create_channel(metadata4));

  REQUIRE(writer2.write(
    LoggedMessage{
      .channel_name = channel_name3,
      .sequence_number = 3U,
      .log_time = time2,
      .transmit_time = time2,
      .header = header3,
      .data = data3,
      .is_repeated_persistent = false,
    }));

  REQUIRE(writer2.write(
    LoggedMessage{
      .channel_name = channel_name4,
      .sequence_number = 4U,
      .log_time = time4,
      .transmit_time = time4,
      .header = header4,
      .data = data4,
      .is_repeated_persistent = false,
    }));

  REQUIRE(writer2.close());

  constexpr auto channel_name5 = "channel5";
  constexpr auto metadata5 = LoggedChannelMetadata{
    .channel_name = channel_name5,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::persistent,
    .schema_name = "schema5",
    .schema_encoding = SchemaEncoding::unspecified,
    .schema_definition = "Schema definition 5",
  };

  REQUIRE(writer3.open(source_log3_path_str));
  REQUIRE(writer3.create_channel(metadata5));
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

    Reader reader{memory_resource, dest_log_path};

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
    REQUIRE(
      (*metrics_result)->byte_count == data1.size() + data2.size() + header1.size() + header2.size() + data3.size() +
                                         header3.size() + data4.size() + header4.size());
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time1, time4});
    REQUIRE((*metrics_result)->metrics_map.size() == 4U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).byte_count == data1.size() + header1.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval == LogInterval{time1, time1});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == data2.size() + header2.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time3, time3});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).byte_count == data3.size() + header3.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).transmit_time_interval == LogInterval{time2, time2});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name4).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name4).byte_count == data4.size() + header4.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name4).transmit_time_interval == LogInterval{time4, time4});

    REQUIRE(reader.open());
    REQUIRE(reader);

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == time1);
    REQUIRE(read_result->transmit_time == time1);
    REQUIRE(read_result->header.size() == header1.size());
    REQUIRE(std::memcmp(read_result->header.data(), header1.data(), header1.size()) == 0);
    REQUIRE(read_result->data.size() == data1.size());
    REQUIRE(std::memcmp(read_result->data.data(), data1.data(), data1.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name3);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.size() == header3.size());
    REQUIRE(std::memcmp(read_result->header.data(), header3.data(), header3.size()) == 0);
    REQUIRE(read_result->data.size() == data3.size());
    REQUIRE(std::memcmp(read_result->data.data(), data3.data(), data3.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time3);
    REQUIRE(read_result->transmit_time == time3);
    REQUIRE(read_result->header.size() == header2.size());
    REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
    REQUIRE(read_result->data.size() == data2.size());
    REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name4);
    REQUIRE(read_result->sequence_number == 4U);
    REQUIRE(read_result->log_time == time4);
    REQUIRE(read_result->transmit_time == time4);
    REQUIRE(read_result->header.size() == header4.size());
    REQUIRE(std::memcmp(read_result->header.data(), header4.data(), header4.size()) == 0);
    REQUIRE(read_result->data.size() == data4.size());
    REQUIRE(std::memcmp(read_result->data.data(), data4.data(), data4.size()) == 0);
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

    Reader reader{memory_resource, dest_log_path};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 2U);
    REQUIRE((*metadata_result)->at(channel_name2) == metadata2);
    REQUIRE((*metadata_result)->at(channel_name3) == metadata3);

    REQUIRE(reader.get_log_interval() == LogInterval{time2, time3});

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == 2U);
    REQUIRE((*metrics_result)->byte_count == data2.size() + header2.size() + data3.size() + header3.size());
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time2, time3});
    REQUIRE((*metrics_result)->metrics_map.size() == 2U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == data2.size() + header2.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time3, time3});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).byte_count == data3.size() + header3.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).transmit_time_interval == LogInterval{time2, time2});

    REQUIRE(reader.open());
    REQUIRE(reader);

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name3);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.size() == header3.size());
    REQUIRE(std::memcmp(read_result->header.data(), header3.data(), header3.size()) == 0);
    REQUIRE(read_result->data.size() == data3.size());
    REQUIRE(std::memcmp(read_result->data.data(), data3.data(), data3.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time3);
    REQUIRE(read_result->transmit_time == time3);
    REQUIRE(read_result->header.size() == header2.size());
    REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
    REQUIRE(read_result->data.size() == data2.size());
    REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
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
    REQUIRE(write_merge_union(memory_resource, source_logs, union1_path.string()));
    REQUIRE(write_merge_union(memory_resource, source_logs, union2_path.string()));

    ChunkReaderWriterFactory chunk_reader_factory{memory_resource};

    const auto log_union1_path = union1_path / log_union_filename;
    const auto union1_result =
      chunk_reader_factory.read_text_proto<::clockwork::logging::offboard::v1::LogUnion>(log_union1_path.string());
    REQUIRE(union1_result);
    const auto& union1 = union1_result.value();
    REQUIRE(union1.log_union_entry_size() == 3);
    REQUIRE(union1.log_union_entry(0).has_absolute_path());
    REQUIRE(union1.log_union_entry(0).absolute_path() == source_log1_path_str);
    REQUIRE(union1.log_union_entry(1).has_absolute_path());
    REQUIRE(union1.log_union_entry(1).absolute_path() == source_log2_path_str);
    REQUIRE(union1.log_union_entry(2).has_absolute_path());
    REQUIRE(union1.log_union_entry(2).absolute_path() == source_log3_path_str);

    const auto log_union2_path = union2_path / log_union_filename;
    const auto union2_result =
      chunk_reader_factory.read_text_proto<::clockwork::logging::offboard::v1::LogUnion>(log_union2_path.string());
    REQUIRE(union2_result);
    const auto& union2 = union2_result.value();
    REQUIRE(union2.log_union_entry_size() == 3);
    REQUIRE(union2.log_union_entry(0).has_absolute_path());
    REQUIRE(union2.log_union_entry(0).absolute_path() == source_log1_path_str);
    REQUIRE(union2.log_union_entry(1).has_absolute_path());
    REQUIRE(union2.log_union_entry(1).absolute_path() == source_log2_path_str);
    REQUIRE(union2.log_union_entry(2).has_absolute_path());
    REQUIRE(union2.log_union_entry(2).absolute_path() == source_log3_path_str);

    std::vector<std::string_view> union_logs{union1_path_str, union2_path_str};
    REQUIRE(write_merge_union(memory_resource, union_logs, union_merge_path.string()));

    const auto union_merge_result = chunk_reader_factory.read_text_proto<::clockwork::logging::offboard::v1::LogUnion>(
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

    Reader reader{memory_resource, union_merge_path.string()};

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
    REQUIRE(
      (*metrics_result)->byte_count == data1.size() + data2.size() + header1.size() + header2.size() + data3.size() +
                                         header3.size() + data4.size() + header4.size());
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time1, time4});
    REQUIRE((*metrics_result)->metrics_map.size() == 4U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).byte_count == data1.size() + header1.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval == LogInterval{time1, time1});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == data2.size() + header2.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time3, time3});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).byte_count == data3.size() + header3.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name3).transmit_time_interval == LogInterval{time2, time2});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name4).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name4).byte_count == data4.size() + header4.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name4).transmit_time_interval == LogInterval{time4, time4});

    REQUIRE(reader.open());
    REQUIRE(reader);

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == time1);
    REQUIRE(read_result->transmit_time == time1);
    REQUIRE(read_result->header.size() == header1.size());
    REQUIRE(std::memcmp(read_result->header.data(), header1.data(), header1.size()) == 0);
    REQUIRE(read_result->data.size() == data1.size());
    REQUIRE(std::memcmp(read_result->data.data(), data1.data(), data1.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name3);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.size() == header3.size());
    REQUIRE(std::memcmp(read_result->header.data(), header3.data(), header3.size()) == 0);
    REQUIRE(read_result->data.size() == data3.size());
    REQUIRE(std::memcmp(read_result->data.data(), data3.data(), data3.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time3);
    REQUIRE(read_result->transmit_time == time3);
    REQUIRE(read_result->header.size() == header2.size());
    REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
    REQUIRE(read_result->data.size() == data2.size());
    REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name4);
    REQUIRE(read_result->sequence_number == 4U);
    REQUIRE(read_result->log_time == time4);
    REQUIRE(read_result->transmit_time == time4);
    REQUIRE(read_result->header.size() == header4.size());
    REQUIRE(std::memcmp(read_result->header.data(), header4.data(), header4.size()) == 0);
    REQUIRE(read_result->data.size() == data4.size());
    REQUIRE(std::memcmp(read_result->data.data(), data4.data(), data4.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }
}

} // namespace
} // namespace clockwork_logging::offboard
