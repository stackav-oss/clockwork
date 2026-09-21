// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/realtime_playback/converter_request.hh"
#include "clockwork/logging/realtime_playback/converter_setup.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"

#include <string>
#include <vector>

namespace clockwork_logging::realtime_playback
{

/// One closed regular log produced beneath the output root.
struct RegularLogResult
{
  std::string simplelaunch_node_name;
  std::string relative_path;
};

/// Results accumulated for later manifest finalization.
struct RegularConversionResult
{
  std::vector<RegularLogResult> regular_logs;
};

/// Convert regular assignments using the shared conversion context.
jewels::BinaryOutcome convert_regular_logs(
  jewels::Out<RegularConversionResult> result_out, const ConverterRequest& request, const ConverterContext& context);

} // namespace clockwork_logging::realtime_playback
