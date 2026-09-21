// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/realtime_playback/converter_request.hh"
#include "clockwork/logging/realtime_playback/converter_setup.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging::realtime_playback
{

/// One source message selected for initialization restoration.
struct SelectedInitializationMessage
{
  uint32_t sequence_number{};
  LogTimestamp publish_time;
  LogTimestamp log_time;
  std::vector<std::byte> header;
  std::vector<std::byte> data;
};

/// Initialization messages indexed by source channel.
using InitializationSelection = std::map<std::string, SelectedInitializationMessage>;

/// One initialization log produced beneath the output root.
struct InitializationLogResult
{
  ProcessUuid process_uuid;
  std::string simplelaunch_node_name;
  std::string relative_path;
};

/// Results accumulated for later manifest finalization.
struct InitializationConversionResult
{
  std::vector<InitializationLogResult> initialization_logs;
};

/// Select the first message on each requested persistent source channel at the interval start.
jewels::BinaryOutcome select_initialization_messages(
  jewels::Out<InitializationSelection> selection_out,
  std::string_view source_uri,
  LogInterval interval,
  std::span<const std::string_view> source_channel_names);

/// Convert selected initialization messages into one log per process with logged values.
jewels::BinaryOutcome convert_initialization_logs(
  jewels::Out<InitializationConversionResult> result_out,
  const ConverterRequest& request,
  const ConverterContext& context);

} // namespace clockwork_logging::realtime_playback
