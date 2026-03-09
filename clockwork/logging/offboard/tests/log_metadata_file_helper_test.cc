// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_metadata_file_helper.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/v1/log_metadata.pb.h"
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
#include <unordered_set>
#include <vector>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("LogMetadataFileHelper")
{
  constexpr auto log_metadata_proto_text = R"(
    # proto-file: clockwork/logging/offboard/v1/log_metadata.proto
    # proto-message: LogMetadata
    log_writer_metadata {
      channel: "channel1",
      channel: "channel2",
      persistent_channel: "channel2",
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
      channel: "channel3",
      channel: "channel4",
      persistent_channel: "channel4",
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
  const auto file5_uri = (log_uri / "no_such_file.slog").string();

  REQUIRE(chunk_reader_writer_factory.write_text_proto<::clockwork::logging::offboard::v1::LogMetadata>(
    log_metadata_file_uri.string(), "", log_metadata_protobuf));
  REQUIRE(chunk_reader_writer_factory.write_log_file(file1_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(file2_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(file3_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(file4_uri, {}));
  REQUIRE(chunk_reader_writer_factory.write_log_file(file5_uri, {}));

  LogMetadataFileHelper helper(memory_resource);
  REQUIRE(helper.initialize(log_metadata_file_uri, chunk_reader_writer_factory));
  REQUIRE(helper.get_transmit_time_interval() == LogInterval{time1, time8});

  REQUIRE(
    helper.get_channels() == std::pmr::unordered_set<std::pmr::string>{"channel1", "channel2", "channel3", "channel4"});
  REQUIRE(helper.get_persistent_channels() == std::pmr::unordered_set<std::pmr::string>{"channel2", "channel4"});

  SECTION("No desired channels or time range, unknown files are filtered out")
  {
    REQUIRE(
      helper.list_log_files({}, {}) == std::pmr::vector<std::pmr::string>{file1_uri, file2_uri, file3_uri, file4_uri});
  }

  SECTION("Desired channels include all files")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel1", "channel3"};
    REQUIRE(
      helper.list_log_files(desired_channels, {}) ==
      std::pmr::vector<std::pmr::string>{file1_uri, file2_uri, file3_uri, file4_uri});
  }

  SECTION("Desired channels include some files")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel1", "channel2"};
    REQUIRE(helper.list_log_files(desired_channels, {}) == std::pmr::vector<std::pmr::string>{file1_uri, file2_uri});
  }

  SECTION("No matching channels in desired channels")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"no_such_channel1", "no_such_channel2"};
    REQUIRE(helper.list_log_files(desired_channels, {}) == std::pmr::vector<std::pmr::string>{});
  }

  SECTION("Desired time range covers entire log")
  {
    LogInterval transmit_time_interval{time1, time8};
    REQUIRE(
      helper.list_log_files({}, transmit_time_interval) ==
      std::pmr::vector<std::pmr::string>{file1_uri, file2_uri, file3_uri, file4_uri});
  }

  SECTION("Desired time range excludes log files with persistent channels")
  {
    LogInterval transmit_time_interval{time3, time6};
    REQUIRE(
      helper.list_log_files({}, transmit_time_interval) ==
      std::pmr::vector<std::pmr::string>{file1_uri, file2_uri, file3_uri});
  }

  SECTION("Desired time range excludes log files")
  {
    LogInterval transmit_time_interval{time3, time6};
    REQUIRE(
      helper.list_log_files(
        std::pmr::unordered_set<std::pmr::string>{"channel1", "channel3"}, transmit_time_interval) ==
      std::pmr::vector<std::pmr::string>{file2_uri, file3_uri});
  }

  SECTION("Desired time range excludes all files")
  {
    LogInterval transmit_time_interval{time0, time0};
    REQUIRE(helper.list_log_files({}, transmit_time_interval) == std::pmr::vector<std::pmr::string>{});
  }

  SECTION("Combine desired channels and time range")
  {
    const std::pmr::unordered_set<std::pmr::string> desired_channels{"channel1"};
    LogInterval transmit_time_interval{time3, time6};
    REQUIRE(
      helper.list_log_files(desired_channels, transmit_time_interval) == std::pmr::vector<std::pmr::string>{file2_uri});
  }
}

} // namespace
} // namespace clockwork_logging::offboard
