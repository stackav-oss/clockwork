// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_metadata_file_helper.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/v1/log_amendment.pb.h"
#include "clockwork/logging/offboard/v1/log_metadata.pb.h"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
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
  constexpr auto amended_metadata_proto_text = R"(
    # proto-file: clockwork/logging/offboard/v1/log_metadata.proto
    # proto-message: LogMetadata
    log_writer_metadata {
      channel: "channel1"
      channel: "channel2"
      channel: "channel3"
      channel: "excluded_channel"
      log_file_metadata {
        log_file_name: "channel123_0.slog"
        min_transmit_time_ns: 1000000000
        max_transmit_time_ns: 2000000000
      }
      log_file_metadata {
        log_file_name: "channel123_1.slog"
        min_transmit_time_ns: 5000000000
        max_transmit_time_ns: 6000000000
      }
      persistent_channel: "channel1"
      persistent_channel: "channel2"
    }
    log_writer_metadata {
      channel: "channel1"
      channel: "channel4"
      channel: "channel5"
      channel: "excluded_channel"
      log_file_metadata {
        log_file_name: "channel145_0.slog"
        min_transmit_time_ns: 3000000000
        max_transmit_time_ns: 4000000000
      }
      log_file_metadata {
        log_file_name: "channel145_1.slog"
        min_transmit_time_ns: 7000000000
        max_transmit_time_ns: 7999999999
      }
      persistent_channel: "channel1"
      persistent_channel: "channel2"
    }
    min_transmit_time_ns: 1000000000
    max_transmit_time_ns: 7999999999
  )";
  ::clockwork::logging::offboard::v1::LogMetadata amended_metadata_protobuf;
  REQUIRE(google::protobuf::TextFormat::ParseFromString(amended_metadata_proto_text, &amended_metadata_protobuf));

  constexpr auto amendment_metadata_proto_text = R"(
    # proto-file: clockwork/logging/offboard/v1/log_amendment.proto
    # proto-message: LogMetadata
    amendment_metadata {
      log_writer_metadata {
        channel: "channel1"
        channel: "channel6"
        log_file_metadata {
          log_file_name: "channel16_0.slog"
          min_transmit_time_ns: 1000000001
          max_transmit_time_ns: 2000000000
        }
        log_file_metadata {
          log_file_name: "channel16_1.slog"
          min_transmit_time_ns: 5000000000
          max_transmit_time_ns: 6000000000
        }
        persistent_channel: "channel6"
      }
      log_writer_metadata {
        channel: "channel1"
        channel: "channel7"
        log_file_metadata {
          log_file_name: "channel17_0.slog"
          min_transmit_time_ns: 3000000000
          max_transmit_time_ns: 4000000000
        }
        log_file_metadata {
          log_file_name: "channel17_1.slog"
          min_transmit_time_ns: 7000000000
          max_transmit_time_ns: 8000000000
        }
      }
      min_transmit_time_ns: 1000000001
      max_transmit_time_ns: 8000000000
    }
    amended_log_path
    {
      excluded_channel: "channel1"
      excluded_channel: "channel6"
      excluded_channel: "channel7"
    }
  )";
  ::clockwork::logging::offboard::v1::LogAmendment amendment_metadata_protobuf;
  REQUIRE(google::protobuf::TextFormat::ParseFromString(amendment_metadata_proto_text, &amendment_metadata_protobuf));

  const auto use_relative_path = GENERATE(false, true);

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto amended_path = test_dir.get_path() / "amended";
  const auto amendment_path = test_dir.get_path() / "amendment";

  std::filesystem::create_directory(amended_path.c_str());
  std::filesystem::create_directory(amendment_path.c_str());

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  ChunkReaderWriterFactory chunk_reader_writer_factory{memory_resource};
  const auto amended_make_result =
    LogUri::try_make(std::string{"file:"}.append(amended_path.string()), memory_resource);
  REQUIRE(amended_make_result);
  const auto& amended_uri = amended_make_result.value();
  const auto amendment_make_result =
    LogUri::try_make(std::string{"file:"}.append(amendment_path.string()), memory_resource);
  REQUIRE(amendment_make_result);
  const auto& amendment_uri = amendment_make_result.value();

  constexpr LogTimestamp time0(std::chrono::seconds(0));
  constexpr LogTimestamp time1(std::chrono::seconds(1));
  constexpr LogTimestamp time3(std::chrono::seconds(3));
  constexpr LogTimestamp time6(std::chrono::seconds(6));
  constexpr LogTimestamp time8(std::chrono::seconds(8));

  const auto amended_metadata_file_uri = amended_uri / "stack_log_metadata.pbtxt";
  const auto amended_file1_uri = (amended_uri / "channel123_0.slog").string();
  const auto amended_file2_uri = (amended_uri / "channel123_1.slog").string();
  const auto amended_file3_uri = (amended_uri / "channel145_0.slog").string();
  const auto amended_file4_uri = (amended_uri / "channel145_1.slog").string();

  REQUIRE(chunk_reader_writer_factory.write_text_proto<::clockwork::logging::offboard::v1::LogMetadata>(
    amended_metadata_file_uri.string(), "", amended_metadata_protobuf));
  REQUIRE(chunk_reader_writer_factory.write_log_file(amended_file1_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(amended_file2_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(amended_file3_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(amended_file4_uri, {}));

  const auto amendment_metadata_file_uri = amendment_uri / "stack_log_amendment.pbtxt";
  const auto amendment_file1_uri = (amendment_uri / "channel16_0.slog").string();
  const auto amendment_file2_uri = (amendment_uri / "channel16_1.slog").string();
  const auto amendment_file3_uri = (amendment_uri / "channel17_0.slog").string();
  const auto amendment_file4_uri = (amendment_uri / "channel17_1.slog").string();

  if (!use_relative_path)
  {
    amendment_metadata_protobuf.mutable_amended_log_path()->set_absolute_path(amended_uri.string());
  }
  else
  {
    amendment_metadata_protobuf.mutable_amended_log_path()->set_relative_path("../amended");
  }

  const auto log_amendment_file_uri = amendment_uri / "stack_log_amendment.pbtxt";

  REQUIRE(chunk_reader_writer_factory.write_text_proto<::clockwork::logging::offboard::v1::LogAmendment>(
    log_amendment_file_uri.string(), "", amendment_metadata_protobuf));
  REQUIRE(chunk_reader_writer_factory.write_log_file(amendment_file1_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(amendment_file2_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(amendment_file3_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(amendment_file4_uri, {}));

  LogAmendmentFileHelper helper(memory_resource, false);
  REQUIRE(ok(helper.initialize(log_amendment_file_uri, {"excluded_channel"}, chunk_reader_writer_factory)));
  REQUIRE(helper.get_transmit_time_interval() == LogInterval{time1, time8});

  REQUIRE(
    helper.get_channels() == std::pmr::unordered_set<std::pmr::string>{
                               "channel1", "channel2", "channel3", "channel4", "channel5", "channel6", "channel7"});

  REQUIRE(helper.get_persistent_channels() == std::pmr::unordered_set<std::pmr::string>{"channel2", "channel6"});

  SECTION("No desired channels or time range, unknown files are filtered out")
  {
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, {}, {})));
    REQUIRE(log_file_map.size() == 8U);
    REQUIRE(log_file_map.contains(amended_file1_uri));
    REQUIRE(log_file_map.at(amended_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel2", "channel3"});
    REQUIRE(log_file_map.contains(amended_file2_uri));
    REQUIRE(log_file_map.at(amended_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel2", "channel3"});
    REQUIRE(log_file_map.contains(amended_file3_uri));
    REQUIRE(log_file_map.at(amended_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel4", "channel5"});
    REQUIRE(log_file_map.contains(amended_file4_uri));
    REQUIRE(log_file_map.at(amended_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel4", "channel5"});
    REQUIRE(log_file_map.contains(amendment_file1_uri));
    REQUIRE(log_file_map.at(amendment_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel6"});
    REQUIRE(log_file_map.contains(amendment_file2_uri));
    REQUIRE(log_file_map.at(amendment_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel6"});
    REQUIRE(log_file_map.contains(amendment_file3_uri));
    REQUIRE(log_file_map.at(amendment_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel7"});
    REQUIRE(log_file_map.contains(amendment_file4_uri));
    REQUIRE(log_file_map.at(amendment_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel7"});
  }

  SECTION("Desired channels include all files")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel1", "channel2", "channel4"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, {}, {})));
    REQUIRE(log_file_map.size() == 8U);
    REQUIRE(log_file_map.contains(amended_file1_uri));
    REQUIRE(log_file_map.at(amended_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel2"});
    REQUIRE(log_file_map.contains(amended_file2_uri));
    REQUIRE(log_file_map.at(amended_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel2"});
    REQUIRE(log_file_map.contains(amended_file3_uri));
    REQUIRE(log_file_map.at(amended_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel4"});
    REQUIRE(log_file_map.contains(amended_file4_uri));
    REQUIRE(log_file_map.at(amended_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel4"});
    REQUIRE(log_file_map.contains(amendment_file1_uri));
    REQUIRE(log_file_map.at(amendment_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(amendment_file2_uri));
    REQUIRE(log_file_map.at(amendment_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(amendment_file3_uri));
    REQUIRE(log_file_map.at(amendment_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(amendment_file4_uri));
    REQUIRE(log_file_map.at(amendment_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
  }

  SECTION("Excluded channels include all files")
  {
    const std::pmr::unordered_set<std::pmr::string> excluded_channels{"channel3", "channel5", "channel6", "channel7"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, {}, excluded_channels)));
    REQUIRE(log_file_map.size() == 8U);
    REQUIRE(log_file_map.contains(amended_file1_uri));
    REQUIRE(log_file_map.at(amended_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel2"});
    REQUIRE(log_file_map.contains(amended_file2_uri));
    REQUIRE(log_file_map.at(amended_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel2"});
    REQUIRE(log_file_map.contains(amended_file3_uri));
    REQUIRE(log_file_map.at(amended_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel4"});
    REQUIRE(log_file_map.contains(amended_file4_uri));
    REQUIRE(log_file_map.at(amended_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel4"});
    REQUIRE(log_file_map.contains(amendment_file1_uri));
    REQUIRE(log_file_map.at(amendment_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(amendment_file2_uri));
    REQUIRE(log_file_map.at(amendment_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(amendment_file3_uri));
    REQUIRE(log_file_map.at(amendment_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(amendment_file4_uri));
  }

  SECTION("Desired channels include some files")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel2", "channel6"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, {}, {})));
    REQUIRE(log_file_map.size() == 4U);
  }

  SECTION("Excluded channels include some files")
  {
    const std::pmr::unordered_set<std::pmr::string> excluded_channels{
      "channel1", "channel3", "channel4", "channel5", "channel7"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, {}, excluded_channels)));
    REQUIRE(log_file_map.size() == 4U);
    REQUIRE(log_file_map.contains(amended_file1_uri));
    REQUIRE(log_file_map.at(amended_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel2"});
    REQUIRE(log_file_map.contains(amended_file2_uri));
    REQUIRE(log_file_map.at(amended_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel2"});
    REQUIRE(log_file_map.contains(amendment_file1_uri));
    REQUIRE(log_file_map.at(amendment_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel6"});
    REQUIRE(log_file_map.contains(amendment_file2_uri));
    REQUIRE(log_file_map.at(amendment_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel6"});
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
      "channel1", "channel2", "channel3", "channel4", "channel5", "channel6", "channel7"};
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
    REQUIRE(log_file_map.contains(amended_file1_uri));
    REQUIRE(log_file_map.at(amended_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel2", "channel3"});
    REQUIRE(log_file_map.contains(amended_file2_uri));
    REQUIRE(log_file_map.at(amended_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel2", "channel3"});
    REQUIRE(log_file_map.contains(amended_file3_uri));
    REQUIRE(log_file_map.at(amended_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel4", "channel5"});
    REQUIRE(log_file_map.contains(amended_file4_uri));
    REQUIRE(log_file_map.at(amended_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel4", "channel5"});
    REQUIRE(log_file_map.contains(amendment_file1_uri));
    REQUIRE(log_file_map.at(amendment_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel6"});
    REQUIRE(log_file_map.contains(amendment_file2_uri));
    REQUIRE(log_file_map.at(amendment_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel6"});
    REQUIRE(log_file_map.contains(amendment_file3_uri));
    REQUIRE(log_file_map.at(amendment_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel7"});
    REQUIRE(log_file_map.contains(amendment_file4_uri));
    REQUIRE(log_file_map.at(amendment_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel7"});
  }

  SECTION("Desired time range excludes log files")
  {
    LogInterval transmit_time_interval{time3, time6};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, transmit_time_interval, {})));
    REQUIRE(log_file_map.size() == 6U);
    REQUIRE(log_file_map.contains(amended_file1_uri));
    REQUIRE(log_file_map.at(amended_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel2", "channel3"});
    REQUIRE(log_file_map.contains(amended_file2_uri));
    REQUIRE(log_file_map.at(amended_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel2", "channel3"});
    REQUIRE(log_file_map.contains(amended_file3_uri));
    REQUIRE(log_file_map.at(amended_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel4", "channel5"});
    REQUIRE(log_file_map.contains(amendment_file1_uri));
    REQUIRE(log_file_map.at(amendment_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel6"});
    REQUIRE(log_file_map.contains(amendment_file2_uri));
    REQUIRE(log_file_map.at(amendment_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel6"});
    REQUIRE(log_file_map.contains(amendment_file3_uri));
    REQUIRE(log_file_map.at(amendment_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel7"});
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
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel2", "channel4", "channel5"};
    const std::pmr::unordered_set<std::pmr::string> excluded_channels{"channel2", "channel3", "channel4"};
    LogInterval transmit_time_interval{time3, time6};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(
      ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, transmit_time_interval, excluded_channels)));
    REQUIRE(log_file_map.size() == 1U);
    REQUIRE(log_file_map.contains(amended_file3_uri));
    REQUIRE(log_file_map.at(amended_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel5"});
  }

  SECTION("Combine desired channels, excluded channels and time range only from amendment")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel1", "channel2", "channel4", "channel6"};
    const std::pmr::unordered_set<std::pmr::string> excluded_channels{"channel2", "channel3", "channel4"};
    LogInterval transmit_time_interval{time3, time6};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(
      ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, transmit_time_interval, excluded_channels)));
    REQUIRE(log_file_map.size() == 3U);
    REQUIRE(log_file_map.contains(amendment_file1_uri));
    REQUIRE(log_file_map.at(amendment_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel6"});
    REQUIRE(log_file_map.contains(amendment_file2_uri));
    REQUIRE(log_file_map.at(amendment_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel6"});
    REQUIRE(log_file_map.contains(amendment_file3_uri));
    REQUIRE(log_file_map.at(amendment_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
  }

  SECTION("Combine desired channels, excluded channels and time range only from amended")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel2", "channel4", "channel5"};
    const std::pmr::unordered_set<std::pmr::string> excluded_channels{"channel2", "channel3", "channel4"};
    LogInterval transmit_time_interval{time3, time6};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(
      ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, transmit_time_interval, excluded_channels)));
    REQUIRE(log_file_map.size() == 1U);
    REQUIRE(log_file_map.contains(amended_file3_uri));
    REQUIRE(log_file_map.at(amended_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel5"});
  }
}

TEST_CASE("LogMetadataFileHelper in merge logs union")
{
  constexpr auto amended_metadata_proto_text = R"(
    # proto-file: clockwork/logging/offboard/v1/log_metadata.proto
    # proto-message: LogMetadata
    log_writer_metadata {
      channel: "channel2"
      log_file_metadata {
        log_file_name: "channel2_0.slog"
      }
      persistent_channel: "channel2"
    }
    min_transmit_time_ns: 1000000000
    max_transmit_time_ns: 8000000000
  )";
  ::clockwork::logging::offboard::v1::LogMetadata amended_metadata_protobuf;
  REQUIRE(google::protobuf::TextFormat::ParseFromString(amended_metadata_proto_text, &amended_metadata_protobuf));

  constexpr auto amendment_metadata_proto_text = R"(
    # proto-file: clockwork/logging/offboard/v1/log_amendment.proto
    # proto-message: LogMetadata
    amendment_metadata {
      log_writer_metadata {
        channel: "channel1"
        channel: "channel6"
        channel: "excluded_channel"
        log_file_metadata {
          log_file_name: "channel16_0.slog"
          min_transmit_time_ns: 2000000000
          max_transmit_time_ns: 3000000000
        }
        log_file_metadata {
          log_file_name: "channel16_1.slog"
          min_transmit_time_ns: 4000000000
          max_transmit_time_ns: 5000000000
        }
        persistent_channel: "channel6"
      }
      log_writer_metadata {
        channel: "channel1"
        channel: "channel7"
        log_file_metadata {
          log_file_name: "channel17_0.slog"
          min_transmit_time_ns: 3000000000
          max_transmit_time_ns: 4000000000
        }
        log_file_metadata {
          log_file_name: "channel17_1.slog"
          min_transmit_time_ns: 5000000000
          max_transmit_time_ns: 6000000000
        }
      }
      min_transmit_time_ns: 2000000000
      max_transmit_time_ns: 6000000000
    }
    amended_log_path
    {
      excluded_channel: "channel1"
      excluded_channel: "channel6"
      excluded_channel: "channel7"
    }
  )";
  ::clockwork::logging::offboard::v1::LogAmendment amendment_metadata_protobuf;
  REQUIRE(google::protobuf::TextFormat::ParseFromString(amendment_metadata_proto_text, &amendment_metadata_protobuf));

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto amended_path = test_dir.get_path() / "amended";
  const auto amendment_path = test_dir.get_path() / "amendment";

  std::filesystem::create_directory(amended_path.c_str());
  std::filesystem::create_directory(amendment_path.c_str());

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  ChunkReaderWriterFactory chunk_reader_writer_factory{memory_resource};
  const auto amended_make_result =
    LogUri::try_make(std::string{"file:"}.append(amended_path.string()), memory_resource);
  REQUIRE(amended_make_result);
  const auto& amended_uri = amended_make_result.value();
  const auto amendment_make_result =
    LogUri::try_make(std::string{"file:"}.append(amendment_path.string()), memory_resource);
  REQUIRE(amendment_make_result);
  const auto& amendment_uri = amendment_make_result.value();

  constexpr LogTimestamp time2(std::chrono::seconds(2));
  constexpr LogTimestamp time6(std::chrono::seconds(6));

  const auto amended_metadata_file_uri = amended_uri / "stack_log_metadata.pbtxt";
  const auto amended_file1_uri = (amended_uri / "channel2_0.slog").string();

  REQUIRE(chunk_reader_writer_factory.write_text_proto<::clockwork::logging::offboard::v1::LogMetadata>(
    amended_metadata_file_uri.string(), "", amended_metadata_protobuf));
  REQUIRE(chunk_reader_writer_factory.write_log_file(amended_file1_uri, {}));

  const auto amendment_metadata_file_uri = amendment_uri / "stack_log_amendment.pbtxt";
  const auto amendment_file1_uri = (amendment_uri / "channel16_0.slog").string();
  const auto amendment_file2_uri = (amendment_uri / "channel16_1.slog").string();
  const auto amendment_file3_uri = (amendment_uri / "channel17_0.slog").string();
  const auto amendment_file4_uri = (amendment_uri / "channel17_1.slog").string();

  amendment_metadata_protobuf.mutable_amended_log_path()->set_relative_path("../amended");

  const auto log_amendment_file_uri = amendment_uri / "stack_log_amendment.pbtxt";

  REQUIRE(chunk_reader_writer_factory.write_text_proto<::clockwork::logging::offboard::v1::LogAmendment>(
    log_amendment_file_uri.string(), "", amendment_metadata_protobuf));
  REQUIRE(chunk_reader_writer_factory.write_log_file(amendment_file1_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(amendment_file2_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(amendment_file3_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(amendment_file4_uri, {}));

  LogAmendmentFileHelper helper(memory_resource, true);
  REQUIRE(ok(helper.initialize(log_amendment_file_uri, {"excluded_channel"}, chunk_reader_writer_factory)));
  REQUIRE(helper.get_transmit_time_interval() == LogInterval{time2, time6});

  REQUIRE(helper.get_channels() == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel6", "channel7"});

  REQUIRE(helper.get_persistent_channels() == std::pmr::unordered_set<std::pmr::string>{"channel6"});

  std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
  REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, {}, {})));
  REQUIRE(log_file_map.size() == 4U);
  REQUIRE(log_file_map.contains(amendment_file1_uri));
  REQUIRE(log_file_map.at(amendment_file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel6"});
  REQUIRE(log_file_map.contains(amendment_file2_uri));
  REQUIRE(log_file_map.at(amendment_file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel6"});
  REQUIRE(log_file_map.contains(amendment_file3_uri));
  REQUIRE(log_file_map.at(amendment_file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel7"});
  REQUIRE(log_file_map.contains(amendment_file4_uri));
  REQUIRE(log_file_map.at(amendment_file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel7"});
}

} // namespace
} // namespace clockwork_logging::offboard
