// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/exec_tools.hh"

#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/meta_channel_factory.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/std/expected.hh"

#include <array>
#include <chrono>
#include <functional>
#include <memory_resource>
#include <string>
#include <utility>

namespace clockwork
{
namespace
{
constexpr pinion::AbstractChannel::ResumeBehavior resume_default = pinion::AbstractChannel::ResumeBehavior::no_resume;

std::vector<std::string> get_resume_strings_vector()
{
  std::vector<std::string> names;
  for (auto enum_item : wise_enum::range<pinion::AbstractChannel::ResumeBehavior>)
  {
    names.emplace_back(enum_item.name);
  }
  return names;
}
} // namespace

PinionArgs::PinionArgs(jewels::memory::MemoryResource memres, TCLAP::ArgContainer& parser)
  : memres_(std::move(memres)),
    arg_pinion_dir_("", "pinion-dir", "Root path for pinion channel buffers", false, "/dev/shm", "string", parser),
    arg_pinion_ns_("", "pinion-ns", "Namespace for pinion channels/sockets", false, "", "string", parser),
    resume_strings_(get_resume_strings_vector()),
    arg_resume_constraint_(resume_strings_),
    arg_resume_(
      "",
      "pinion-resume",
      "Sets pinion channel resume behavior",
      false,
      std::string(wise_enum::to_string(resume_default)),
      &arg_resume_constraint_,
      parser)
{
}

pinion::AbstractChannel::ResumeBehavior PinionArgs::resume() const
{
  auto requested_resume_maybe = wise_enum::from_string<pinion::AbstractChannel::ResumeBehavior>(arg_resume_.getValue());
  auto resume = requested_resume_maybe.value_or(resume_default);
  // We know the second condition must be true if the first is, but clang-tidy can't see that so check it explicitly.
  if (resume != resume_default && requested_resume_maybe.has_value())
  {
    jewels::log_cerr_warn(
      "{} flag set to nondefault: \"{}\".", arg_resume_.longID(), wise_enum::to_string(requested_resume_maybe.value()));
  }
  return resume;
}

jewels::expected<std::shared_ptr<pinion::AbstractChannelFactory>, jewels::MonoError>
PinionArgs::make_factory(const Tappy<common::ProcessDescription<>>& config) const
{
  return pinion::MetaChannelFactory::make(
           memres_, config, arg_pinion_ns_.getValue(), arg_pinion_dir_.getValue(), resume())
    .transform([this](pinion::MetaChannelFactory&& factory)
               { return jewels::memory::make_pmr_shared<pinion::MetaChannelFactory>(memres_, std::move(factory)); });
}

jewels::expected<std::shared_ptr<pinion::AbstractChannelFactory>, jewels::MonoError> PinionArgs::make_factory(
  std::pmr::unordered_map<std::pmr::string, pinion::ChannelType> channel_types,
  std::pmr::unordered_map<std::pmr::string, std::pmr::vector<std::pmr::string>> publisher_keys,
  std::pmr::unordered_map<std::pmr::string, std::pmr::string> subscriber_keys) const
{
  return pinion::MetaChannelFactory::make(
           memres_,
           std::move(channel_types),
           std::move(publisher_keys),
           std::move(subscriber_keys),
           arg_pinion_ns_.getValue(),
           arg_pinion_dir_.getValue(),
           resume())
    .transform([this](pinion::MetaChannelFactory&& factory)
               { return jewels::memory::make_pmr_shared<pinion::MetaChannelFactory>(memres_, std::move(factory)); });
}

ExecutionArgs::ExecutionArgs(TCLAP::ArgContainer& parser)
  : deterministic_runner_("", "deterministic-runner", "Use the deterministic runner", parser, false),
    playback_speed_(
      "",
      "playback-speed",
      "Playback speed multiplier (1.0 = real-time, 0.5 = half speed, 2.0 = double speed, 0.0 = unlimited)",
      false,
      0.0,
      "double",
      parser),
    input_log_uri_("", "input-log-uri", "input log file uri for log publisher", false, "", "string", parser),
    output_log_uri_("", "output-log-uri", "Simulation output log file uri", false, "", "string", parser),
    channel_publisher_config_path_(
      "", "channel-publisher-config", "Path to the channel publisher config file", false, "", "string", parser),
    log_writer_config_path_("", "log-writer-config", "Path to the log writer config file", false, "", "string", parser),
    start_time_ns_("", "sim-start-time-ns", "Start time of simulation in nanoseconds", false, 0U, "uint64_t", parser),
    end_time_ns_("", "sim-end-time-ns", "End time of simulation in nanoseconds", false, 0U, "uint64_t", parser),
    cog_gpu_assignment_config_path_(
      "", "cog-gpu-config", "Path to the cog gpu assignment config file", false, "", "string", parser),
    metrics_channel_metadata_config_path_(
      "",
      "metrics-channel-metadata-config",
      "Path to the metrics channel metadata config file",
      false,
      "",
      "string",
      parser),
    signal_metadata_config_path_(
      "", "signal-metadata-config", "Path to the signal metadata config file", false, "", "string", parser),
    suppress_schema_mismatch_errors_(
      "", "suppress-schema-mismatch-errors", "Suppress errors for schema mismatch", parser, false)
{
}

jewels::expected<ExecutionParams, jewels::MonoError> ExecutionArgs::make_execution_params() const
{
  auto execution_params = ExecutionParams{
    .execution_mode = (deterministic_runner_.getValue() ? ExecutionMode::deterministic : ExecutionMode::online),
  };

  if (playback_speed_.isSet())
  {
    execution_params.playback_speed.emplace(playback_speed_.getValue());
  }

  if (start_time_ns_.isSet())
  {
    execution_params.start_time.emplace(jewels::time::SyncTime(std::chrono::nanoseconds(start_time_ns_.getValue())));
  }
  if (end_time_ns_.isSet())
  {
    execution_params.end_time.emplace(jewels::time::SyncTime(std::chrono::nanoseconds(end_time_ns_.getValue())));
  }

  if (input_log_uri_.isSet() && channel_publisher_config_path_.isSet())
  {
    execution_params.input_log_uri.emplace(input_log_uri_.getValue());
    execution_params.channel_publisher_config_path.emplace(channel_publisher_config_path_.getValue());
  }
  else if (input_log_uri_.isSet() || channel_publisher_config_path_.isSet())
  {
    jewels::log_cerr_error("If an input log uri is set a log publisher config path must also be.");
    return jewels::unexpected(jewels::MonoError{});
  }

  if (output_log_uri_.isSet() && log_writer_config_path_.isSet())
  {
    execution_params.output_log_uri.emplace(output_log_uri_.getValue());
    execution_params.log_writer_config_path.emplace(log_writer_config_path_.getValue());
  }
  else if (output_log_uri_.isSet() || log_writer_config_path_.isSet())
  {
    jewels::log_cerr_error("If an output log uri is set a log writer config path must also be.");
    return jewels::unexpected(jewels::MonoError{});
  }

  if (cog_gpu_assignment_config_path_.isSet())
  {
    execution_params.cog_gpu_assignment_config_path.emplace(cog_gpu_assignment_config_path_.getValue());
  }
  if (metrics_channel_metadata_config_path_.isSet())
  {
    execution_params.metrics_channel_metadata_config_path.emplace(metrics_channel_metadata_config_path_.getValue());
  }
  if (signal_metadata_config_path_.isSet())
  {
    execution_params.signal_metadata_config_path.emplace(signal_metadata_config_path_.getValue());
  }

  execution_params.suppress_schema_mismatch_errors.emplace(suppress_schema_mismatch_errors_.getValue());

  return execution_params;
}

} // namespace clockwork
