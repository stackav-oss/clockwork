// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_metadata_file_helper.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/v1/log_metadata.pb.h"
#include "clockwork/logging/offboard/v1/log_union.pb.h"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>
#include <google/protobuf/repeated_ptr_field.h>
#include <google/protobuf/text_format.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace clockwork_logging::offboard
{
namespace
{

using jewels::ok;
using jewels::Out;

TEST_CASE("LogMetadataFileHelper")
{
  constexpr auto log1_metadata_proto_text = R"(
    # proto-file: clockwork/logging/offboard/v1/log_metadata.proto
    # proto-message: LogMetadata
    log_writer_metadata {
      channel: "channel1"
      channel: "channel2"
      channel: "excluded_channel"
      log_file_metadata {
        log_file_name: "channel12_0.slog"
        min_transmit_time_ns: 1000000000
        max_transmit_time_ns: 2000000000
      }
      log_file_metadata {
        log_file_name: "channel12_1.slog"
        min_transmit_time_ns: 5000000000
        max_transmit_time_ns: 6000000000
      }
    }
    log_writer_metadata {
      channel: "channel3"
      channel: "channel4"
      channel: "excluded_channel"
      log_file_metadata {
        log_file_name: "channel34_0.slog"
        min_transmit_time_ns: 3000000000
        max_transmit_time_ns: 4000000000
      }
      log_file_metadata {
        log_file_name: "channel34_1.slog"
        min_transmit_time_ns: 7000000000
        max_transmit_time_ns: 7999999999
      }
    }
    min_transmit_time_ns: 1000000000
    max_transmit_time_ns: 7999999999
  )";
  ::clockwork::logging::offboard::v1::LogMetadata log1_metadata_protobuf;
  REQUIRE(google::protobuf::TextFormat::ParseFromString(log1_metadata_proto_text, &log1_metadata_protobuf));

  constexpr auto log2_metadata_proto_text = R"(
    # proto-file: clockwork/logging/offboard/v1/log_metadata.proto
    # proto-message: LogMetadata
    log_writer_metadata {
      channel: "channel1"
      channel: "channel5"
      channel: "excluded_channel"
      log_file_metadata {
        log_file_name: "channel15_0.slog"
        min_transmit_time_ns: 1000000001
        max_transmit_time_ns: 2000000000
      }
      log_file_metadata {
        log_file_name: "channel15_1.slog"
        min_transmit_time_ns: 5000000000
        max_transmit_time_ns: 6000000000
      }
    }
    log_writer_metadata {
      channel: "channel3"
      channel: "channel6"
      channel: "excluded_channel"
      log_file_metadata {
        log_file_name: "channel36_0.slog"
        min_transmit_time_ns: 3000000000
        max_transmit_time_ns: 4000000000
      }
      log_file_metadata {
        log_file_name: "channel36_1.slog"
        min_transmit_time_ns: 7000000000
        max_transmit_time_ns: 8000000000
      }
    }
    min_transmit_time_ns: 1000000001
    max_transmit_time_ns: 8000000000
  )";
  ::clockwork::logging::offboard::v1::LogMetadata log2_metadata_protobuf;
  REQUIRE(google::protobuf::TextFormat::ParseFromString(log2_metadata_proto_text, &log2_metadata_protobuf));

  constexpr auto log3_metadata_proto_text = R"(
    # proto-file: clockwork/logging/offboard/v1/log_metadata.proto
    # proto-message: LogMetadata
    min_transmit_time_ns: 0
    max_transmit_time_ns: 0
  )";
  ::clockwork::logging::offboard::v1::LogMetadata log3_metadata_protobuf;
  REQUIRE(google::protobuf::TextFormat::ParseFromString(log3_metadata_proto_text, &log3_metadata_protobuf));

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto log1_path = test_dir.get_path() / "log1";
  const auto log2_path = test_dir.get_path() / "log2";
  const auto log3_path = test_dir.get_path() / "log3";
  const auto union_path = test_dir.get_path() / "union";

  std::filesystem::create_directory(log1_path.c_str());
  std::filesystem::create_directory(log2_path.c_str());
  std::filesystem::create_directory(log3_path.c_str());
  std::filesystem::create_directory(union_path.c_str());

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  ChunkReaderWriterFactory chunk_reader_writer_factory{memory_resource};
  const auto log1_make_result = LogUri::try_make(std::string{"file:"}.append(log1_path.string()), memory_resource);
  REQUIRE(log1_make_result);
  const auto& log1_uri = log1_make_result.value();
  const auto log2_make_result = LogUri::try_make(std::string{"file:"}.append(log2_path.string()), memory_resource);
  REQUIRE(log2_make_result);
  const auto& log2_uri = log2_make_result.value();
  const auto log3_make_result = LogUri::try_make(std::string{"file:"}.append(log3_path.string()), memory_resource);
  REQUIRE(log3_make_result);
  const auto& log3_uri = log3_make_result.value();
  const auto union_make_result = LogUri::try_make(std::string{"file:"}.append(union_path.string()), memory_resource);
  REQUIRE(union_make_result);
  const auto& union_uri = union_make_result.value();

  constexpr LogTimestamp time0(std::chrono::seconds(0));
  constexpr LogTimestamp time1(std::chrono::seconds(1));
  constexpr LogTimestamp time3(std::chrono::seconds(3));
  constexpr LogTimestamp time6(std::chrono::seconds(6));
  constexpr LogTimestamp time8(std::chrono::seconds(8));

  const auto log1_metadata_file_uri = log1_uri / "stack_log_metadata.pbtxt";
  const auto log1_file1_uri = (log1_uri / "channel12_0.slog").string();
  const auto log1_file2_uri = (log1_uri / "channel12_1.slog").string();
  const auto log1_file3_uri = (log1_uri / "channel34_0.slog").string();
  const auto log1_file4_uri = (log1_uri / "channel34_1.slog").string();

  REQUIRE(chunk_reader_writer_factory.write_text_proto<::clockwork::logging::offboard::v1::LogMetadata>(
    log1_metadata_file_uri.string(), "", log1_metadata_protobuf));
  REQUIRE(chunk_reader_writer_factory.write_log_file(log1_file1_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(log1_file2_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(log1_file3_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(log1_file4_uri, {}));

  const auto log2_metadata_file_uri = log2_uri / "stack_log_metadata.pbtxt";
  const auto log2_file1_uri = (log2_uri / "channel15_0.slog").string();
  const auto log2_file2_uri = (log2_uri / "channel15_1.slog").string();
  const auto log2_file3_uri = (log2_uri / "channel36_0.slog").string();
  const auto log2_file4_uri = (log2_uri / "channel36_1.slog").string();

  REQUIRE(chunk_reader_writer_factory.write_text_proto<::clockwork::logging::offboard::v1::LogMetadata>(
    log2_metadata_file_uri.string(), "", log2_metadata_protobuf));
  REQUIRE(chunk_reader_writer_factory.write_log_file(log2_file1_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(log2_file2_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(log2_file3_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(log2_file4_uri, {}));

  const auto log3_metadata_file_uri = log3_uri / "stack_log_metadata.pbtxt";

  REQUIRE(chunk_reader_writer_factory.write_text_proto<::clockwork::logging::offboard::v1::LogMetadata>(
    log3_metadata_file_uri.string(), "", log3_metadata_protobuf));

  ::clockwork::logging::offboard::v1::LogUnion log_union_protobuf;
  log_union_protobuf.mutable_log_union_entry()->Add()->set_absolute_path(log1_uri.string());
  log_union_protobuf.mutable_log_union_entry()->Add()->set_relative_path("../log2");
  log_union_protobuf.mutable_log_union_entry()->Add()->set_absolute_path(log3_uri.string());

  const auto log_union_file_uri = union_uri / "stack_log_union.pbtxt";

  REQUIRE(chunk_reader_writer_factory.write_text_proto<::clockwork::logging::offboard::v1::LogUnion>(
    log_union_file_uri.string(), "", log_union_protobuf));

  LogUnionFileHelper helper(memory_resource);
  REQUIRE(ok(helper.initialize(log_union_file_uri, {"excluded_channel"}, chunk_reader_writer_factory)));
  REQUIRE(helper.get_transmit_time_interval() == LogInterval{time1, time8});

  REQUIRE(
    helper.get_channels() ==
    std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2", "channel3", "channel4", "channel5", "channel6"});

  SECTION("No desired channels or time range, unknown files are filtered out")
  {
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, {}, {})));
    REQUIRE(log_file_map.size() == 8U);
    REQUIRE(log_file_map.contains(log1_file1_uri));
    REQUIRE(log_file_map.at(log1_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(log1_file2_uri));
    REQUIRE(log_file_map.at(log1_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(log1_file3_uri));
    REQUIRE(log_file_map.at(log1_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3", "channel4"});
    REQUIRE(log_file_map.contains(log1_file4_uri));
    REQUIRE(log_file_map.at(log1_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3", "channel4"});
    REQUIRE(log_file_map.contains(log2_file1_uri));
    REQUIRE(log_file_map.at(log2_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel5"});
    REQUIRE(log_file_map.contains(log2_file2_uri));
    REQUIRE(log_file_map.at(log2_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel5"});
    REQUIRE(log_file_map.contains(log2_file3_uri));
    REQUIRE(log_file_map.at(log2_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3", "channel6"});
    REQUIRE(log_file_map.contains(log2_file4_uri));
    REQUIRE(log_file_map.at(log2_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3", "channel6"});
  }

  SECTION("Desired channels include all files")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel1", "channel3"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, {}, {})));
    REQUIRE(log_file_map.size() == 8U);
    REQUIRE(log_file_map.contains(log1_file1_uri));
    REQUIRE(log_file_map.at(log1_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(log1_file2_uri));
    REQUIRE(log_file_map.at(log1_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(log1_file3_uri));
    REQUIRE(log_file_map.at(log1_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3"});
    REQUIRE(log_file_map.contains(log1_file4_uri));
    REQUIRE(log_file_map.at(log1_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3"});
    REQUIRE(log_file_map.contains(log2_file1_uri));
    REQUIRE(log_file_map.at(log2_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(log2_file2_uri));
    REQUIRE(log_file_map.at(log2_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(log2_file3_uri));
    REQUIRE(log_file_map.at(log2_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3"});
    REQUIRE(log_file_map.contains(log2_file4_uri));
    REQUIRE(log_file_map.at(log2_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3"});
  }

  SECTION("Excluded channels include all files")
  {
    const std::pmr::unordered_set<std::pmr::string> excluded_channels{"channel2", "channel4", "channel5", "channel6"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, {}, excluded_channels)));
    REQUIRE(log_file_map.size() == 8U);
    REQUIRE(log_file_map.contains(log1_file1_uri));
    REQUIRE(log_file_map.at(log1_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(log1_file2_uri));
    REQUIRE(log_file_map.at(log1_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(log1_file3_uri));
    REQUIRE(log_file_map.at(log1_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3"});
    REQUIRE(log_file_map.contains(log1_file4_uri));
    REQUIRE(log_file_map.at(log1_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3"});
    REQUIRE(log_file_map.contains(log2_file1_uri));
    REQUIRE(log_file_map.at(log2_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(log2_file2_uri));
    REQUIRE(log_file_map.at(log2_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(log2_file3_uri));
    REQUIRE(log_file_map.at(log2_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3"});
    REQUIRE(log_file_map.contains(log2_file4_uri));
    REQUIRE(log_file_map.at(log2_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3"});
  }

  SECTION("Desired channels include some files")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel1", "channel2"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, {}, {})));
    REQUIRE(log_file_map.size() == 4U);
    REQUIRE(log_file_map.contains(log1_file1_uri));
    REQUIRE(log_file_map.at(log1_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(log1_file2_uri));
    REQUIRE(log_file_map.at(log1_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(log2_file1_uri));
    REQUIRE(log_file_map.at(log2_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(log2_file2_uri));
    REQUIRE(log_file_map.at(log2_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
  }

  SECTION("Excluded channels include some files")
  {
    const std::pmr::unordered_set<std::pmr::string> excluded_channels{"channel3", "channel4", "channel5", "channel6"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, {}, excluded_channels)));
    REQUIRE(log_file_map.size() == 4U);
    REQUIRE(log_file_map.contains(log1_file1_uri));
    REQUIRE(log_file_map.at(log1_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(log1_file2_uri));
    REQUIRE(log_file_map.at(log1_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(log2_file1_uri));
    REQUIRE(log_file_map.at(log2_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(log2_file2_uri));
    REQUIRE(log_file_map.at(log2_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
  }

  SECTION("No matching channels in desired channels")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"no_such_channel1", "no_such_channel2"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, {}, {})));
    REQUIRE(log_file_map.empty());
  }

  SECTION("Excluded channels excluded all channels")
  {
    const std::pmr::unordered_set<std::pmr::string> excluded_channels{
      "channel1", "channel2", "channel3", "channel4", "channel5", "channel6"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, {}, excluded_channels)));
    REQUIRE(log_file_map.empty());
  }

  SECTION("Desired time range covers entire log")
  {
    LogInterval transmit_time_interval{time1, time8};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, transmit_time_interval, {})));
    REQUIRE(log_file_map.size() == 8U);
    REQUIRE(log_file_map.contains(log1_file1_uri));
    REQUIRE(log_file_map.at(log1_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(log1_file2_uri));
    REQUIRE(log_file_map.at(log1_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(log1_file3_uri));
    REQUIRE(log_file_map.at(log1_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3", "channel4"});
    REQUIRE(log_file_map.contains(log1_file4_uri));
    REQUIRE(log_file_map.at(log1_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3", "channel4"});
    REQUIRE(log_file_map.contains(log2_file1_uri));
    REQUIRE(log_file_map.at(log2_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel5"});
    REQUIRE(log_file_map.contains(log2_file2_uri));
    REQUIRE(log_file_map.at(log2_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel5"});
    REQUIRE(log_file_map.contains(log2_file3_uri));
    REQUIRE(log_file_map.at(log2_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3", "channel6"});
    REQUIRE(log_file_map.contains(log2_file4_uri));
    REQUIRE(log_file_map.at(log2_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3", "channel6"});
  }

  SECTION("Desired time range excludes log files")
  {
    LogInterval transmit_time_interval{time3, time6};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, transmit_time_interval, {})));
    REQUIRE(log_file_map.size() == 4U);
    REQUIRE(log_file_map.contains(log1_file2_uri));
    REQUIRE(log_file_map.at(log1_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(log1_file3_uri));
    REQUIRE(log_file_map.at(log1_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3", "channel4"});
    REQUIRE(log_file_map.contains(log2_file2_uri));
    REQUIRE(log_file_map.at(log2_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel5"});
    REQUIRE(log_file_map.contains(log2_file3_uri));
    REQUIRE(log_file_map.at(log2_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3", "channel6"});
  }

  SECTION("Desired time range excludes all files")
  {
    LogInterval transmit_time_interval{time0, time0};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, transmit_time_interval, {})));
    REQUIRE(log_file_map.empty());
  }

  SECTION("Combine desired channels, excluded channels and time range")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel1", "channel2"};
    const std::pmr::unordered_set<std::pmr::string> excluded_channels{"channel2", "channel3"};
    LogInterval transmit_time_interval{time3, time6};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(
      ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, transmit_time_interval, excluded_channels)));
    REQUIRE(log_file_map.size() == 2U);
    REQUIRE(log_file_map.contains(log1_file2_uri));
    REQUIRE(log_file_map.at(log1_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(log2_file2_uri));
    REQUIRE(log_file_map.at(log2_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
  }

  SECTION("Combine desired channels, excluded channels and time range only from log1")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel1", "channel2"};
    const std::pmr::unordered_set<std::pmr::string> excluded_channels{"channel1", "channel3"};
    LogInterval transmit_time_interval{time3, time6};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(
      ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, transmit_time_interval, excluded_channels)));
    REQUIRE(log_file_map.size() == 1U);
    REQUIRE(log_file_map.contains(log1_file2_uri));
    REQUIRE(log_file_map.at(log1_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel2"});
  }

  SECTION("Combine desired channels, excluded channels and time range only from log2")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel5", "channel6"};
    const std::pmr::unordered_set<std::pmr::string> excluded_channels{"channel1", "channel6"};
    LogInterval transmit_time_interval{time3, time6};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(
      ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, transmit_time_interval, excluded_channels)));
    REQUIRE(log_file_map.size() == 1U);
    REQUIRE(log_file_map.contains(log2_file2_uri));
    REQUIRE(log_file_map.at(log2_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel5"});
  }
}

} // namespace
} // namespace clockwork_logging::offboard
