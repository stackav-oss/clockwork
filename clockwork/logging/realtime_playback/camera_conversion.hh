// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/realtime_playback/converter_request.hh"
#include "clockwork/logging/realtime_playback/converter_setup.hh"
#include "jewels/callsig/outcome.hh"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace clockwork_logging::realtime_playback
{

/// Camera pre-roll selected for one destination assignment.
struct CameraChannelResult
{
  std::string destination_channel_name;
  std::optional<LogTimestamp> idr_time;
  uint64_t preroll_message_count{};
};

/// Camera results for one SimpleLaunch node.
struct CameraNodeResult
{
  std::string simplelaunch_node_name;
  std::optional<std::string> relative_path;
  std::vector<CameraChannelResult> channels;
};

/// Results accumulated for later manifest finalization.
struct CameraConversionResult
{
  std::vector<CameraNodeResult> nodes;
};

/// Camera conversion extension invoked by the bundle converter.
using CameraConverterFunction =
  std::function<jewels::BinaryOutcome(CameraConversionResult&, const ConverterRequest&, const ConverterContext&)>;

} // namespace clockwork_logging::realtime_playback
