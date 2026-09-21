// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/copy_log.hh"
#include "clockwork/logging/offboard/reader.hh"
#include "clockwork/logging/offboard/tests/support/test_support.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/v1/log_metadata.pb.h"
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

#include <catch2/catch_test_macros.hpp>
#include <google/protobuf/repeated_ptr_field.h>

#include <chrono>
#include <cstddef>
#include <cstring>
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

TEST_CASE("copy_log")
{
  constexpr auto source_log_name = "source_log";
  constexpr auto dest_log_name = "dest_log";

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  LiteCompressor lite_compressor{memory_resource};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto source_log_path = test_dir.get_path() / source_log_name;
  const auto dest_log_path = test_dir.get_path() / dest_log_name;
  Writer writer{memory_resource};

  constexpr LogTimestamp time1{std::chrono::seconds(1)};
  constexpr LogTimestamp time2{std::chrono::seconds(2)};

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
  const auto compressed_data2 = offboard::tests::lite_compress(data2, lite_compressor);

  REQUIRE(writer.open(source_log_path.string()));
  REQUIRE(writer.create_channel(metadata1));
  REQUIRE(writer.create_channel(metadata2));

  REQUIRE(writer.write(
    LoggedMessage{
      .channel_name = channel_name1,
      .sequence_number = 1U,
      .log_time = time1,
      .transmit_time = time1,
      .header = header1,
      .data = data1,
      .is_repeated_persistent = false,
      .is_lite_compressed = false,
    }));

  REQUIRE(writer.write(
    LoggedMessage{
      .channel_name = channel_name2,
      .sequence_number = 2U,
      .log_time = time2,
      .transmit_time = time2,
      .header = header2,
      .data = compressed_data2,
      .is_repeated_persistent = false,
      .is_lite_compressed = true,
    }));

  REQUIRE(writer.close());

  SECTION("Copy entire log")
  {
    REQUIRE(copy_log(/*memory_resource=*/memory_resource,
                     /*source_uri=*/source_log_path.string(),
                     /*dest_uri=*/dest_log_path.string()));

    const auto chunk_reader_writer_factory = std::make_shared<ChunkReaderWriterFactory<>>(memory_resource);
    Reader reader{memory_resource, dest_log_path.string(), chunk_reader_writer_factory};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 2U);
    REQUIRE((*metadata_result)->at(channel_name1) == metadata1);
    REQUIRE((*metadata_result)->at(channel_name2) == metadata2);

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == 2U);
    REQUIRE((*metrics_result)->byte_count == data1.size() + compressed_data2.size() + header1.size() + header2.size());
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time1, time2});
    REQUIRE((*metrics_result)->metrics_map.size() == 2U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).byte_count == data1.size() + header1.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval == LogInterval{time1, time1});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == compressed_data2.size() + header2.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time2, time2});

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
    REQUIRE_FALSE(read_result->is_lite_compressed);
    REQUIRE(read_result->data.size() == data1.size());
    REQUIRE(std::memcmp(read_result->data.data(), data1.data(), data1.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.size() == header2.size());
    REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
    REQUIRE_FALSE(read_result->is_lite_compressed);
    REQUIRE(read_result->data.size() == data2.size());
    REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
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
        .end_offset = time2 - time1,
      };
      REQUIRE(copy_log(/*memory_resource=*/memory_resource,
                       /*source_uri=*/source_log_path.string(),
                       /*dest_uri=*/dest_log_path.string(),
                       /*maybe_desired_channels=*/{},
                       /*maybe_excluded_channels=*/{},
                       /*maybe_log_interval=*/log_interval));
    }

    SECTION("Filter by desired channels")
    {
      std::pmr::unordered_set<std::pmr::string> desired_channels{channel_name2};
      REQUIRE(copy_log(/*memory_resource=*/memory_resource,
                       /*source_uri=*/source_log_path.string(),
                       /*dest_uri=*/dest_log_path.string(),
                       /*maybe_desired_channels=*/desired_channels));
    }

    SECTION("Filter by excluded channels")
    {
      std::pmr::unordered_set<std::pmr::string> excluded_channels{channel_name1};
      REQUIRE(copy_log(/*memory_resource=*/memory_resource,
                       /*source_uri=*/source_log_path.string(),
                       /*dest_uri=*/dest_log_path.string(),
                       /*maybe_desired_channels=*/{},
                       /*maybe_excluded_channels=*/excluded_channels));
    }

    SECTION("Filter by desired and excluded channels")
    {
      std::pmr::unordered_set<std::pmr::string> desired_channels{channel_name1, channel_name2};
      std::pmr::unordered_set<std::pmr::string> excluded_channels{channel_name1};
      REQUIRE(copy_log(
        /*memory_resource=*/memory_resource,
        /*source_uri=*/source_log_path.string(),
        /*dest_uri=*/dest_log_path.string(),
        /*maybe_desired_channels=*/desired_channels,
        /*maybe_excluded_channels=*/excluded_channels));
    }

    const auto chunk_reader_writer_factory = std::make_shared<ChunkReaderWriterFactory<>>(memory_resource);
    Reader reader{memory_resource, dest_log_path.string(), chunk_reader_writer_factory};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 1U);
    REQUIRE((*metadata_result)->at(channel_name2) == metadata2);

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == 1U);
    REQUIRE((*metrics_result)->byte_count == compressed_data2.size() + header2.size());
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time2, time2});
    REQUIRE((*metrics_result)->metrics_map.size() == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == compressed_data2.size() + header2.size());
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time2, time2});

    REQUIRE(reader.open());
    REQUIRE(reader);

    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == time2);
    REQUIRE(read_result->transmit_time == time2);
    REQUIRE(read_result->header.size() == header2.size());
    REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
    REQUIRE_FALSE(read_result->is_lite_compressed);
    REQUIRE(read_result->data.size() == data2.size());
    REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }
}

TEST_CASE("copy_log_union")
{
  constexpr auto source_log_union = "source_log_union";
  constexpr auto source_log_name1 = "source_log1";
  constexpr auto source_log_name2 = "source_log2";
  constexpr auto source_log_name3 = "source_log3";
  constexpr auto dest_log_name = "dest_log";

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  LiteCompressor lite_compressor{memory_resource};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto source_log_union_path = test_dir.get_path() / source_log_union;
  const auto source_log_path1 = test_dir.get_path() / source_log_name1;
  const auto source_log_path2 = test_dir.get_path() / source_log_name2;
  const auto source_log_path3 = test_dir.get_path() / source_log_name3;
  const auto dest_log_path = test_dir.get_path() / dest_log_name;
  Writer writer1{memory_resource};
  Writer writer2{memory_resource};
  Writer writer3{memory_resource};

  constexpr LogTimestamp time1{std::chrono::seconds(1)};
  constexpr LogTimestamp time2{std::chrono::seconds(2)};

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
  const auto compressed_data2 = offboard::tests::lite_compress(data2, lite_compressor);

  REQUIRE(writer1.open(source_log_path1.string()));
  REQUIRE(writer1.create_channel(metadata1));
  REQUIRE(writer2.open(source_log_path2.string()));
  REQUIRE(writer2.create_channel(metadata2));
  REQUIRE(writer3.open(source_log_path3.string()));

  REQUIRE(writer1.write(
    LoggedMessage{
      .channel_name = channel_name1,
      .sequence_number = 1U,
      .log_time = time1,
      .transmit_time = time1,
      .header = header1,
      .data = data1,
      .is_repeated_persistent = false,
      .is_lite_compressed = false,
    }));

  REQUIRE(writer2.write(
    LoggedMessage{
      .channel_name = channel_name2,
      .sequence_number = 2U,
      .log_time = time2,
      .transmit_time = time2,
      .header = header2,
      .data = compressed_data2,
      .is_repeated_persistent = false,
      .is_lite_compressed = true,
    }));

  REQUIRE(writer1.close());
  REQUIRE(writer2.close());
  REQUIRE(writer3.close());

  ::clockwork::logging::offboard::v1::LogUnion source_log_union_protobuf;
  source_log_union_protobuf.mutable_log_union_entry()->Add()->set_absolute_path(source_log_path1.string());
  source_log_union_protobuf.mutable_log_union_entry()->Add()->set_absolute_path(source_log_path2.string());
  source_log_union_protobuf.mutable_log_union_entry()->Add()->set_absolute_path(source_log_path3.string());

  const auto source_log_union_file_uri = source_log_union_path / "stack_log_union.pbtxt";

  const auto chunk_reader_writer_factory = std::make_shared<ChunkReaderWriterFactory<>>(memory_resource);
  REQUIRE(chunk_reader_writer_factory->create_directories(source_log_union_path.string()));
  REQUIRE(chunk_reader_writer_factory->write_text_proto<::clockwork::logging::offboard::v1::LogUnion>(
    source_log_union_file_uri.string(), "", source_log_union_protobuf));

  SECTION("Deep copy")
  {
    SECTION("Copy entire log")
    {
      REQUIRE(copy_log(/*memory_resource=*/memory_resource,
                       /*source_uri=*/source_log_union_path.string(),
                       /*dest_uri=*/dest_log_path.string()));

      Reader reader{memory_resource, dest_log_path.string(), chunk_reader_writer_factory};

      const auto metadata_result = reader.get_metadata();
      REQUIRE(metadata_result);
      REQUIRE((*metadata_result)->size() == 2U);
      REQUIRE((*metadata_result)->at(channel_name1) == metadata1);
      REQUIRE((*metadata_result)->at(channel_name2) == metadata2);

      const auto metrics_result = reader.get_metrics();
      REQUIRE(metrics_result);
      REQUIRE((*metrics_result)->message_count == 2U);
      REQUIRE(
        (*metrics_result)->byte_count == data1.size() + compressed_data2.size() + header1.size() + header2.size());
      REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time1, time2});
      REQUIRE((*metrics_result)->metrics_map.size() == 2U);
      REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == 1U);
      REQUIRE((*metrics_result)->metrics_map.at(channel_name1).byte_count == data1.size() + header1.size());
      REQUIRE((*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval == LogInterval{time1, time1});
      REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
      REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == compressed_data2.size() + header2.size());
      REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time2, time2});

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
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(read_result->data.size() == data1.size());
      REQUIRE(std::memcmp(read_result->data.data(), data1.data(), data1.size()) == 0);
      REQUIRE_FALSE(read_result->is_repeated_persistent);

      read_result = reader.read_next();
      REQUIRE(read_result);
      REQUIRE(read_result->channel_name == channel_name2);
      REQUIRE(read_result->sequence_number == 2U);
      REQUIRE(read_result->log_time == time2);
      REQUIRE(read_result->transmit_time == time2);
      REQUIRE(read_result->header.size() == header2.size());
      REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(read_result->data.size() == data2.size());
      REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
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
          .end_offset = time2 - time1,
        };
        REQUIRE(copy_log(/*memory_resource=*/memory_resource,
                         /*source_uri=*/source_log_union_path.string(),
                         /*dest_uri=*/dest_log_path.string(),
                         /*maybe_desired_channels=*/{},
                         /*maybe_excluded_channels=*/{},
                         /*maybe_log_interval=*/log_interval));
      }

      SECTION("Filter by desired channels")
      {
        std::pmr::unordered_set<std::pmr::string> desired_channels{channel_name2};
        REQUIRE(copy_log(/*memory_resource=*/memory_resource,
                         /*source_uri=*/source_log_union_path.string(),
                         /*dest_uri=*/dest_log_path.string(),
                         /*maybe_desired_channels=*/desired_channels));
      }

      SECTION("Filter by excluded channels")
      {
        std::pmr::unordered_set<std::pmr::string> excluded_channels{channel_name1};
        REQUIRE(copy_log(/*memory_resource=*/memory_resource,
                         /*source_uri=*/source_log_union_path.string(),
                         /*dest_uri=*/dest_log_path.string(),
                         /*maybe_desired_channels=*/{},
                         /*maybe_excluded_channels=*/excluded_channels));
      }

      SECTION("Filter by desired and excluded channels")
      {
        std::pmr::unordered_set<std::pmr::string> desired_channels{channel_name1, channel_name2};
        std::pmr::unordered_set<std::pmr::string> excluded_channels{channel_name1};
        REQUIRE(copy_log(
          /*memory_resource=*/memory_resource,
          /*source_uri=*/source_log_union_path.string(),
          /*dest_uri=*/dest_log_path.string(),
          /*maybe_desired_channels=*/desired_channels,
          /*maybe_excluded_channels=*/excluded_channels));
      }

      Reader reader{memory_resource, dest_log_path.string(), chunk_reader_writer_factory};

      const auto metadata_result = reader.get_metadata();
      REQUIRE(metadata_result);
      REQUIRE((*metadata_result)->size() == 1U);
      REQUIRE((*metadata_result)->at(channel_name2) == metadata2);

      const auto metrics_result = reader.get_metrics();
      REQUIRE(metrics_result);
      REQUIRE((*metrics_result)->message_count == 1U);
      REQUIRE((*metrics_result)->byte_count == compressed_data2.size() + header2.size());
      REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{time2, time2});
      REQUIRE((*metrics_result)->metrics_map.size() == 1U);
      REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == 1U);
      REQUIRE((*metrics_result)->metrics_map.at(channel_name2).byte_count == compressed_data2.size() + header2.size());
      REQUIRE((*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval == LogInterval{time2, time2});

      REQUIRE(reader.open());
      REQUIRE(reader);

      const auto read_result = reader.read_next();
      REQUIRE(read_result);
      REQUIRE(read_result->channel_name == channel_name2);
      REQUIRE(read_result->sequence_number == 2U);
      REQUIRE(read_result->log_time == time2);
      REQUIRE(read_result->transmit_time == time2);
      REQUIRE(read_result->header.size() == header2.size());
      REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(read_result->data.size() == data2.size());
      REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);

      REQUIRE_FALSE(reader);
      REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
    }
  }

  SECTION("Shallow copy")
  {
    REQUIRE(copy_log(/*memory_resource=*/memory_resource,
                     /*source_uri=*/source_log_union_path.string(),
                     /*dest_uri=*/dest_log_path.string(),
                     /*maybe_desired_channels=*/{},
                     /*maybe_excluded_channels=*/{},
                     /*maybe_log_interval=*/{},
                     /*writer_config_str=*/{},
                     /*no_deep_copy=*/true));

    const auto dest_log_union_file_uri = dest_log_path / "stack_log_union.pbtxt";

    const auto read_result = chunk_reader_writer_factory->read_text_proto<::clockwork::logging::offboard::v1::LogUnion>(
      dest_log_union_file_uri.string());
    REQUIRE(read_result);
    REQUIRE(read_result->log_union_entry(0).absolute_path() == source_log_path1.string_view());
    REQUIRE(read_result->log_union_entry(1).absolute_path() == source_log_path2.string_view());
  }
}

} // namespace
} // namespace clockwork_logging::offboard
