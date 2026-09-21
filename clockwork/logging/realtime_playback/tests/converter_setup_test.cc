// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_publisher_config_clk_cc.hh"
#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/realtime_playback/converter_setup.hh"
#include "clockwork/logging/realtime_playback/realtime_playback_conversion_config_clk_cc.hh"
#include "clockwork/logging/realtime_playback/realtime_playback_stream_kind_clk_cc.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/tests/support/test_message_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/tachyon_upgrader.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <algorithm>
#include <array>
#include <cstddef>
#include <fcntl.h>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace clockwork_logging::realtime_playback
{
namespace
{

using TestMessage = clockwork::Tappy<tests::TestMessage>;

void write_config(std::string_view path, const ConversionConfig& config)
{
  const jewels::filesystem::File file{path, O_CREAT | O_TRUNC | O_WRONLY};
  const auto bytes = std::as_bytes(jewels::as_single_item_span(config));
  REQUIRE(::write(file.descriptor(), bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
}

[[nodiscard]] std::string test_schema_definition()
{
  const auto& definition = clockwork::LoggingTraits<TestMessage>::schema_definition;
  return {definition.data(), definition.size()};
}

[[nodiscard]] std::shared_ptr<ConversionConfig> make_config()
{
  auto config = std::make_shared<ConversionConfig>();
  auto& publisher = config->get_mutable_publishers().get_underlying_channels().emplace_back();
  REQUIRE(publisher.try_set_channel_name("/target"));
  REQUIRE(publisher.try_set_class_name(clockwork::LoggingTraits<TestMessage>::class_name));
  const auto definition = test_schema_definition();
  REQUIRE(publisher.try_set_schema_definition(std::as_bytes(std::span{definition})));
  publisher.set_message_size(sizeof(TestMessage));
  auto& source_publisher = config->get_mutable_publishers().get_underlying_channels().emplace_back(publisher);
  REQUIRE(source_publisher.try_set_channel_name("/source"));
  return config;
}

void add_assignment(ConversionConfig& config, std::string_view domain_name, std::string_view node_name)
{
  auto& domain = config.get_underlying_cpu_domains().emplace_back();
  REQUIRE(domain.try_set_cpu_domain_name(domain_name));
  REQUIRE(domain.try_set_simplelaunch_node_name(node_name));
  auto& assignment = domain.get_underlying_assignments().emplace_back();
  REQUIRE(assignment.try_set_source_channel_name("/source"));
  REQUIRE(assignment.try_set_destination_channel_name("/target"));
  assignment.set_stream_kind(RealtimePlaybackStreamKind::regular);
}

[[nodiscard]] ProcessUuid make_process_uuid(std::string_view value)
{
  const auto uuid = ProcessUuid::from_string(value);
  REQUIRE(uuid);
  return *uuid;
}

void add_initialization_requirement(
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

[[nodiscard]] TopicMetadata make_source_metadata()
{
  return TopicMetadata{
    .name = "/source",
    .type = std::string{clockwork::LoggingTraits<TestMessage>::schema_name},
    .message_encoding = MessageEncoding::tachyon,
    .channel_type = ChannelType::regular,
    .schema_encoding = SchemaEncoding::clockwork_tachyon,
    .schema_definition = test_schema_definition(),
    .is_amended = false,
  };
}

TEST_CASE("Converter setup groups CPU domains by SimpleLaunch node")
{
  auto config = make_config();
  add_assignment(*config, "domain_a", "c1");
  auto& second_publisher = config->get_mutable_publishers().get_underlying_channels().emplace_back(
    config->get_publishers().get_channels().front());
  REQUIRE(second_publisher.try_set_channel_name("/target_two"));
  auto& second_domain = config->get_underlying_cpu_domains().emplace_back();
  REQUIRE(second_domain.try_set_cpu_domain_name("domain_b"));
  REQUIRE(second_domain.try_set_simplelaunch_node_name("c1"));
  auto& second_assignment = second_domain.get_underlying_assignments().emplace_back();
  REQUIRE(second_assignment.try_set_source_channel_name("/source"));
  REQUIRE(second_assignment.try_set_destination_channel_name("/target_two"));
  second_assignment.set_stream_kind(RealtimePlaybackStreamKind::regular);

  ConverterSetup setup;
  REQUIRE(jewels::ok(prepare_converter_setup(jewels::Out{setup}, *config, {make_source_metadata()})));
  REQUIRE(setup.nodes.size() == 1U);
  CHECK(setup.nodes.front().simplelaunch_node_name == "c1");
  CHECK(setup.nodes.front().assignment_indices.size() == 2U);
  CHECK(setup.assignments.size() == 2U);
  CHECK(setup.nodes.front().assignment_indices == std::vector<size_t>{0U, 1U});
  CHECK(setup.assignments.at(0).source_channel_name == "/source");
  CHECK(setup.assignments.at(0).destination_channel_name == "/target");
  CHECK(setup.assignments.at(1).destination_channel_name == "/target_two");
  CHECK(setup.assignments.at(0).stream_kind == RealtimePlaybackStreamKind::regular);
  CHECK(setup.assignments.at(0).target_schema_name == clockwork::LoggingTraits<TestMessage>::schema_name);
  CHECK(setup.assignments.at(0).target_message_size == sizeof(TestMessage));
  REQUIRE(setup.assignments.at(0).upgrader);

  const TestMessage source_message;
  std::vector<std::byte> upgraded(sizeof(TestMessage));
  setup.assignments.at(0).upgrader->upgrade(std::as_bytes(std::span{&source_message, 1}), upgraded);
  CHECK(std::ranges::equal(std::as_bytes(std::span{&source_message, 1}), upgraded));
}

TEST_CASE("Converter setup deduplicates initialization sources per process")
{
  auto config = make_config();
  add_assignment(*config, "domain", "c1");
  const auto first_process = make_process_uuid("11111111-1111-4111-8111-111111111111");
  const auto second_process = make_process_uuid("22222222-2222-4222-8222-222222222222");
  add_initialization_requirement(*config, first_process, "/source");
  add_initialization_requirement(*config, first_process, "/source");
  add_initialization_requirement(*config, second_process, "/source");

  ConverterSetup setup;
  REQUIRE(jewels::ok(prepare_converter_setup(jewels::Out{setup}, *config, {make_source_metadata()})));
  REQUIRE(setup.initialization_requirements.size() == 2U);
  REQUIRE(setup.initialization_processes.size() == 2U);
  CHECK(setup.initialization_processes.at(0).process_uuid == first_process);
  CHECK(setup.initialization_processes.at(0).simplelaunch_node_name == "c1");
  CHECK(setup.initialization_processes.at(0).requirement_indices == std::vector<size_t>{0U});
  CHECK(setup.initialization_processes.at(1).process_uuid == second_process);
  CHECK(setup.initialization_processes.at(1).simplelaunch_node_name == "c1");
  CHECK(setup.initialization_processes.at(1).requirement_indices == std::vector<size_t>{1U});
}

TEST_CASE("Converter setup rejects an absent required initialization source")
{
  auto config = make_config();
  add_assignment(*config, "domain", "c1");
  const auto process_uuid = make_process_uuid("33333333-3333-4333-8333-333333333335");
  add_initialization_requirement(*config, process_uuid, "/target");
  ConverterSetup setup;
  REQUIRE(jewels::fails(prepare_converter_setup(jewels::Out{setup}, *config, {make_source_metadata()})));
}

TEST_CASE("Converter setup allows an absent initialization source with a fallback")
{
  auto config = make_config();
  add_assignment(*config, "domain", "c1");
  const auto process_uuid = make_process_uuid("33333333-3333-4333-8333-333333333336");
  add_initialization_requirement(*config, process_uuid, "/target", true);

  ConverterSetup setup;
  REQUIRE(jewels::ok(prepare_converter_setup(jewels::Out{setup}, *config, {make_source_metadata()})));
  REQUIRE(setup.initialization_requirements.size() == 1U);
  const auto& requirement = setup.initialization_requirements.front();
  CHECK(requirement.allow_missing);
  CHECK(requirement.target_schema_name == clockwork::LoggingTraits<TestMessage>::schema_name);
  CHECK(requirement.target_message_size == sizeof(TestMessage));
  CHECK_FALSE(requirement.target_schema_definition.empty());
  CHECK_FALSE(requirement.upgrader);
}

TEST_CASE("Converter setup rejects missing source metadata")
{
  auto config = make_config();
  add_assignment(*config, "domain", "c1");
  ConverterSetup setup;
  REQUIRE(jewels::fails(prepare_converter_setup(jewels::Out{setup}, *config, {})));
}

TEST_CASE("Converter setup loads only complete supported configurations")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const auto valid_path = (temp_dir.get_path() / "valid.tachyon").string();
  auto config = make_config();
  write_config(valid_path, *config);

  std::shared_ptr<const ConversionConfig> loaded;
  REQUIRE(jewels::ok(load_conversion_config(jewels::Out{loaded}, valid_path)));
  REQUIRE(loaded);
  CHECK(loaded->get_format_version() == 1U);

  const auto unsupported_path = (temp_dir.get_path() / "unsupported.tachyon").string();
  config->set_format_version(2U);
  write_config(unsupported_path, *config);
  REQUIRE(jewels::fails(load_conversion_config(jewels::Out{loaded}, unsupported_path)));

  const auto truncated_path = (temp_dir.get_path() / "truncated.tachyon").string();
  const jewels::filesystem::File truncated_file{truncated_path, O_CREAT | O_TRUNC | O_WRONLY};
  constexpr std::byte byte{};
  REQUIRE(::write(truncated_file.descriptor(), &byte, sizeof(byte)) == static_cast<ssize_t>(sizeof(byte)));
  REQUIRE(jewels::fails(load_conversion_config(jewels::Out{loaded}, truncated_path)));

  const auto missing_path = (temp_dir.get_path() / "missing.tachyon").string();
  REQUIRE(jewels::fails(load_conversion_config(jewels::Out{loaded}, missing_path)));
}

} // namespace
} // namespace clockwork_logging::realtime_playback
