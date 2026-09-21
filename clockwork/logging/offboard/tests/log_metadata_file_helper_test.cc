// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_metadata_file_helper.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/v1/log_metadata.pb.h"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>
#include <google/protobuf/text_format.h>

#include <chrono>
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
  constexpr auto log_metadata_proto_text = R"(
    # proto-file: clockwork/logging/offboard/v1/log_metadata.proto
    # proto-message: LogMetadata
    log_writer_metadata {
      channel: "channel1"
      channel: "channel2"
      channel: "excluded_channel"
      persistent_channel: "channel2"
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
      persistent_channel: "channel4"
      log_file_metadata {
        log_file_name: "channel34_0.slog"
        min_transmit_time_ns: 3000000000
        max_transmit_time_ns: 4000000000
      }
      log_file_metadata {
        log_file_name: "channel34_1.slog"
        min_transmit_time_ns: 7000000000
        max_transmit_time_ns: 8000000000
      }
    }
    min_transmit_time_ns: 1000000000
    max_transmit_time_ns: 8000000000
  )";
  ::clockwork::logging::offboard::v1::LogMetadata log_metadata_protobuf;
  REQUIRE(google::protobuf::TextFormat::ParseFromString(log_metadata_proto_text, &log_metadata_protobuf));

  const jewels::testing::TmpDirectoryGuard test_dir;

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  ChunkReaderWriterFactory chunk_reader_writer_factory{memory_resource};
  const auto make_result = LogUri::try_make(std::string{"file:"}.append(test_dir.get_path().string()), memory_resource);
  REQUIRE(make_result);
  const auto& log_uri = make_result.value();

  constexpr LogTimestamp time0(std::chrono::seconds(0));
  constexpr LogTimestamp time1(std::chrono::seconds(1));
  constexpr LogTimestamp time3(std::chrono::seconds(3));
  constexpr LogTimestamp time6(std::chrono::seconds(6));
  constexpr LogTimestamp time8(std::chrono::seconds(8));

  const auto log_metadata_file_uri = log_uri / "stack_log_metadata.pbtxt";
  const auto file1_uri = (log_uri / "channel12_0.slog").string();
  const auto file2_uri = (log_uri / "channel12_1.slog").string();
  const auto file3_uri = (log_uri / "channel34_0.slog").string();
  const auto file4_uri = (log_uri / "channel34_1.slog").string();

  REQUIRE(chunk_reader_writer_factory.write_text_proto<::clockwork::logging::offboard::v1::LogMetadata>(
    log_metadata_file_uri.string(), "", log_metadata_protobuf));
  REQUIRE(chunk_reader_writer_factory.write_log_file(file1_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(file2_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(file3_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(file4_uri, {}));

  LogMetadataFileHelper helper(memory_resource);
  REQUIRE(ok(helper.initialize(log_metadata_file_uri, {"excluded_channel"}, chunk_reader_writer_factory)));
  REQUIRE(helper.get_transmit_time_interval() == LogInterval{time1, time8});

  REQUIRE(
    helper.get_channels() == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2", "channel3", "channel4"});
  REQUIRE(helper.get_persistent_channels() == std::pmr::unordered_set<std::pmr::string>{"channel2", "channel4"});

  SECTION("No desired channels or time range, unknown files are filtered out")
  {
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, {}, {})));
    REQUIRE(log_file_map.size() == 4U);
    REQUIRE(log_file_map.contains(file1_uri));
    REQUIRE(log_file_map.at(file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(file2_uri));
    REQUIRE(log_file_map.at(file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(file3_uri));
    REQUIRE(log_file_map.at(file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3", "channel4"});
    REQUIRE(log_file_map.contains(file4_uri));
    REQUIRE(log_file_map.at(file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3", "channel4"});
  }

  SECTION("Desired channels include all files")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel1", "channel3"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, {}, {})));
    REQUIRE(log_file_map.size() == 4U);
    REQUIRE(log_file_map.contains(file1_uri));
    REQUIRE(log_file_map.at(file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(file2_uri));
    REQUIRE(log_file_map.at(file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(file3_uri));
    REQUIRE(log_file_map.at(file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3"});
    REQUIRE(log_file_map.contains(file4_uri));
    REQUIRE(log_file_map.at(file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3"});
  }

  SECTION("Excluded channels include all files")
  {
    const std::pmr::unordered_set<std::pmr::string> excluded_channels{"channel2", "channel4"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, {}, excluded_channels)));
    REQUIRE(log_file_map.size() == 4U);
    REQUIRE(log_file_map.contains(file1_uri));
    REQUIRE(log_file_map.at(file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(file2_uri));
    REQUIRE(log_file_map.at(file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(file3_uri));
    REQUIRE(log_file_map.at(file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3"});
    REQUIRE(log_file_map.contains(file4_uri));
    REQUIRE(log_file_map.at(file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3"});
  }

  SECTION("Desired channels include some files")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel1", "channel2"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, {}, {})));
    REQUIRE(log_file_map.size() == 2U);
    REQUIRE(log_file_map.contains(file1_uri));
    REQUIRE(log_file_map.at(file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(file2_uri));
    REQUIRE(log_file_map.at(file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
  }

  SECTION("Excluded channels include some files")
  {
    const std::pmr::unordered_set<std::pmr::string> excluded_channels{"channel3", "channel4"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, {}, excluded_channels)));
    REQUIRE(log_file_map.size() == 2U);
    REQUIRE(log_file_map.contains(file1_uri));
    REQUIRE(log_file_map.at(file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(file2_uri));
    REQUIRE(log_file_map.at(file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
  }

  SECTION("No matching channels in desired channels")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"no_such_channel1", "no_such_channel2"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, {}, {})));
    REQUIRE(log_file_map.empty());
  }

  SECTION("All channels excluded")
  {
    const std::pmr::unordered_set<std::pmr::string> excluded_channels{"channel1", "channel2", "channel3", "channel4"};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, {}, excluded_channels)));
    REQUIRE(log_file_map.empty());
  }

  SECTION("Desired time range covers entire log")
  {
    LogInterval transmit_time_interval{time1, time8};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, transmit_time_interval, {})));
    REQUIRE(log_file_map.size() == 4U);
    REQUIRE(log_file_map.contains(file1_uri));
    REQUIRE(log_file_map.at(file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(file2_uri));
    REQUIRE(log_file_map.at(file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(file3_uri));
    REQUIRE(log_file_map.at(file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3", "channel4"});
    REQUIRE(log_file_map.contains(file4_uri));
    REQUIRE(log_file_map.at(file4_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3", "channel4"});
  }

  SECTION("Desired time range excludes log files with persistent channels")
  {
    LogInterval transmit_time_interval{time3, time6};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, {}, transmit_time_interval, {})));
    REQUIRE(log_file_map.size() == 3U);
    REQUIRE(log_file_map.contains(file1_uri));
    REQUIRE(log_file_map.at(file1_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(file2_uri));
    REQUIRE(log_file_map.at(file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2"});
    REQUIRE(log_file_map.contains(file3_uri));
    REQUIRE(log_file_map.at(file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3", "channel4"});
  }

  SECTION("Desired time range excludes log files")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel1", "channel3"};
    LogInterval transmit_time_interval{time3, time6};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, transmit_time_interval, {})));
    REQUIRE(log_file_map.size() == 2U);
    REQUIRE(log_file_map.contains(file2_uri));
    REQUIRE(log_file_map.at(file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
    REQUIRE(log_file_map.contains(file3_uri));
    REQUIRE(log_file_map.at(file3_uri) == std::pmr::unordered_set<std::pmr::string>{"channel3"});
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
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel1", "channel4"};
    const std::pmr::unordered_set<std::pmr::string> excluded_channels{"channel3", "channel4"};
    LogInterval transmit_time_interval{time3, time6};
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    REQUIRE(
      ok(helper.get_log_file_map(Out{log_file_map}, desired_channels, transmit_time_interval, excluded_channels)));
    REQUIRE(log_file_map.size() == 1U);
    REQUIRE(log_file_map.contains(file2_uri));
    REQUIRE(log_file_map.at(file2_uri) == std::pmr::unordered_set<std::pmr::string>{"channel1"});
  }
}

} // namespace
} // namespace clockwork_logging::offboard
