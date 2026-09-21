// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace clockwork_logging::realtime_playback
{

/// Parsed converter command-line request.
struct ConverterRequest
{
  std::string source_uri;
  std::string generated_config_path;
  std::string output_path;
  std::optional<int64_t> start_time_ns;
  std::optional<int64_t> end_time_ns;
};

/// Parse the converter command-line arguments.
/// The program name must not be present in args.
jewels::BinaryOutcome
parse_converter_request(jewels::Out<ConverterRequest> request_out, std::span<const std::string_view> args);

/// Resolve the request to an inclusive interval within the source bounds.
jewels::BinaryOutcome resolve_converter_interval(
  jewels::Out<LogInterval> interval_out,
  const ConverterRequest& request,
  LogTimestamp source_start,
  LogTimestamp source_end);

} // namespace clockwork_logging::realtime_playback
