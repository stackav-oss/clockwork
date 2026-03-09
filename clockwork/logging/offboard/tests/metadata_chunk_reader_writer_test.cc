// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/file_chunk_reader.hh"
#include "clockwork/logging/offboard/file_chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/metadata_chunk_reader.hh"
#include "clockwork/logging/offboard/metadata_chunk_writer.hh"
#include "clockwork/logging/offboard/reader_types.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/filesystem_wrapper.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <functional>
#include <memory>
#include <memory_resource>
#include <string>
#include <unordered_map>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("Metadata chunk reader/writer")
{
  constexpr auto test_file_name = "test_file.slog";

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_file_path = test_dir.get_path() / test_file_name;
  ChunkCompressor compressor{memory_resource};
  const auto writer_result = FileChunkWriter<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(writer_result);
  auto& file_writer = *writer_result.value();
  const auto reader_result = FileChunkReader<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(reader_result);
  auto& file_reader = *reader_result.value();
  MetadataChunkWriter metadata_writer{memory_resource};

  constexpr auto channel_name1 = "channel_1";
  constexpr auto compression_type1 = CompressionType::none;
  constexpr auto message_encoding1 = MessageEncoding::unspecified;
  constexpr auto channel_type1 = ChannelType::regular;
  constexpr auto schema_name1 = "schema1";
  constexpr auto schema_encoding1 = SchemaEncoding::clockwork_tachyon;
  constexpr auto schema_definition1 = "schema_definition 1";

  constexpr auto channel_name2 = "channel2";
  constexpr auto compression_type2 = CompressionType::zstd;
  constexpr auto message_encoding2 = MessageEncoding::undefined;
  constexpr auto channel_type2 = ChannelType::persistent;
  constexpr auto schema_name2 = "schema2";
  constexpr auto schema_encoding2 = SchemaEncoding::undefined;
  constexpr auto schema_definition2 = "schema_definition 2";

  SECTION("Empty metadata")
  {
    REQUIRE(file_writer.open());
    const auto write_result = metadata_writer.write_chunk(compressor, file_writer);
    REQUIRE(write_result);
    REQUIRE(file_writer.close());
    REQUIRE(file_reader.open());
    const auto read_result = read_metadata_chunk(memory_resource, write_result.value(), file_reader, compressor);
    REQUIRE(read_result);
    REQUIRE(read_result.value().empty());
  }

  SECTION("Chunk with metadata")
  {
    REQUIRE(
      metadata_writer.add_channel(
        reader::LoggedChannelInfo{
          .compression_type = compression_type1,
          .channel_name = channel_name1,
          .message_encoding = message_encoding1,
          .channel_type = channel_type1,
          .schema_name = schema_name1,
          .schema_encoding = schema_encoding1,
          .schema_definition = schema_definition1,
        }) == 1U);
    REQUIRE(metadata_writer.get_channel_id("channel_1") == 1U);
    REQUIRE(metadata_writer.get_compression_type(1U) == compression_type1);
    REQUIRE(
      metadata_writer.add_channel(
        reader::LoggedChannelInfo{
          .compression_type = compression_type2,
          .channel_name = channel_name2,
          .message_encoding = message_encoding2,
          .channel_type = channel_type2,
          .schema_name = schema_name2,
          .schema_encoding = schema_encoding2,
          .schema_definition = schema_definition2,
        }) == 2U);
    REQUIRE(metadata_writer.get_channel_id("channel2") == 2U);
    REQUIRE(metadata_writer.get_compression_type(2U) == compression_type2);

    REQUIRE(file_writer.open());
    const auto write_result = metadata_writer.write_chunk(compressor, file_writer);
    REQUIRE(write_result);
    REQUIRE(file_writer.close());
    REQUIRE(file_reader.open());

    const auto read_result = read_metadata_chunk(memory_resource, write_result.value(), file_reader, compressor);
    REQUIRE(read_result);
    const auto& metadata_map = read_result.value();
    REQUIRE(metadata_map.size() == 2U);
    REQUIRE(metadata_map.contains(1U));
    REQUIRE(metadata_map.at(1U).channel_name == channel_name1);
    REQUIRE(metadata_map.at(1U).compression_type == compression_type1);
    REQUIRE(metadata_map.at(1U).message_encoding == message_encoding1);
    REQUIRE(metadata_map.at(1U).channel_type == channel_type1);
    REQUIRE(metadata_map.at(1U).schema_name == schema_name1);
    REQUIRE(metadata_map.at(1U).schema_encoding == schema_encoding1);
    REQUIRE(metadata_map.at(1U).schema_definition == schema_definition1);
    REQUIRE(metadata_map.contains(2U));
    REQUIRE(metadata_map.at(2U).channel_name == channel_name2);
    REQUIRE(metadata_map.at(2U).compression_type == compression_type2);
    REQUIRE(metadata_map.at(2U).message_encoding == message_encoding2);
    REQUIRE(metadata_map.at(2U).channel_type == channel_type2);
    REQUIRE(metadata_map.at(2U).schema_name == schema_name2);
    REQUIRE(metadata_map.at(2U).schema_encoding == schema_encoding2);
    REQUIRE(metadata_map.at(2U).schema_definition == schema_definition2);
  }

  SECTION("Error handling")
  {
    SECTION("Duplicate channel")
    {
      REQUIRE(
        metadata_writer.add_channel(
          reader::LoggedChannelInfo{
            .compression_type = compression_type1,
            .channel_name = channel_name1,
            .message_encoding = message_encoding1,
            .channel_type = channel_type1,
            .schema_name = schema_name1,
            .schema_encoding = schema_encoding1,
            .schema_definition = schema_definition1,
          }) == 1U);
      REQUIRE(
        metadata_writer.add_channel(
          reader::LoggedChannelInfo{
            .compression_type = compression_type1,
            .channel_name = channel_name1,
            .message_encoding = message_encoding1,
            .channel_type = channel_type1,
            .schema_name = schema_name1,
            .schema_encoding = schema_encoding1,
            .schema_definition = schema_definition1,
          }) == jewels::unexpected(LogError::channel_already_exists));
    }

    SECTION("Unknown channel")
    {
      REQUIRE(metadata_writer.get_channel_id("channel1") == jewels::unexpected(LogError::unknown_channel));
      REQUIRE(metadata_writer.get_compression_type(1U) == jewels::unexpected(LogError::unknown_channel));
    }

    SECTION("Channel name too long")
    {
      REQUIRE(
        metadata_writer.add_channel(
          reader::LoggedChannelInfo{
            .compression_type = compression_type1,
            .channel_name = std::pmr::string{max_name_string_size + 1U, 'X', memory_resource},
            .message_encoding = message_encoding1,
            .channel_type = channel_type1,
            .schema_name = schema_name1,
            .schema_encoding = schema_encoding1,
            .schema_definition = schema_definition1,
          }) == jewels::unexpected(LogError::channel_name_exceeds_max_name_size));
    }

    SECTION("Schema name too long")
    {
      REQUIRE(
        metadata_writer.add_channel(
          reader::LoggedChannelInfo{
            .compression_type = compression_type1,
            .channel_name = channel_name1,
            .message_encoding = message_encoding1,
            .channel_type = channel_type1,
            .schema_name = std::pmr::string{max_name_string_size + 1U, 'X', memory_resource},
            .schema_encoding = schema_encoding1,
            .schema_definition = schema_definition1,
          }) == jewels::unexpected(LogError::schema_name_exceeds_max_name_size));
    }

    SECTION("Schema definition too long")
    {
      REQUIRE(
        metadata_writer.add_channel(
          reader::LoggedChannelInfo{
            .compression_type = compression_type1,
            .channel_name = channel_name1,
            .message_encoding = message_encoding1,
            .channel_type = channel_type1,
            .schema_name = schema_name1,
            .schema_encoding = schema_encoding1,
            .schema_definition = std::pmr::string{max_schema_definition_string_size + 1U, 'X', memory_resource},
          }) == jewels::unexpected(LogError::schema_definition_exceeds_max_size));
    }

    SECTION("Writer not open")
    {
      REQUIRE(metadata_writer.write_chunk(compressor, file_writer) == jewels::unexpected(LogError::not_open));
    }

    SECTION("Write fails")
    {
      REQUIRE(file_writer.open());
      file_writer.filesystem().inject_write_error(EIO);
      REQUIRE(metadata_writer.write_chunk(compressor, file_writer) == jewels::unexpected(LogError::io_error));
    }

    SECTION("Reader not open")
    {
      REQUIRE(file_writer.open());
      const auto write_result = metadata_writer.write_chunk(compressor, file_writer);
      REQUIRE(write_result);
      REQUIRE(file_writer.close());
      REQUIRE(
        read_metadata_chunk(memory_resource, write_result.value(), file_reader, compressor) ==
        jewels::unexpected(LogError::not_open));
    }

    SECTION("Read fails")
    {
      REQUIRE(file_writer.open());
      const auto write_result = metadata_writer.write_chunk(compressor, file_writer);
      REQUIRE(write_result);
      REQUIRE(file_writer.close());
      REQUIRE(file_reader.open());
      file_reader.filesystem().inject_read_error(EIO);
      REQUIRE(
        read_metadata_chunk(memory_resource, write_result.value(), file_reader, compressor) ==
        jewels::unexpected(LogError::io_error));
    }

    SECTION("Decompression fails")
    {
      REQUIRE(file_writer.open());
      const auto write_result = metadata_writer.write_chunk(compressor, file_writer);
      REQUIRE(write_result);
      REQUIRE(file_writer.close());
      REQUIRE(onboard::tests::corrupt_log_file(test_file_path.string(), 10U, "XXX"));
      REQUIRE(file_reader.open());
      REQUIRE(
        read_metadata_chunk(memory_resource, write_result.value(), file_reader, compressor) ==
        jewels::unexpected(LogError::decompression_failure));
    }
  }
}

} // namespace
} // namespace clockwork_logging::offboard
