// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "clockwork/logging/tools/mcap_converter/mcap_channel_registry.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_predicate.hpp>
#include <catch2/matchers/catch_matchers_quantifiers.hpp>
#include <mcap/errors.hpp>
#include <mcap/reader.hpp>
#include <mcap/types.hpp>
#include <mcap/writer.hpp>
#include <wise_enum.h>

#include <cstring>
#include <filesystem>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace clockwork_logging
{
namespace
{

TEST_CASE("McapChannelRegistry")
{
  using Catch::Matchers::AnyMatch;
  using Catch::Matchers::Predicate;

  const jewels::testing::TmpDirectoryGuard test_dir;
  auto mcap_log_path = (test_dir.get_path() / "test.mcap").string();

  const onboard::LoggedChannelMetadata metadata1{
    .channel_name = "channel1",
    .message_encoding = MessageEncoding::tachyon,
    .schema_name = "schema1",
    .schema_encoding = SchemaEncoding::clockwork_tachyon,
    .schema_definition = "string data",
  };
  const onboard::LoggedChannelMetadata metadata2{
    .channel_name = "channel2",
    .message_encoding = MessageEncoding::undefined,
    .schema_name = "schema2",
    .schema_encoding = SchemaEncoding::unspecified,
    .schema_definition = "int32 data",
  };
  const onboard::LoggedChannelMetadata metadata3{
    .channel_name = "channel3",
    .message_encoding = MessageEncoding::unspecified,
    .schema_name = "schema3",
    .schema_encoding = SchemaEncoding::undefined,
    .schema_definition = "float data",
  };

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  const auto writer_options = mcap::McapWriterOptions("");

  auto mcap_writer = mcap::McapWriter();
  auto status = mcap_writer.open(mcap_log_path, writer_options);
  REQUIRE(status.ok());

  const auto registry = std::make_unique<McapChannelRegistry>();
  REQUIRE_FALSE(registry->try_get_channel_id(metadata1.channel_name));
  const auto channel_id1 = registry->register_channel(mcap_writer, metadata1);
  REQUIRE_FALSE(registry->try_get_channel_id(metadata2.channel_name));
  const auto channel_id2 = registry->register_channel(mcap_writer, metadata2);
  REQUIRE_FALSE(registry->try_get_channel_id(metadata3.channel_name));
  const auto channel_id3 = registry->register_channel(mcap_writer, metadata3);
  REQUIRE(registry->register_channel(mcap_writer, metadata1) == channel_id1);
  REQUIRE(registry->register_channel(mcap_writer, metadata2) == channel_id2);
  REQUIRE(registry->register_channel(mcap_writer, metadata3) == channel_id3);
  REQUIRE(registry->try_get_channel_id(metadata1.channel_name));
  REQUIRE(registry->try_get_channel_id(metadata1.channel_name).value() == channel_id1);
  REQUIRE(registry->try_get_channel_id(metadata2.channel_name).value() == channel_id2);
  REQUIRE(registry->try_get_channel_id(metadata3.channel_name).value() == channel_id3);
  mcap_writer.close();

  auto mcap_reader = mcap::McapReader();
  status = mcap_reader.open(mcap_log_path);
  REQUIRE(status.ok());

  status = mcap_reader.readSummary(mcap::ReadSummaryMethod::AllowFallbackScan);
  REQUIRE(status.ok());

  /// Log contains the expected channels and schemas

  const auto& schemas = mcap_reader.schemas();
  REQUIRE(3 == schemas.size());
  const auto& channels = mcap_reader.channels();
  REQUIRE(3 == channels.size());

  using ChannelPair = std::unordered_map<mcap::ChannelId, mcap::ChannelPtr>::value_type;
  REQUIRE_THAT(
    channels,
    AnyMatch(
      Predicate<ChannelPair>(
        [&metadata1, &schemas](const auto& channel_pair)
        {
          const auto& channel = *channel_pair.second;
          const auto& schema = *schemas.at(channel.schemaId);
          return channel.topic == metadata1.channel_name &&
                 channel.messageEncoding == wise_enum::to_string(metadata1.message_encoding) &&
                 schema.name == metadata1.schema_name &&
                 schema.encoding == wise_enum::to_string(metadata1.schema_encoding) &&
                 schema.data.size() == metadata1.schema_definition.size() &&
                 std::memcmp(
                   schema.data.data(),
                   std::span{metadata1.schema_definition.data(), metadata1.schema_definition.size()}.data(),
                   schema.data.size()) == 0;
        })));
  REQUIRE_THAT(
    channels,
    AnyMatch(
      Predicate<ChannelPair>(
        [&metadata2, &schemas](const auto& channel_pair)
        {
          const auto& channel = *channel_pair.second;
          const auto& schema = *schemas.at(channel.schemaId);
          return channel.topic == metadata2.channel_name &&
                 channel.messageEncoding == wise_enum::to_string(metadata2.message_encoding) &&
                 schema.name == metadata2.schema_name &&
                 schema.encoding == wise_enum::to_string(metadata2.schema_encoding) &&
                 schema.data.size() == metadata2.schema_definition.size() &&
                 std::memcmp(
                   schema.data.data(),
                   std::span{metadata2.schema_definition.data(), metadata2.schema_definition.size()}.data(),
                   schema.data.size()) == 0;
        })));
  REQUIRE_THAT(
    channels,
    AnyMatch(
      Predicate<ChannelPair>(
        [&metadata3, &schemas](const auto& channel_pair)
        {
          const auto& channel = *channel_pair.second;
          const auto& schema = *schemas.at(channel.schemaId);
          return channel.topic == metadata3.channel_name &&
                 channel.messageEncoding == wise_enum::to_string(metadata3.message_encoding) &&
                 schema.name == metadata3.schema_name &&
                 schema.encoding == wise_enum::to_string(metadata3.schema_encoding) &&
                 schema.data.size() == metadata3.schema_definition.size() &&
                 std::memcmp(
                   schema.data.data(),
                   std::span{metadata3.schema_definition.data(), metadata3.schema_definition.size()}.data(),
                   schema.data.size()) == 0;
        })));
}

} // namespace
} // namespace clockwork_logging
