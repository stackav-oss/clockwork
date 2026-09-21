// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/realtime_playback/converter_request.hh"

#include "clockwork/logging/log_timestamp.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <tclap/ArgException.h>
#include <tclap/CmdLine.h>
#include <tclap/ValueArg.h>

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::realtime_playback
{
jewels::BinaryOutcome
parse_converter_request(jewels::Out<ConverterRequest> request_out, const std::span<const std::string_view> args)
{
  std::set<std::string_view> supplied_options;
  for (const auto arg : args)
  {
    if (arg.starts_with("--") && !supplied_options.emplace(arg).second)
    {
      jewels::log_cerr_error("Converter option supplied more than once: {}", arg);
      return jewels::failure;
    }
  }
  try
  {
    TCLAP::CmdLine command{"Convert an offboard log for real-time playback", ' ', "1.0", false};
    command.setExceptionHandling(false);
    const TCLAP::ValueArg<std::string> source_arg{"", "source", "Source log URI", true, "", "URI", command};
    const TCLAP::ValueArg<std::string> config_arg{
      "", "generated-config", "Generated conversion configuration", true, "", "path", command};
    const TCLAP::ValueArg<std::string> output_arg{
      "", "output", "New output bundle directory", true, "", "path", command};
    const TCLAP::ValueArg<int64_t> start_arg{
      "", "start-time-ns", "Inclusive publish-time interval start", false, 0, "nanoseconds", command};
    const TCLAP::ValueArg<int64_t> end_arg{
      "", "end-time-ns", "Inclusive publish-time interval end", false, 0, "nanoseconds", command};

    std::vector<std::string> argument_storage;
    argument_storage.reserve(args.size() + 1U);
    argument_storage.emplace_back("realtime_playback_converter");
    for (const auto arg : args)
    {
      argument_storage.emplace_back(arg);
    }
    std::vector<const char*> argument_pointers;
    argument_pointers.reserve(argument_storage.size());
    for (const auto& arg : argument_storage)
    {
      argument_pointers.emplace_back(arg.c_str());
    }
    command.parse(static_cast<int>(argument_pointers.size()), argument_pointers.data());

    if (start_arg.isSet() != end_arg.isSet())
    {
      jewels::log_cerr_error("--start-time-ns and --end-time-ns must be supplied together");
      return jewels::failure;
    }
    *request_out = ConverterRequest{
      .source_uri = source_arg.getValue(),
      .generated_config_path = config_arg.getValue(),
      .output_path = output_arg.getValue(),
      .start_time_ns = start_arg.isSet() ? std::optional{start_arg.getValue()} : std::nullopt,
      .end_time_ns = end_arg.isSet() ? std::optional{end_arg.getValue()} : std::nullopt,
    };
    return jewels::success;
  }
  catch (const TCLAP::ArgException& error)
  {
    jewels::log_cerr_error("Failed to parse converter arguments: {}", error.what());
    return jewels::failure;
  }
}

jewels::BinaryOutcome resolve_converter_interval(
  jewels::Out<LogInterval> interval_out,
  const ConverterRequest& request,
  const LogTimestamp source_start,
  const LogTimestamp source_end)
{
  if (source_end < source_start)
  {
    jewels::log_cerr_error("Source log has invalid time bounds");
    return jewels::failure;
  }
  if (!request.start_time_ns && !request.end_time_ns)
  {
    *interval_out = LogInterval{source_start, source_end};
    return jewels::success;
  }
  if (!request.start_time_ns || !request.end_time_ns || *request.end_time_ns < *request.start_time_ns)
  {
    jewels::log_cerr_error("Invalid absolute interval");
    return jewels::failure;
  }
  const LogTimestamp requested_start{*request.start_time_ns};
  const LogTimestamp requested_end{*request.end_time_ns};
  if (requested_start < source_start || source_end < requested_end)
  {
    jewels::log_cerr_error("Requested interval is outside the source log bounds");
    return jewels::failure;
  }
  *interval_out = LogInterval{requested_start, requested_end};
  return jewels::success;
}

} // namespace clockwork_logging::realtime_playback
