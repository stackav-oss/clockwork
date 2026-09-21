// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/realtime_playback/converter_request.hh"
#include "clockwork/logging/realtime_playback/realtime_playback_conversion_config_clk_cc.hh"
#include "clockwork/logging/realtime_playback/realtime_playback_stream_kind_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/tachyon_upgrader.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging::realtime_playback
{

using ConversionConfig = clockwork::Tappy<RealtimePlaybackConversionConfig>;
using ProcessUuid = jewels::Uuid<clockwork::common::ProcessInstanceId>;

/// Validated conversion details for one replay assignment.
struct ConverterAssignment
{
  std::string source_channel_name;
  std::string destination_channel_name;
  std::string simplelaunch_node_name;
  RealtimePlaybackStreamKind stream_kind{};
  std::string target_schema_name;
  std::vector<std::byte> target_schema_definition;
  uint32_t target_message_size{};
  std::unique_ptr<clockwork::serialization::TachyonUpgrader> upgrader;
};

/// Assignment indices grouped by one explicit SimpleLaunch node.
struct ConverterNode
{
  std::string simplelaunch_node_name;
  std::vector<size_t> assignment_indices;
};

/// Validated initialization conversion for one process requirement.
struct ConverterInitializationRequirement
{
  ProcessUuid process_uuid;
  std::string source_channel_name;
  std::string destination_channel_name;
  bool allow_missing{};
  std::string target_schema_name;
  std::vector<std::byte> target_schema_definition;
  uint32_t target_message_size{};
  std::unique_ptr<clockwork::serialization::TachyonUpgrader> upgrader;
};

/// Initialization requirements owned by one process.
struct ConverterProcess
{
  ProcessUuid process_uuid;
  std::string simplelaunch_node_name;
  std::vector<size_t> requirement_indices;
};

/// Immutable setup shared by the converter passes.
struct ConverterSetup
{
  std::vector<ConverterAssignment> assignments;
  std::vector<ConverterNode> nodes;
  std::vector<ConverterInitializationRequirement> initialization_requirements;
  std::vector<ConverterProcess> initialization_processes;
};

/// Source bounds, selected interval, and validated plans shared by all conversion passes.
struct ConverterContext
{
  LogInterval source_interval;
  LogInterval requested_interval;
  ConverterSetup setup;
};

/// Load a fixed-size generated conversion configuration.
jewels::BinaryOutcome load_conversion_config(
  jewels::Out<std::shared_ptr<const ConversionConfig>> config_out, std::string_view generated_config_path);

/// Validate source metadata and construct deterministic node and upgrade plans.
jewels::BinaryOutcome prepare_converter_setup(
  jewels::Out<ConverterSetup> setup_out,
  const ConversionConfig& config,
  const std::vector<TopicMetadata>& source_metadata);

/// Read source metadata once and prepare the context shared by all conversion passes.
jewels::BinaryOutcome prepare_converter_context(
  jewels::Out<ConverterContext> context_out, const ConverterRequest& request, const ConversionConfig& config);

} // namespace clockwork_logging::realtime_playback
