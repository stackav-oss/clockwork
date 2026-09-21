// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/realtime_playback/converter_setup.hh"

#include "clockwork/common/exec_tools.hh"
#include "clockwork/logging/channel_publisher_config_clk_cc.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/realtime_playback/converter_request.hh"
#include "clockwork/logging/realtime_playback/realtime_playback_conversion_config_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/clk_type.hh"
#include "clockwork/serialization/cpp/tachyon_model.hh"
#include "clockwork/serialization/cpp/tachyon_upgrader.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/scope_guard/scope_guard.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace clockwork_logging::realtime_playback
{
namespace
{

using PublisherConfig = clockwork::Tappy<PublishedChannelConfig<>>;
using InitializationRequirementConfig = clockwork::Tappy<InitializationRequirement>;
using MetadataByName = std::map<std::string_view, const TopicMetadata*>;
using PublisherByName = std::map<std::string_view, const PublisherConfig*>;
using NodeByDomain = std::map<std::string_view, std::string_view>;

struct TargetSchemaInfo
{
  std::string name;
};

/// Parse Tachyon metadata and extract the fully qualified schema name.
jewels::BinaryOutcome
target_schema_info(jewels::Out<TargetSchemaInfo> info_out, const std::span<const std::byte> schema)
{
  const auto schema_chars = jewels::as_chars(schema);
  const std::string serialized{schema_chars.data(), schema_chars.size()};
  auto metadata = std::make_unique<clockwork::serialization::metadata::TachyonMetadata>();
  if (!metadata->ParseFromString(serialized))
  {
    return jewels::failure;
  }
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model = clockwork::serialization::TachyonModel::from_proto(memory_resource, std::move(metadata));
  *info_out = TargetSchemaInfo{.name = std::string{model->get_outer_type().get_fqn()}};
  return jewels::success;
}

/// Extract the target schema name and create an upgrader from source metadata.
jewels::BinaryOutcome prepare_target_schema(
  jewels::Out<TargetSchemaInfo> schema_info_out,
  jewels::Out<std::unique_ptr<clockwork::serialization::TachyonUpgrader>> upgrader_out,
  const PublisherConfig& publisher,
  const TopicMetadata& source_metadata)
{
  const auto schema = publisher.get_schema_definition();
  TargetSchemaInfo schema_info;
  if (jewels::fails(target_schema_info(jewels::Out{schema_info}, schema)))
  {
    return jewels::failure;
  }
  auto upgrader = clockwork::serialization::make_tachyon_cpp_upgrader(
    publisher.get_class_name(), schema, std::as_bytes(std::span{source_metadata.schema_definition}));
  if (!upgrader)
  {
    return jewels::failure;
  }
  *schema_info_out = std::move(schema_info);
  *upgrader_out = std::move(upgrader);
  return jewels::success;
}

/// Build node-indexed conversion plans for playback assignments.
jewels::BinaryOutcome prepare_assignments(
  jewels::Out<std::vector<ConverterAssignment>> assignments_out,
  jewels::Out<std::vector<ConverterNode>> nodes_out,
  const ConversionConfig& config,
  const MetadataByName& metadata_by_name,
  const PublisherByName& publishers)
{
  std::vector<ConverterAssignment> assignments;
  std::map<std::string, std::vector<size_t>> assignments_by_node;
  for (const auto& domain : config.get_cpu_domains())
  {
    const auto node_name = domain.get_simplelaunch_node_name();
    for (const auto& assignment : domain.get_assignments())
    {
      const auto metadata_iter = metadata_by_name.find(assignment.get_source_channel_name());
      if (metadata_iter == metadata_by_name.end())
      {
        jewels::log_cerr_error("Selected source channel is missing: {}", assignment.get_source_channel_name());
        return jewels::failure;
      }
      if (metadata_iter->second->message_encoding != MessageEncoding::tachyon)
      {
        jewels::log_cerr_error("Selected source channel is not Tachyon: {}", assignment.get_source_channel_name());
        return jewels::failure;
      }
      const auto& publisher = *publishers.at(assignment.get_destination_channel_name());
      TargetSchemaInfo schema_info;
      std::unique_ptr<clockwork::serialization::TachyonUpgrader> upgrader;
      if (jewels::fails(
            prepare_target_schema(jewels::Out{schema_info}, jewels::Out{upgrader}, publisher, *metadata_iter->second)))
      {
        jewels::log_cerr_error(
          "Target publisher schema or upgrader is invalid: {}", assignment.get_destination_channel_name());
        return jewels::failure;
      }
      const auto schema = publisher.get_schema_definition();
      const auto index = assignments.size();
      assignments.emplace_back(
        ConverterAssignment{
          .source_channel_name = std::string{assignment.get_source_channel_name()},
          .destination_channel_name = std::string{assignment.get_destination_channel_name()},
          .simplelaunch_node_name = std::string{node_name},
          .stream_kind = assignment.get_stream_kind(),
          .target_schema_name = std::move(schema_info.name),
          .target_schema_definition = std::vector<std::byte>{schema.begin(), schema.end()},
          .target_message_size = publisher.get_message_size(),
          .upgrader = std::move(upgrader),
        });
      assignments_by_node[std::string{node_name}].emplace_back(index);
    }
  }

  std::vector<ConverterNode> nodes;
  nodes.reserve(assignments_by_node.size());
  for (auto& [node_name, assignment_indices] : assignments_by_node)
  {
    nodes.emplace_back(
      ConverterNode{.simplelaunch_node_name = node_name, .assignment_indices = std::move(assignment_indices)});
  }
  *assignments_out = std::move(assignments);
  *nodes_out = std::move(nodes);
  return jewels::success;
}

/// Build conversion and process plans for initialization requirements.
jewels::BinaryOutcome prepare_initialization(
  jewels::Out<std::vector<ConverterInitializationRequirement>> requirements_out,
  jewels::Out<std::vector<ConverterProcess>> processes_out,
  const ConversionConfig& config,
  const MetadataByName& metadata_by_name,
  const PublisherByName& publishers,
  const NodeByDomain& node_by_domain)
{
  std::vector<ConverterInitializationRequirement> requirements;
  std::vector<ConverterProcess> processes;
  std::map<ProcessUuid, size_t> process_indices;
  std::map<std::pair<ProcessUuid, std::string>, size_t> requirement_indices;
  for (const auto& requirement : config.get_initialization_requirements())
  {
    const auto [process_iter, process_inserted] =
      process_indices.emplace(requirement.get_process_uuid(), processes.size());
    if (process_inserted)
    {
      processes.emplace_back(
        ConverterProcess{
          .process_uuid = requirement.get_process_uuid(),
          .simplelaunch_node_name = std::string{node_by_domain.at(requirement.get_cpu_domain_name())},
          .requirement_indices = {}});
    }
    const auto process_index = process_iter->second;
    const auto requirement_key = std::pair<ProcessUuid, std::string>{
      requirement.get_process_uuid(), std::string{requirement.get_source_channel_name()}};
    if (requirement_indices.contains(requirement_key))
    {
      continue;
    }

    const auto metadata_iter = metadata_by_name.find(requirement.get_source_channel_name());
    if (metadata_iter != metadata_by_name.end() && metadata_iter->second->message_encoding != MessageEncoding::tachyon)
    {
      jewels::log_cerr_error(
        "Selected initialization source channel is not Tachyon: {}", requirement.get_source_channel_name());
      return jewels::failure;
    }
    const auto& publisher = *publishers.at(requirement.get_source_channel_name());
    const auto schema = publisher.get_schema_definition();
    TargetSchemaInfo schema_info;
    std::unique_ptr<clockwork::serialization::TachyonUpgrader> upgrader;
    if (metadata_iter == metadata_by_name.end())
    {
      if (!requirement.get_allow_missing())
      {
        jewels::log_cerr_error(
          "Selected initialization source channel is missing: {}", requirement.get_source_channel_name());
        return jewels::failure;
      }
      if (jewels::fails(target_schema_info(jewels::Out{schema_info}, schema)))
      {
        jewels::log_cerr_error(
          "Unable to read initialization target schema: {}", requirement.get_source_channel_name());
        return jewels::failure;
      }
    }
    else if (jewels::fails(prepare_target_schema(
               jewels::Out{schema_info}, jewels::Out{upgrader}, publisher, *metadata_iter->second)))
    {
      jewels::log_cerr_error(
        "Initialization target schema or upgrader is invalid: {}", requirement.get_source_channel_name());
      return jewels::failure;
    }
    const auto destination_name = std::string{requirement.get_source_channel_name()};
    const auto index = requirements.size();
    requirements.emplace_back(
      ConverterInitializationRequirement{
        .process_uuid = requirement.get_process_uuid(),
        .source_channel_name = std::string{requirement.get_source_channel_name()},
        .destination_channel_name = destination_name,
        .allow_missing = requirement.get_allow_missing(),
        .target_schema_name = std::move(schema_info.name),
        .target_schema_definition = std::vector<std::byte>{schema.begin(), schema.end()},
        .target_message_size = publisher.get_message_size(),
        .upgrader = std::move(upgrader),
      });
    requirement_indices.emplace(requirement_key, index);
    processes.at(process_index).requirement_indices.emplace_back(index);
  }
  *requirements_out = std::move(requirements);
  *processes_out = std::move(processes);
  return jewels::success;
}

} // namespace

jewels::BinaryOutcome load_conversion_config(
  jewels::Out<std::shared_ptr<const ConversionConfig>> config_out, const std::string_view generated_config_path)
{
  auto result = clockwork::read_tachyon_config_to_heap<ConversionConfig>(generated_config_path);
  if (!result)
  {
    return jewels::failure;
  }
  if (result.value()->get_format_version() != 1U)
  {
    jewels::log_cerr_error("Unsupported conversion configuration version: {}", result.value()->get_format_version());
    return jewels::failure;
  }
  *config_out = std::move(result.value());
  return jewels::success;
}

jewels::BinaryOutcome prepare_converter_setup(
  jewels::Out<ConverterSetup> setup_out,
  const ConversionConfig& config,
  const std::vector<TopicMetadata>& source_metadata)
{
  std::map<std::string_view, const TopicMetadata*> metadata_by_name;
  for (const auto& metadata : source_metadata)
  {
    if (!metadata_by_name.emplace(metadata.name, &metadata).second)
    {
      jewels::log_cerr_error("Duplicate source metadata for channel: {}", metadata.name);
      return jewels::failure;
    }
  }
  std::map<std::string_view, const PublisherConfig*> publishers;
  for (const auto& publisher : config.get_publishers().get_channels())
  {
    publishers.emplace(publisher.get_channel_name(), &publisher);
  }

  std::map<std::string_view, std::string_view> node_by_domain;
  for (const auto& domain : config.get_cpu_domains())
  {
    node_by_domain.emplace(domain.get_cpu_domain_name(), domain.get_simplelaunch_node_name());
  }

  std::vector<ConverterAssignment> assignments;
  std::vector<ConverterNode> nodes;
  if (jewels::fails(
        prepare_assignments(jewels::Out{assignments}, jewels::Out{nodes}, config, metadata_by_name, publishers)))
  {
    return jewels::failure;
  }

  std::vector<ConverterInitializationRequirement> initialization_requirements;
  std::vector<ConverterProcess> initialization_processes;
  if (jewels::fails(prepare_initialization(
        jewels::Out{initialization_requirements},
        jewels::Out{initialization_processes},
        config,
        metadata_by_name,
        publishers,
        node_by_domain)))
  {
    return jewels::failure;
  }

  *setup_out = ConverterSetup{
    .assignments = std::move(assignments),
    .nodes = std::move(nodes),
    .initialization_requirements = std::move(initialization_requirements),
    .initialization_processes = std::move(initialization_processes)};
  return jewels::success;
}

jewels::BinaryOutcome prepare_converter_context(
  jewels::Out<ConverterContext> context_out, const ConverterRequest& request, const ConversionConfig& config)
{
  auto reader = make_reader(request.source_uri, {}, {});
  if (!reader->open({}))
  {
    return jewels::failure;
  }
  bool reader_open = true;
  jewels::ScopeGuard cleanup{[&reader, &reader_open]() noexcept
                             {
                               if (reader_open)
                               {
                                 // Cleanup is best effort while unwinding a failed conversion.
                                 std::ignore = reader->close();
                               }
                             }};
  const auto start = reader->start_time();
  const auto end = reader->end_time();
  if (!start || !end)
  {
    return jewels::failure;
  }
  const LogInterval source_interval{*start, *end};
  LogInterval requested_interval;
  if (jewels::fails(resolve_converter_interval(jewels::Out{requested_interval}, request, *start, *end)))
  {
    return jewels::failure;
  }
  ConverterSetup setup;
  if (jewels::fails(prepare_converter_setup(jewels::Out{setup}, config, reader->get_metadata())))
  {
    return jewels::failure;
  }
  const auto close_result = reader->close();
  reader_open = false;
  if (!close_result)
  {
    return jewels::failure;
  }
  *context_out = ConverterContext{
    .source_interval = source_interval, .requested_interval = requested_interval, .setup = std::move(setup)};
  return jewels::success;
}

} // namespace clockwork_logging::realtime_playback
