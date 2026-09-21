// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_publisher_config_clk_cc.hh"
#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/onboard/clockwork_writer_policy.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/serialization.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/realtime_playback/converter_request.hh"
#include "clockwork/logging/realtime_playback/converter_setup.hh"
#include "clockwork/logging/realtime_playback/initialization_converter.hh"
#include "clockwork/logging/realtime_playback/realtime_playback_conversion_config_clk_cc.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/tests/support/test_message_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/py/tests/support/simple_schema_v1_clk_cc.hh"
#include "clockwork/serialization/py/tests/support/simple_schema_v2_clk_cc.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging::realtime_playback
{
namespace
{

using TestMessage = clockwork::Tappy<tests::TestMessage>;
using UpgradeMessageV1 = clockwork::Tappy<clockwork::tests::SimpleSchemaV1>;
using UpgradeMessageV2 = clockwork::Tappy<clockwork::tests::SimpleSchemaV2>;
using TestWriter = onboard::Writer<onboard::ClockworkWriterPolicy<>>;

[[nodiscard]] std::string schema_definition()
{
  const auto& definition = clockwork::LoggingTraits<TestMessage>::schema_definition;
  return {definition.data(), definition.size()};
}

void add_source_channel(TestWriter& writer, std::string_view channel_name)
{
  REQUIRE(writer.add_channel(
    onboard::LoggedChannelMetadata{
      .channel_name = channel_name,
      .compression_type = CompressionType::none,
      .message_encoding = MessageEncoding::tachyon,
      .channel_type = ChannelType::regular,
      .schema_name = clockwork::LoggingTraits<TestMessage>::schema_name,
      .schema_encoding = SchemaEncoding::clockwork_tachyon,
      .schema_definition = schema_definition(),
    },
    jewels::time::SteadyClock::now()));
}

void write_source(const std::string& path)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  TestWriter writer{
    memory_resource, memory_resource, 1024U, std::chrono::nanoseconds{0}, onboard::WriterEnvironment::simulation};
  REQUIRE(writer.open_log(path, "source", jewels::time::SteadyClock::now()));
  add_source_channel(writer, "/source");
  add_source_channel(writer, "/quiet");

  constexpr std::array header{std::byte{0x12}, std::byte{0x34}};
  TestMessage message;
  message.get_underlying_message_string().set_truncate("winner");
  REQUIRE(writer.log_message_wait(
    onboard::Message{
      .channel_name = "/source",
      .sequence_number = 8U,
      .log_time = LogTimestamp{21},
      .message_time = LogTimestamp{20},
      .header = header,
      .data = std::as_bytes(std::span{&message, 1}),
    },
    false,
    jewels::time::SteadyClock::now()));
  REQUIRE(writer.close_log(jewels::time::SteadyClock::now()));
  REQUIRE(writer.drain_async_operations());
}

void write_upgrade_source(const std::string& path, const bool malformed_payload = false)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  TestWriter writer{
    memory_resource, memory_resource, 1024U, std::chrono::nanoseconds{0}, onboard::WriterEnvironment::simulation};
  REQUIRE(writer.open_log(path, "source", jewels::time::SteadyClock::now()));
  const auto& definition = clockwork::LoggingTraits<UpgradeMessageV1>::schema_definition;
  REQUIRE(writer.add_channel(
    onboard::LoggedChannelMetadata{
      .channel_name = "/source",
      .compression_type = CompressionType::none,
      .message_encoding = MessageEncoding::tachyon,
      .channel_type = ChannelType::regular,
      .schema_name = clockwork::LoggingTraits<UpgradeMessageV1>::schema_name,
      .schema_encoding = SchemaEncoding::clockwork_tachyon,
      .schema_definition = std::string{definition.data(), definition.size()},
    },
    jewels::time::SteadyClock::now()));
  UpgradeMessageV1 message;
  message.set_integer_field(42);
  constexpr std::array malformed_data{std::byte{0x42}};
  const auto data =
    malformed_payload ? std::span<const std::byte>{malformed_data} : std::as_bytes(std::span{&message, 1U});
  REQUIRE(writer.log_message_wait(
    onboard::Message{
      .channel_name = "/source",
      .sequence_number = 1U,
      .log_time = LogTimestamp{11},
      .message_time = LogTimestamp{10},
      .header = {},
      .data = data,
    },
    false,
    jewels::time::SteadyClock::now()));
  REQUIRE(writer.close_log(jewels::time::SteadyClock::now()));
  REQUIRE(writer.drain_async_operations());
}

[[nodiscard]] ProcessUuid make_process_uuid(std::string_view value)
{
  const auto uuid = ProcessUuid::from_string(value);
  REQUIRE(uuid);
  return *uuid;
}

[[nodiscard]] std::shared_ptr<ConversionConfig> make_config()
{
  auto config = std::make_shared<ConversionConfig>();
  const auto schema = schema_definition();
  for (const auto channel_name : {std::string_view{"/source"}, std::string_view{"/quiet"}})
  {
    auto& publisher = config->get_mutable_publishers().get_underlying_channels().emplace_back();
    REQUIRE(publisher.try_set_channel_name(channel_name));
    REQUIRE(publisher.try_set_class_name(clockwork::LoggingTraits<TestMessage>::class_name));
    REQUIRE(publisher.try_set_schema_definition(std::as_bytes(std::span{schema})));
    publisher.set_message_size(sizeof(TestMessage));
  }

  auto& domain = config->get_underlying_cpu_domains().emplace_back();
  REQUIRE(domain.try_set_cpu_domain_name("domain"));
  REQUIRE(domain.try_set_simplelaunch_node_name("c1"));
  return config;
}

[[nodiscard]] std::shared_ptr<ConversionConfig> make_upgrade_config()
{
  auto config = std::make_shared<ConversionConfig>();
  auto& publisher = config->get_mutable_publishers().get_underlying_channels().emplace_back();
  REQUIRE(publisher.try_set_channel_name("/source"));
  REQUIRE(publisher.try_set_class_name(clockwork::LoggingTraits<UpgradeMessageV2>::class_name));
  const auto& definition = clockwork::LoggingTraits<UpgradeMessageV2>::schema_definition;
  REQUIRE(publisher.try_set_schema_definition(std::as_bytes(std::span{definition})));
  publisher.set_message_size(sizeof(UpgradeMessageV2));
  auto& domain = config->get_underlying_cpu_domains().emplace_back();
  REQUIRE(domain.try_set_cpu_domain_name("domain"));
  REQUIRE(domain.try_set_simplelaunch_node_name("c1"));
  return config;
}

void add_requirement(
  ConversionConfig& config,
  ProcessUuid process_uuid,
  std::string_view source_channel_name,
  const bool allow_missing = false)
{
  auto& requirement = config.get_underlying_initialization_requirements().emplace_back();
  requirement.set_process_uuid(process_uuid);
  REQUIRE(requirement.try_set_source_channel_name(source_channel_name));
  REQUIRE(requirement.try_set_cpu_domain_name("domain"));
  requirement.set_allow_missing(allow_missing);
}

[[nodiscard]] ConverterRequest make_request(const std::string& source, const std::string& output)
{
  return ConverterRequest{
    .source_uri = source,
    .generated_config_path = {},
    .output_path = output,
    .start_time_ns = {},
    .end_time_ns = {},
  };
}

jewels::BinaryOutcome convert_for_test(
  jewels::Out<InitializationConversionResult> result_out,
  const ConverterRequest& request,
  const std::shared_ptr<const ConversionConfig>& config)
{
  ConverterContext context;
  if (jewels::fails(prepare_converter_context(jewels::Out{context}, request, *config)))
  {
    return jewels::failure;
  }
  return convert_initialization_logs(jewels::Out{*result_out}, request, context);
}

void check_raw_initialization_log(const std::string& path)
{
  auto raw_reader = make_reader(path, {}, {}, DecompressOption::dont_decompress);
  REQUIRE(raw_reader->open({}));
  const auto raw_message = raw_reader->next_message();
  REQUIRE(raw_message);
  CHECK(raw_message->is_lite_compressed);
  REQUIRE(raw_reader->close());
}

void check_initialization_log_metadata(AbstractLogReader& reader)
{
  const auto metadata_entries = reader.get_metadata();
  REQUIRE(metadata_entries.size() == 1U);
  const auto& metadata = metadata_entries.front();
  CHECK(metadata.name == "/source");
  CHECK(metadata.type == clockwork::LoggingTraits<TestMessage>::schema_name);
  CHECK(metadata.message_encoding == MessageEncoding::tachyon);
  CHECK(metadata.channel_type == ChannelType::regular);
  CHECK(metadata.schema_encoding == SchemaEncoding::clockwork_tachyon);
  CHECK(metadata.schema_definition == schema_definition());
}

void check_initialization_log_message(AbstractLogReader& reader)
{
  const auto message = reader.next_message();
  REQUIRE(message);
  CHECK(message->topic == "/source");
  CHECK(message->sequence_number == 8U);
  CHECK(message->publish_time == LogTimestamp{20});
  CHECK(message->log_time == LogTimestamp{21});
  CHECK(std::ranges::equal(message->header, std::array{std::byte{0x12}, std::byte{0x34}}));
  TestMessage decoded;
  deserialize_tachyon(decoded, message->data);
  CHECK(decoded.get_message_string() == "winner");
  CHECK_FALSE(reader.next_message());
}

void check_initialization_log(const std::string& path)
{
  check_raw_initialization_log(path);
  auto reader = make_reader(path, {}, {});
  REQUIRE(reader->open({}));
  check_initialization_log_metadata(*reader);
  check_initialization_log_message(*reader);
  REQUIRE(reader->close());
}

TEST_CASE("Initialization conversion requires a value when no fallback is configured")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const auto source = temp_dir.get_path() / "source";
  const auto output = temp_dir.get_path() / "output";
  const std::string source_string{source.c_str()};
  const std::string output_string{output.c_str()};
  write_source(source_string);
  const auto process_uuid = make_process_uuid("11111111-1111-4111-8111-111111111111");
  auto config = make_config();
  add_requirement(*config, process_uuid, "/quiet");

  InitializationConversionResult result;
  REQUIRE(jewels::fails(convert_for_test(jewels::Out{result}, make_request(source_string, output_string), config)));
}

TEST_CASE("Initialization conversion permits a fallback without a synthetic log")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const auto source = temp_dir.get_path() / "source";
  const auto output = temp_dir.get_path() / "output";
  const std::string source_string{source.c_str()};
  const std::string output_string{output.c_str()};
  write_source(source_string);
  const auto process_uuid = make_process_uuid("22222222-2222-4222-8222-222222222222");

  auto config = make_config();
  add_requirement(*config, process_uuid, "/quiet", true);
  InitializationConversionResult result;
  REQUIRE(jewels::ok(convert_for_test(jewels::Out{result}, make_request(source_string, output_string), config)));
  CHECK(result.initialization_logs.empty());
}

TEST_CASE("Initialization conversion writes one log per process")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const auto source = temp_dir.get_path() / "source";
  const auto output = temp_dir.get_path() / "output";
  const std::string source_string{source.c_str()};
  const std::string output_string{output.c_str()};
  write_source(source_string);
  const std::array process_uuids{
    make_process_uuid("33333333-3333-4333-8333-333333333333"),
    make_process_uuid("44444444-4444-4444-8444-444444444444")};
  auto config = make_config();
  for (const auto process_uuid : process_uuids)
  {
    add_requirement(*config, process_uuid, "/source");
  }

  InitializationConversionResult result;
  REQUIRE(jewels::ok(convert_for_test(jewels::Out{result}, make_request(source_string, output_string), config)));
  REQUIRE(result.initialization_logs.size() == process_uuids.size());
  CHECK(result.initialization_logs.at(0).simplelaunch_node_name == "c1");
  CHECK(result.initialization_logs.at(1).simplelaunch_node_name == "c1");
  CHECK(result.initialization_logs.at(0).relative_path != result.initialization_logs.at(1).relative_path);
  for (const auto& log : result.initialization_logs)
  {
    const auto log_path = (output / log.relative_path).string();
    check_initialization_log(std::string{log_path});
  }
}

TEST_CASE("Initialization conversion upgrades a logged value to the target schema")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const auto source = temp_dir.get_path() / "source";
  const auto output = temp_dir.get_path() / "output";
  const std::string source_string{source.c_str()};
  const std::string output_string{output.c_str()};
  write_upgrade_source(source_string);
  const auto process_uuid = make_process_uuid("55555555-5555-4555-8555-555555555555");
  auto config = make_upgrade_config();
  add_requirement(*config, process_uuid, "/source");

  InitializationConversionResult result;
  REQUIRE(jewels::ok(convert_for_test(jewels::Out{result}, make_request(source_string, output_string), config)));
  REQUIRE(result.initialization_logs.size() == 1U);
  auto reader = make_reader(
    (std::filesystem::path{output_string} / result.initialization_logs.front().relative_path).string(), {}, {});
  REQUIRE(reader->open({}));
  const auto metadata = reader->get_metadata();
  REQUIRE(metadata.size() == 1U);
  CHECK(metadata.front().type == clockwork::LoggingTraits<UpgradeMessageV2>::schema_name);
  const auto logged_message = reader->next_message();
  REQUIRE(logged_message);
  UpgradeMessageV2 decoded;
  deserialize_tachyon(decoded, logged_message->data);
  CHECK(decoded.get_integer_field() == 42);
  CHECK(decoded.get_string_field().empty());
  CHECK_FALSE(reader->next_message());
  REQUIRE(reader->close());
}

TEST_CASE("Initialization conversion reports a malformed source payload as failure")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const std::string source{(temp_dir.get_path() / "source").c_str()};
  const std::string output{(temp_dir.get_path() / "output").c_str()};
  write_upgrade_source(source, true);
  const auto process_uuid = make_process_uuid("55555555-5555-4555-8555-555555555555");
  auto config = make_upgrade_config();
  add_requirement(*config, process_uuid, "/source");

  InitializationConversionResult result;
  CHECK(jewels::fails(convert_for_test(jewels::Out{result}, make_request(source, output), config)));
}

} // namespace
} // namespace clockwork_logging::realtime_playback
