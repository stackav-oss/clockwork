// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_publisher_config_clk_cc.hh"
#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/onboard/clockwork_writer_policy.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/realtime_playback/bundle_converter.hh"
#include "clockwork/logging/realtime_playback/camera_conversion.hh"
#include "clockwork/logging/realtime_playback/converter_request.hh"
#include "clockwork/logging/realtime_playback/converter_setup.hh"
#include "clockwork/logging/realtime_playback/realtime_playback_conversion_config_clk_cc.hh"
#include "clockwork/logging/realtime_playback/realtime_playback_stream_kind_clk_cc.hh"
#include "clockwork/logging/realtime_playback/v1/realtime_playback_converter_manifest.pb.h"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/tests/support/test_message_clk_cc.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <google/protobuf/repeated_ptr_field.h>
#include <google/protobuf/text_format.h>
#include <gsl/util>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace clockwork_logging::realtime_playback
{
namespace
{

namespace manifest_v1 = clockwork::logging::realtime_playback::v1;
using TestMessage = clockwork::Tappy<tests::TestMessage>;
using TestWriter = onboard::Writer<onboard::ClockworkWriterPolicy<>>;

[[nodiscard]] std::string test_schema()
{
  const auto& definition = clockwork::LoggingTraits<TestMessage>::schema_definition;
  return {definition.data(), definition.size()};
}

void write_source(const std::filesystem::path& path)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  TestWriter writer{
    memory_resource, memory_resource, 1024U, std::chrono::nanoseconds{0}, onboard::WriterEnvironment::simulation};
  REQUIRE(writer.open_log(path.string(), "source", jewels::time::SteadyClock::now()));
  REQUIRE(writer.add_channel(
    onboard::LoggedChannelMetadata{
      .channel_name = "/source",
      .compression_type = CompressionType::none,
      .message_encoding = MessageEncoding::tachyon,
      .channel_type = ChannelType::regular,
      .schema_name = clockwork::LoggingTraits<TestMessage>::schema_name,
      .schema_encoding = SchemaEncoding::clockwork_tachyon,
      .schema_definition = test_schema(),
    },
    jewels::time::SteadyClock::now()));
  for (const auto index : std::ranges::views::iota(size_t{0}, size_t{2}))
  {
    TestMessage message;
    message.get_underlying_message_string().set_truncate(std::to_string(index));
    const auto publish_time = static_cast<int64_t>((index + 1U) * 10U);
    REQUIRE(writer.log_message_wait(
      onboard::Message{
        .channel_name = "/source",
        .sequence_number = static_cast<uint32_t>(index + 1U),
        .log_time = LogTimestamp{publish_time + 1},
        .message_time = LogTimestamp{publish_time},
        .header = {},
        .data = std::as_bytes(std::span{&message, 1}),
      },
      false,
      jewels::time::SteadyClock::now()));
  }
  REQUIRE(writer.close_log(jewels::time::SteadyClock::now()));
  REQUIRE(writer.drain_async_operations());
}

void add_publisher(ConversionConfig& config, const std::string_view channel_name)
{
  auto& publisher = config.get_mutable_publishers().get_underlying_channels().emplace_back();
  REQUIRE(publisher.try_set_channel_name(channel_name));
  REQUIRE(publisher.try_set_class_name(clockwork::LoggingTraits<TestMessage>::class_name));
  const auto schema = test_schema();
  REQUIRE(publisher.try_set_schema_definition(std::as_bytes(std::span{schema})));
  publisher.set_message_size(sizeof(TestMessage));
}

void add_assignment(
  ConversionConfig& config,
  const std::string_view domain_name,
  const std::string_view node_name,
  const std::string_view destination,
  const RealtimePlaybackStreamKind stream_kind)
{
  auto& domain = config.get_underlying_cpu_domains().emplace_back();
  REQUIRE(domain.try_set_cpu_domain_name(domain_name));
  REQUIRE(domain.try_set_simplelaunch_node_name(node_name));
  auto& assignment = domain.get_underlying_assignments().emplace_back();
  REQUIRE(assignment.try_set_source_channel_name("/source"));
  REQUIRE(assignment.try_set_destination_channel_name(destination));
  assignment.set_stream_kind(stream_kind);
}

[[nodiscard]] ProcessUuid test_process_uuid()
{
  const auto process_uuid = ProcessUuid::from_string("12345678-1234-1234-1234-123456789abc");
  REQUIRE(process_uuid);
  return *process_uuid;
}

[[nodiscard]] std::shared_ptr<ConversionConfig> make_config(const bool include_camera = true)
{
  auto config = std::make_shared<ConversionConfig>();
  config->set_format_version(1U);
  REQUIRE(config->try_set_platform_id("test-platform"));
  config->set_platform_config_xxh3(0x1234U);
  add_publisher(*config, "/regular");
  add_publisher(*config, "/source");
  add_assignment(*config, "domain_a", "c1", "/regular", RealtimePlaybackStreamKind::regular);
  if (include_camera)
  {
    add_publisher(*config, "/camera");
    add_assignment(*config, "domain_b", "c2", "/camera", RealtimePlaybackStreamKind::camera);
  }
  auto& requirement = config->get_underlying_initialization_requirements().emplace_back();
  requirement.set_process_uuid(test_process_uuid());
  REQUIRE(requirement.try_set_source_channel_name("/source"));
  REQUIRE(requirement.try_set_cpu_domain_name("domain_a"));
  requirement.set_allow_missing(false);
  return config;
}

[[nodiscard]] CameraConverterFunction
make_camera_converter(const std::optional<LogTimestamp> idr_time, const uint64_t preroll_message_count)
{
  return
    [idr_time, preroll_message_count](
      CameraConversionResult& result_out, const ConverterRequest&, const ConverterContext&) -> jewels::BinaryOutcome
  {
    CameraConversionResult result;
    CameraNodeResult node{
      .simplelaunch_node_name = "c2",
      .relative_path = std::nullopt,
      .channels = {},
    };
    node.channels.emplace_back(
      CameraChannelResult{
        .destination_channel_name = "/camera", .idr_time = idr_time, .preroll_message_count = preroll_message_count});
    result.nodes.emplace_back(std::move(node));
    result_out = std::move(result);
    return jewels::success;
  };
}

void write_config(const std::filesystem::path& path, const ConversionConfig& config)
{
  const jewels::filesystem::File file{path.string(), O_CREAT | O_TRUNC | O_WRONLY};
  const auto bytes = std::as_bytes(jewels::as_single_item_span(config));
  REQUIRE(::write(file.descriptor(), bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
}

[[nodiscard]] ConverterRequest make_request(
  const std::filesystem::path& source, const std::filesystem::path& config, const std::filesystem::path& output)
{
  return ConverterRequest{
    .source_uri = source.string(),
    .generated_config_path = config.string(),
    .output_path = output.string(),
    .start_time_ns = 20,
    .end_time_ns = 20,
  };
}

[[nodiscard]] manifest_v1::RealtimePlaybackConverterManifest read_manifest(const std::filesystem::path& output)
{
  std::ifstream stream{output / "converter_manifest.textproto"};
  const std::string text{std::istreambuf_iterator<char>{stream}, {}};
  manifest_v1::RealtimePlaybackConverterManifest manifest;
  REQUIRE(google::protobuf::TextFormat::ParseFromString(text, &manifest));
  return manifest;
}

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path& path)
{
  std::ifstream stream{path, std::ios::binary};
  const std::vector<char> chars{std::istreambuf_iterator<char>{stream}, {}};
  std::vector<std::byte> bytes(chars.size());
  std::ranges::transform(chars, bytes.begin(), [](const char value) { return static_cast<std::byte>(value); });
  return bytes;
}

void check_log(const std::filesystem::path& path, const std::string_view topic, const size_t expected_count)
{
  auto reader = make_reader(path.string(), {}, {});
  REQUIRE(reader->open({}));
  size_t count{};
  while (const auto message = reader->next_message())
  {
    CHECK(message->topic == topic);
    ++count;
  }
  CHECK(count == expected_count);
  REQUIRE(reader->close());
}

TEST_CASE("Bundle conversion publishes a complete checksummed fixture")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const std::filesystem::path temp_path{temp_dir.get_path().c_str()};
  const auto source = temp_path / "source";
  const auto config_path = temp_path / "config.tachyon";
  const auto output = temp_path / "bundle";
  write_source(source);
  write_config(config_path, *make_config());

  BundleConversionResult result;
  REQUIRE(
    jewels::ok(convert_realtime_playback_bundle(
      jewels::Out{result}, make_request(source, config_path, output), make_camera_converter(LogTimestamp{10}, 1U))));
  CHECK(result.output_path == output.string());
  CHECK(result.requested_interval == LogInterval{LogTimestamp{20}, LogTimestamp{20}});

  const auto manifest = read_manifest(output);
  CHECK(manifest.platform_id() == "test-platform");
  CHECK(manifest.platform_config_xxh3() == 0x1234U);
  CHECK(manifest.nodes_size() == 2);
  REQUIRE(manifest.files_size() > 0);
  uint64_t total_size{};
  for (const auto& file : manifest.files())
  {
    const auto bytes = read_file(output / file.relative_path());
    CHECK(file.size_bytes() == bytes.size());
    CHECK(file.xxh3() == compute_xxh3_checksum(bytes));
    total_size += file.size_bytes();
  }
  CHECK(manifest.total_artifact_size_bytes() == total_size);
  CHECK(result.total_artifact_size_bytes == total_size);
  CHECK(result.artifact_file_count == static_cast<size_t>(manifest.files_size()));

  const auto& first_node = manifest.nodes(0);
  const auto& second_node = manifest.nodes(1);
  CHECK(first_node.simplelaunch_node_name() == "c1");
  CHECK(second_node.simplelaunch_node_name() == "c2");
  check_log(output / first_node.regular_log_path(), "/regular", 1U);
  REQUIRE(first_node.processes_size() == 1);
  check_log(output / first_node.processes(0).initialization_log_path(), "/source", 1U);
  REQUIRE(second_node.camera_channels_size() == 1);
  CHECK(second_node.camera_channels(0).idr_time_ns() == 10);
  CHECK(second_node.camera_channels(0).preroll_message_count() == 1U);
}

TEST_CASE("Bundle conversion rejects existing output and leaves failures visible")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const std::filesystem::path temp_path{temp_dir.get_path().c_str()};
  const auto output = temp_path / "bundle";
  REQUIRE(std::filesystem::create_directory(output));
  BundleConversionResult result;
  CHECK(
    jewels::fails(convert_realtime_playback_bundle(
      jewels::Out{result}, make_request("missing", "missing", output), CameraConverterFunction{})));
  REQUIRE(std::filesystem::remove(output));

  CHECK(
    jewels::fails(convert_realtime_playback_bundle(
      jewels::Out{result}, make_request("missing", "missing", output), CameraConverterFunction{})));
  CHECK(std::filesystem::is_directory(output));
}

TEST_CASE("Bundle conversion rejects a dangling output symlink")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const std::filesystem::path temp_path{temp_dir.get_path().c_str()};
  const auto output = temp_path / "bundle";
  std::filesystem::create_symlink(temp_path / "missing-target", output);
  BundleConversionResult result;
  CHECK(
    jewels::fails(convert_realtime_playback_bundle(
      jewels::Out{result}, make_request("missing", "missing", output), CameraConverterFunction{})));
  CHECK(std::filesystem::is_symlink(output));
}

TEST_CASE("Bundle conversion requires an existing output parent")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const std::filesystem::path temp_path{temp_dir.get_path().c_str()};
  BundleConversionResult result;
  CHECK(
    jewels::fails(convert_realtime_playback_bundle(
      jewels::Out{result},
      make_request("missing", "missing", temp_path / "missing" / "bundle"),
      CameraConverterFunction{})));
}

TEST_CASE("Bundle conversion requires a camera implementation for camera assignments")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const std::filesystem::path temp_path{temp_dir.get_path().c_str()};
  const auto source = temp_path / "source";
  const auto config_path = temp_path / "config.tachyon";
  const auto output = temp_path / "bundle";
  write_source(source);
  write_config(config_path, *make_config());
  BundleConversionResult result;
  CHECK(
    jewels::fails(convert_realtime_playback_bundle(
      jewels::Out{result}, make_request(source, config_path, output), CameraConverterFunction{})));
  CHECK(std::filesystem::is_directory(output));
}

TEST_CASE("Bundle conversion propagates camera implementation failures")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const std::filesystem::path temp_path{temp_dir.get_path().c_str()};
  const auto source = temp_path / "source";
  const auto config_path = temp_path / "config.tachyon";
  const auto output = temp_path / "bundle";
  write_source(source);
  write_config(config_path, *make_config());
  const CameraConverterFunction failing_converter =
    [](CameraConversionResult&, const ConverterRequest&, const ConverterContext&) -> jewels::BinaryOutcome
  { return jewels::failure; };
  BundleConversionResult result;
  CHECK(
    jewels::fails(convert_realtime_playback_bundle(
      jewels::Out{result}, make_request(source, config_path, output), failing_converter)));
  CHECK(std::filesystem::is_directory(output));
}

TEST_CASE("Bundle conversion does not require a camera implementation without camera assignments")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const std::filesystem::path temp_path{temp_dir.get_path().c_str()};
  const auto source = temp_path / "source";
  const auto config_path = temp_path / "config.tachyon";
  const auto output = temp_path / "bundle";
  write_source(source);
  write_config(config_path, *make_config(false));
  BundleConversionResult result;
  REQUIRE(
    jewels::ok(convert_realtime_playback_bundle(
      jewels::Out{result}, make_request(source, config_path, output), CameraConverterFunction{})));
  CHECK(std::filesystem::exists(output / "converter_manifest.textproto"));
}

TEST_CASE("Omitted and equivalent absolute intervals produce the same semantics")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const std::filesystem::path temp_path{temp_dir.get_path().c_str()};
  const auto source = temp_path / "source";
  const auto config_path = temp_path / "config.tachyon";
  const auto full_output = temp_path / "full";
  const auto absolute_output = temp_path / "absolute";
  write_source(source);
  write_config(config_path, *make_config());
  const auto source_string = source.string();
  const auto config_string = config_path.string();
  const auto full_output_string = full_output.string();
  const auto absolute_output_string = absolute_output.string();
  const std::array full_args{
    std::string_view{"--source"},
    std::string_view{source_string},
    std::string_view{"--generated-config"},
    std::string_view{config_string},
    std::string_view{"--output"},
    std::string_view{full_output_string},
  };
  const std::array absolute_args{
    std::string_view{"--source"},
    std::string_view{source_string},
    std::string_view{"--generated-config"},
    std::string_view{config_string},
    std::string_view{"--output"},
    std::string_view{absolute_output_string},
    std::string_view{"--start-time-ns"},
    std::string_view{"10"},
    std::string_view{"--end-time-ns"},
    std::string_view{"20"},
  };
  ConverterRequest full_request;
  ConverterRequest absolute_request;
  REQUIRE(jewels::ok(parse_converter_request(jewels::Out{full_request}, full_args)));
  REQUIRE(jewels::ok(parse_converter_request(jewels::Out{absolute_request}, absolute_args)));
  BundleConversionResult full_result;
  BundleConversionResult absolute_result;
  const auto camera_converter = make_camera_converter(LogTimestamp{10}, 0U);
  REQUIRE(jewels::ok(convert_realtime_playback_bundle(jewels::Out{full_result}, full_request, camera_converter)));
  REQUIRE(
    jewels::ok(convert_realtime_playback_bundle(jewels::Out{absolute_result}, absolute_request, camera_converter)));
  CHECK(full_result.requested_interval == absolute_result.requested_interval);
  const auto full_manifest = read_manifest(full_output);
  const auto absolute_manifest = read_manifest(absolute_output);
  CHECK(full_manifest.requested_start_time_ns() == absolute_manifest.requested_start_time_ns());
  CHECK(full_manifest.requested_end_time_ns() == absolute_manifest.requested_end_time_ns());
  REQUIRE(full_manifest.nodes_size() == 2);
  REQUIRE(absolute_manifest.nodes_size() == 2);
  CHECK(full_manifest.files_size() == absolute_manifest.files_size());
  check_log(full_output / full_manifest.nodes(0).regular_log_path(), "/regular", 2U);
  check_log(absolute_output / absolute_manifest.nodes(0).regular_log_path(), "/regular", 2U);
  REQUIRE(full_manifest.nodes(0).processes_size() == 1);
  REQUIRE(absolute_manifest.nodes(0).processes_size() == 1);
  check_log(full_output / full_manifest.nodes(0).processes(0).initialization_log_path(), "/source", 1U);
  check_log(absolute_output / absolute_manifest.nodes(0).processes(0).initialization_log_path(), "/source", 1U);
  REQUIRE(full_manifest.nodes(1).camera_channels_size() == 1);
  REQUIRE(absolute_manifest.nodes(1).camera_channels_size() == 1);
  CHECK(full_manifest.nodes(1).camera_channels(0).idr_time_ns() == 10);
  CHECK(absolute_manifest.nodes(1).camera_channels(0).idr_time_ns() == 10);
  CHECK(full_manifest.nodes(1).camera_channels(0).preroll_message_count() == 0U);
  CHECK(absolute_manifest.nodes(1).camera_channels(0).preroll_message_count() == 0U);
}

} // namespace
} // namespace clockwork_logging::realtime_playback
