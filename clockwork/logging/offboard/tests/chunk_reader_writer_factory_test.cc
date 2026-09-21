// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/v1/writer_config.pb.h"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>
#include <google/protobuf/repeated_ptr_field.h>
#include <google/protobuf/text_format.h>

#include <fstream>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("File URIs")
{
  constexpr auto sub_dir_name = "sub_dir";
  constexpr auto log_file_name = "file.slog";
  constexpr auto sub_sub_dir_name = "sub_sub_dir";
  constexpr auto sub_sub_dir_file_name = "test.xxx";

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  ChunkReaderWriterFactory factory{memory_resource};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto sub_dir_path = test_dir.get_path() / sub_dir_name;
  const auto log_file_path = sub_dir_path / log_file_name;
  const auto sub_sub_dir_path = sub_dir_path / sub_sub_dir_name;
  const auto sub_sub_dir_file_path = sub_dir_path / sub_sub_dir_name / sub_sub_dir_file_name;

  REQUIRE(factory.make_chunk_writer(sub_dir_path.string()));
  REQUIRE(factory.make_chunk_reader(sub_dir_path.string()));
  REQUIRE(factory.exists(sub_dir_path.string()) == false);
  REQUIRE(factory.list_log_files(sub_dir_path.string()) == jewels::unexpected(LogError::no_such_file_or_directory));
  REQUIRE(factory.list_subdirs(sub_dir_path.string()) == jewels::unexpected(LogError::no_such_file_or_directory));
  REQUIRE(factory.create_directories(sub_sub_dir_path.string()));
  std::ofstream ofs1{std::string(sub_sub_dir_file_path)};
  REQUIRE(ofs1);
  ofs1.close();
  REQUIRE(factory.exists(sub_sub_dir_path.string()) == true);
  REQUIRE(factory.list_log_files(sub_dir_path.string()) == std::pmr::vector<LogUri>{});
  std::ofstream ofs2{std::string(log_file_path)};
  REQUIRE(ofs2);
  ofs2.close();
  REQUIRE(
    factory.list_log_files(sub_dir_path.string()) ==
    std::pmr::vector<LogUri>{LogUri::try_make(log_file_path.string(), memory_resource).value()});
  REQUIRE(factory.list_subdirs(sub_dir_path.string()));
  REQUIRE(factory.list_subdirs(sub_dir_path.string())->size() == 1U);
  REQUIRE(
    factory.list_subdirs(sub_dir_path.string()) ==
    std::pmr::vector<LogUri>{LogUri::try_make(sub_sub_dir_path.string(), memory_resource).value()});
}

TEST_CASE("Read/Write text proto")
{
  constexpr auto test_file_name = "test_file.txtpb";

  constexpr auto config_text = R"(
      # proto-file: clockwork/logging/offboard/v1/writer_config.proto
      # proto-message: WriterConfig
      rule {
        regex: "test_regex"
      }
    )";
  clockwork::logging::offboard::v1::WriterConfig test_proto;
  REQUIRE(google::protobuf::TextFormat::ParseFromString(config_text, &test_proto));

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  ChunkReaderWriterFactory factory{memory_resource};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_file_path = test_dir.get_path() / test_file_name;

  REQUIRE(factory.write_text_proto(test_file_path.string(), "#HEADER\n", test_proto));
  const auto read_result =
    factory.read_text_proto<clockwork::logging::offboard::v1::WriterConfig>(test_file_path.string());
  REQUIRE(read_result);
  const auto& result_proto = read_result.value();
  REQUIRE(test_proto.rule().size() == result_proto.rule().size());
  REQUIRE(test_proto.rule(0).regex().size() == result_proto.rule(0).regex().size());
  REQUIRE(test_proto.rule(0).regex(0) == result_proto.rule(0).regex(0));

  REQUIRE(factory.write_log_file(test_file_path.string(), {}));
  REQUIRE(
    factory.read_text_proto<clockwork::logging::offboard::v1::WriterConfig>(
      test_file_path.string(), ProtobufReadMode::fail_if_empty) == jewels::unexpected(LogError::empty_metadata_file));
  const auto empty_read_result =
    factory.read_text_proto<clockwork::logging::offboard::v1::WriterConfig>(test_file_path.string());
  REQUIRE(empty_read_result);
  REQUIRE(empty_read_result.value().rule().empty());
}

TEST_CASE("Read/Write binary proto")
{
  constexpr auto test_file_name = "test_file.bin";

  constexpr auto config_text = R"(
      # proto-file: clockwork/logging/offboard/v1/writer_config.proto
      # proto-message: WriterConfig
      rule {
        regex: "test_regex"
      }
    )";
  clockwork::logging::offboard::v1::WriterConfig test_proto;
  REQUIRE(google::protobuf::TextFormat::ParseFromString(config_text, &test_proto));

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  ChunkReaderWriterFactory factory{memory_resource};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_file_path = test_dir.get_path() / test_file_name;

  REQUIRE(factory.write_binary_proto(test_file_path.string(), test_proto));
  const auto read_result =
    factory.read_binary_proto<clockwork::logging::offboard::v1::WriterConfig>(test_file_path.string());
  REQUIRE(read_result);
  const auto& result_proto = read_result.value();
  REQUIRE(test_proto.rule().size() == result_proto.rule().size());
  REQUIRE(test_proto.rule(0).regex().size() == result_proto.rule(0).regex().size());
  REQUIRE(test_proto.rule(0).regex(0) == result_proto.rule(0).regex(0));

  REQUIRE(factory.write_log_file(test_file_path.string(), {}));
  REQUIRE(
    factory.read_binary_proto<clockwork::logging::offboard::v1::WriterConfig>(
      test_file_path.string(), ProtobufReadMode::fail_if_empty) == jewels::unexpected(LogError::empty_metadata_file));
  const auto empty_read_result =
    factory.read_binary_proto<clockwork::logging::offboard::v1::WriterConfig>(test_file_path.string());
  REQUIRE(empty_read_result);
  REQUIRE(empty_read_result.value().rule().empty());
}

TEST_CASE("S3 URIs")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  ChunkReaderWriterFactory factory{memory_resource};

  REQUIRE(factory.make_chunk_writer("s3://xxx/yyy"));
  REQUIRE(factory.make_chunk_reader("s3://xxx/yyy"));
}

} // namespace
} // namespace clockwork_logging::offboard
