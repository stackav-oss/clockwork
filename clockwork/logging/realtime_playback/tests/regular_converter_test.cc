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
#include "clockwork/logging/realtime_playback/realtime_playback_conversion_config_clk_cc.hh"
#include "clockwork/logging/realtime_playback/realtime_playback_stream_kind_clk_cc.hh"
#include "clockwork/logging/realtime_playback/regular_converter.hh"
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
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::realtime_playback
{
namespace
{

using TestMessage = clockwork::Tappy<tests::TestMessage>;
using UpgradeMessageV1 = clockwork::Tappy<clockwork::tests::SimpleSchemaV1>;
using UpgradeMessageV2 = clockwork::Tappy<clockwork::tests::SimpleSchemaV2>;
using TestWriter = onboard::Writer<onboard::ClockworkWriterPolicy<>>;

constexpr std::size_t writer_buffer_size = 1024U;
constexpr auto upgrade_integer_field_value = 42;

[[nodiscard]] std::string schema_definition()
{
  const auto& definition = clockwork::LoggingTraits<TestMessage>::schema_definition;
  return {definition.data(), definition.size()};
}

void write_source(const std::string& path)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  TestWriter writer{
    memory_resource,
    memory_resource,
    writer_buffer_size,
    std::chrono::nanoseconds{0},
    onboard::WriterEnvironment::simulation};
  REQUIRE(writer.open_log(path, "source", jewels::time::SteadyClock::now()));
  const auto schema = schema_definition();
  REQUIRE(writer.add_channel(
    onboard::LoggedChannelMetadata{
      .channel_name = "/source",
      .compression_type = CompressionType::none,
      .message_encoding = MessageEncoding::tachyon,
      .channel_type = ChannelType::regular,
      .schema_name = clockwork::LoggingTraits<TestMessage>::schema_name,
      .schema_encoding = SchemaEncoding::clockwork_tachyon,
      .schema_definition = schema,
    },
    jewels::time::SteadyClock::now()));
  constexpr std::array header{std::byte{0x12}, std::byte{0x34}};
  for (const auto index : std::ranges::views::iota(std::int64_t{1}, std::int64_t{4}))
  {
    TestMessage message;
    message.get_underlying_message_string().set_truncate(std::to_string(index));
    REQUIRE(writer.log_message_wait(
      onboard::Message{
        .channel_name = "/source",
        .sequence_number = static_cast<std::uint32_t>(index),
        .log_time = LogTimestamp{(index * 10) + 1},
        .message_time = LogTimestamp{index * 10},
        .header = header,
        .data = std::as_bytes(std::span{&message, 1}),
      },
      false,
      jewels::time::SteadyClock::now()));
  }
  REQUIRE(writer.close_log(jewels::time::SteadyClock::now()));
  REQUIRE(writer.drain_async_operations());
}

void write_upgrade_source(const std::string& path, const bool malformed_payload = false)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  TestWriter writer{
    memory_resource,
    memory_resource,
    writer_buffer_size,
    std::chrono::nanoseconds{0},
    onboard::WriterEnvironment::simulation};
  REQUIRE(writer.open_log(path, "source", jewels::time::SteadyClock::now()));
  const auto& definition = clockwork::LoggingTraits<UpgradeMessageV1>::schema_definition;
  const std::string schema{definition.data(), definition.size()};
  REQUIRE(writer.add_channel(
    onboard::LoggedChannelMetadata{
      .channel_name = "/source",
      .compression_type = CompressionType::none,
      .message_encoding = MessageEncoding::tachyon,
      .channel_type = ChannelType::regular,
      .schema_name = clockwork::LoggingTraits<UpgradeMessageV1>::schema_name,
      .schema_encoding = SchemaEncoding::clockwork_tachyon,
      .schema_definition = schema,
    },
    jewels::time::SteadyClock::now()));
  UpgradeMessageV1 message;
  message.set_integer_field(upgrade_integer_field_value);
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

[[nodiscard]] std::shared_ptr<ConversionConfig> make_config()
{
  auto config = std::make_shared<ConversionConfig>();
  const auto schema = schema_definition();
  constexpr std::array targets{
    std::array<std::string_view, 3>{"/target", "domain", "c1"},
    std::array<std::string_view, 3>{"/target_two", "domain_two", "c2"},
  };
  for (const auto& target : targets)
  {
    auto& publisher = config->get_mutable_publishers().get_underlying_channels().emplace_back();
    REQUIRE(publisher.try_set_channel_name(target.at(0)));
    REQUIRE(publisher.try_set_class_name(clockwork::LoggingTraits<TestMessage>::class_name));
    REQUIRE(publisher.try_set_schema_definition(std::as_bytes(std::span{schema})));
    publisher.set_message_size(sizeof(TestMessage));
    auto& domain = config->get_underlying_cpu_domains().emplace_back();
    REQUIRE(domain.try_set_cpu_domain_name(target.at(1)));
    REQUIRE(domain.try_set_simplelaunch_node_name(target.at(2)));
    auto& assignment = domain.get_underlying_assignments().emplace_back();
    REQUIRE(assignment.try_set_source_channel_name("/source"));
    REQUIRE(assignment.try_set_destination_channel_name(target.at(0)));
    assignment.set_stream_kind(RealtimePlaybackStreamKind::regular);
  }
  return config;
}

[[nodiscard]] std::shared_ptr<ConversionConfig> make_upgrade_config()
{
  auto config = std::make_shared<ConversionConfig>();
  const auto add_target = [&config]<typename MessageType>(const std::string_view channel_name)
  {
    const auto& definition = clockwork::LoggingTraits<MessageType>::schema_definition;
    auto& publisher = config->get_mutable_publishers().get_underlying_channels().emplace_back();
    REQUIRE(publisher.try_set_channel_name(channel_name));
    REQUIRE(publisher.try_set_class_name(clockwork::LoggingTraits<MessageType>::class_name));
    REQUIRE(publisher.try_set_schema_definition(std::as_bytes(std::span{definition})));
    publisher.set_message_size(sizeof(MessageType));
    auto& domain = config->get_underlying_cpu_domains().emplace_back();
    REQUIRE(domain.try_set_cpu_domain_name(channel_name));
    REQUIRE(domain.try_set_simplelaunch_node_name("shared"));
    auto& assignment = domain.get_underlying_assignments().emplace_back();
    REQUIRE(assignment.try_set_source_channel_name("/source"));
    REQUIRE(assignment.try_set_destination_channel_name(channel_name));
    assignment.set_stream_kind(RealtimePlaybackStreamKind::regular);
  };
  add_target.template operator()<UpgradeMessageV1>("/v1");
  add_target.template operator()<UpgradeMessageV2>("/v2");
  return config;
}

jewels::BinaryOutcome convert_for_test(
  jewels::Out<RegularConversionResult> result_out,
  const ConverterRequest& request,
  const std::shared_ptr<const ConversionConfig>& config)
{
  ConverterContext context;
  if (jewels::fails(prepare_converter_context(jewels::Out{context}, request, *config)))
  {
    return jewels::failure;
  }
  return convert_regular_logs(jewels::Out{*result_out}, request, context);
}

TEST_CASE("Regular conversion preserves selected message metadata and LiteCompresses target data")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const auto source = temp_dir.get_path() / "source";
  const auto output = temp_dir.get_path() / "output";
  const std::string source_string{source.c_str()};
  const std::string output_string{output.c_str()};
  write_source(source_string);
  const ConverterRequest request{
    .source_uri = source_string,
    .generated_config_path = {},
    .output_path = output_string,
    .start_time_ns = 20,
    .end_time_ns = 30,
  };
  RegularConversionResult result;
  REQUIRE(jewels::ok(convert_for_test(jewels::Out{result}, request, make_config())));
  REQUIRE(result.regular_logs.size() == 2U);
  constexpr std::array expected{
    std::array<std::string_view, 3>{"c1", "nodes/c1/regular", "/target"},
    std::array<std::string_view, 3>{"c2", "nodes/c2/regular", "/target_two"},
  };
  for (const auto log_index : std::ranges::views::iota(std::size_t{0}, expected.size()))
  {
    const auto& log_result = result.regular_logs.at(log_index);
    CHECK(log_result.simplelaunch_node_name == expected.at(log_index).at(0));
    CHECK(log_result.relative_path == expected.at(log_index).at(1));
    const auto log_path = (output / log_result.relative_path).string();
    auto raw_reader = make_reader(log_path, {}, {}, DecompressOption::dont_decompress);
    REQUIRE(raw_reader->open({}));
    const auto raw_message = raw_reader->next_message();
    REQUIRE(raw_message);
    CHECK(raw_message->is_lite_compressed);
    REQUIRE(raw_reader->close());

    auto reader = make_reader(log_path, {}, {});
    REQUIRE(reader->open({}));
    const auto metadata_entries = reader->get_metadata();
    REQUIRE(metadata_entries.size() == 1U);
    const auto& metadata = metadata_entries.front();
    CHECK(metadata.name == expected.at(log_index).at(2));
    CHECK(metadata.type == clockwork::LoggingTraits<TestMessage>::schema_name);
    CHECK(metadata.message_encoding == MessageEncoding::tachyon);
    CHECK(metadata.channel_type == ChannelType::regular);
    CHECK(metadata.schema_encoding == SchemaEncoding::clockwork_tachyon);
    CHECK(metadata.schema_definition == schema_definition());
    std::size_t message_count{};
    while (const auto message = reader->next_message())
    {
      CHECK(message->topic == expected.at(log_index).at(2));
      CHECK(message->sequence_number == message_count + 2U);
      CHECK(message->publish_time == LogTimestamp{static_cast<std::int64_t>((message_count + 2U) * 10U)});
      CHECK(message->log_time == LogTimestamp{static_cast<std::int64_t>(((message_count + 2U) * 10U) + 1U)});
      CHECK(std::ranges::equal(message->header, std::array{std::byte{0x12}, std::byte{0x34}}));
      TestMessage decoded;
      deserialize_tachyon(decoded, message->data);
      CHECK(decoded.get_message_string() == std::to_string(message_count + 2U));
      ++message_count;
    }
    CHECK(message_count == 2U);
    REQUIRE(reader->close());
  }
}

TEST_CASE("Regular conversion groups channels assigned to the same SimpleLaunch node")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const std::string source{(temp_dir.get_path() / "source").c_str()};
  const std::string output{(temp_dir.get_path() / "output").c_str()};
  write_source(source);
  auto config = make_config();
  REQUIRE(config->get_underlying_cpu_domains().at(1).try_set_simplelaunch_node_name("c1"));
  RegularConversionResult result;
  REQUIRE(
    jewels::ok(convert_for_test(
      jewels::Out{result},
      ConverterRequest{
        .source_uri = source,
        .generated_config_path = {},
        .output_path = output,
        .start_time_ns = {},
        .end_time_ns = {},
      },
      std::move(config))));
  REQUIRE(result.regular_logs.size() == 1U);
  auto reader =
    make_reader((std::filesystem::path{output} / result.regular_logs.front().relative_path).string(), {}, {});
  REQUIRE(reader->open({}));
  CHECK(reader->get_metadata().size() == 2U);
  std::size_t message_count{};
  while (reader->next_message())
  {
    ++message_count;
  }
  CHECK(message_count == 6U);
  REQUIRE(reader->close());
}

TEST_CASE("Regular conversion upgrades one source schema to different target schemas")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const std::string source{(temp_dir.get_path() / "source").c_str()};
  const std::string output{(temp_dir.get_path() / "output").c_str()};
  write_upgrade_source(source);
  RegularConversionResult result;
  REQUIRE(
    jewels::ok(convert_for_test(
      jewels::Out{result},
      ConverterRequest{
        .source_uri = source,
        .generated_config_path = {},
        .output_path = output,
        .start_time_ns = {},
        .end_time_ns = {},
      },
      make_upgrade_config())));
  REQUIRE(result.regular_logs.size() == 1U);
  auto reader =
    make_reader((std::filesystem::path{output} / result.regular_logs.front().relative_path).string(), {}, {});
  REQUIRE(reader->open({}));
  REQUIRE(reader->get_metadata().size() == 2U);
  const auto first = reader->next_message();
  REQUIRE(first);
  const auto second = reader->next_message();
  REQUIRE(second);
  for (const auto* message : {&*first, &*second})
  {
    if (message->topic == "/v1")
    {
      UpgradeMessageV1 decoded;
      deserialize_tachyon(decoded, message->data);
      CHECK(decoded.get_integer_field() == upgrade_integer_field_value);
    }
    else
    {
      REQUIRE(message->topic == "/v2");
      UpgradeMessageV2 decoded;
      deserialize_tachyon(decoded, message->data);
      CHECK(decoded.get_integer_field() == upgrade_integer_field_value);
      CHECK(decoded.get_string_field().empty());
    }
  }
  CHECK_FALSE(reader->next_message());
  REQUIRE(reader->close());
}

TEST_CASE("Regular conversion reports a malformed source payload as failure")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const std::string source{(temp_dir.get_path() / "source").c_str()};
  const std::string output{(temp_dir.get_path() / "output").c_str()};
  write_upgrade_source(source, true);
  RegularConversionResult result;
  CHECK(
    jewels::fails(convert_for_test(
      jewels::Out{result},
      ConverterRequest{
        .source_uri = source,
        .generated_config_path = {},
        .output_path = output,
        .start_time_ns = {},
        .end_time_ns = {},
      },
      make_upgrade_config())));
}

TEST_CASE("Regular conversion permits missing initialization input with a fallback")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const auto source = temp_dir.get_path() / "source";
  const auto output = temp_dir.get_path() / "output";
  const std::string source_string{source.c_str()};
  const std::string output_string{output.c_str()};
  write_source(source_string);

  auto config = make_config();
  const auto process_uuid = ProcessUuid::from_string("11111111-1111-4111-8111-111111111111");
  REQUIRE(process_uuid);
  auto& requirement = config->get_underlying_initialization_requirements().emplace_back();
  requirement.set_process_uuid(*process_uuid);
  REQUIRE(requirement.try_set_source_channel_name("/target"));
  REQUIRE(requirement.try_set_cpu_domain_name("domain"));
  requirement.set_allow_missing(true);

  const ConverterRequest request{
    .source_uri = source_string,
    .generated_config_path = {},
    .output_path = output_string,
    .start_time_ns = {},
    .end_time_ns = {},
  };
  RegularConversionResult result;
  REQUIRE(jewels::ok(convert_for_test(jewels::Out{result}, request, config)));
}

} // namespace
} // namespace clockwork_logging::realtime_playback
