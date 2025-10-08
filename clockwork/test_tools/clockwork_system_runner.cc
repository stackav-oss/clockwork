// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/test_tools/clockwork_system_runner.hh"

#include "clockwork/common/exec_tools.hh"
#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/offboard_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/scaffolding.hh"
#include "jewels/cli/tests/support/simple_exit_condition.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/uuid/uuid.hh"

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory_resource>
#include <utility>

namespace clockwork
{
LogConfig::LogConfig(std::string_view uri, std::string_view config_path)
  : log_uri(uri), log_config_path(config_path)
{
}

jewels::expected<ClockworkSystemRunner, ClockworkSystemRunnerError> ClockworkSystemRunner::create(
  const MessageInjectorSystemRunnerConfig& config,
  const jewels::memory::NonNullSharedPtr<testing::MessageWriterContainer>& message_writer,
  const jewels::memory::NonNullSharedPtr<testing::SyntheticMessageFetcher>& message_fetcher)
{

  auto maybe_process_description = load_process_description(config.process_description_path);
  if (!maybe_process_description)
  {
    return jewels::unexpected(maybe_process_description.error());
  }

  ClockworkSystemRunner system_runner;
  system_runner.process_description_ = std::move(*maybe_process_description);
  system_runner.start_time_ = config.start_time;
  system_runner.end_time_ = config.end_time;
  system_runner.channel_publisher_config_ = config.channel_publisher_config;
  system_runner.log_writer_config_ = config.log_writer_config;
  system_runner.message_writer_ = message_writer;
  system_runner.message_fetcher_ = message_fetcher;

  return system_runner;
}

jewels::expected<ClockworkSystemRunner, ClockworkSystemRunnerError>
ClockworkSystemRunner::create(const ClockworkSystemRunnerConfig& runner_config)
{
  bool start_time_set = false;
  bool end_time_set = false;
  auto maybe_process_description = load_process_description(runner_config.process_description_path);
  if (!maybe_process_description)
  {
    return jewels::unexpected(maybe_process_description.error());
  }

  ClockworkSystemRunner system_runner;
  system_runner.process_description_ = std::move(*maybe_process_description);
  if (runner_config.input_log_config)
  {
    auto log_reader = clockwork_logging::OffboardLogReader(
      runner_config.input_log_config->log_uri, {}, {}, clockwork_logging::DecompressOption::decompress);
    if (!log_reader.open({}))
    {
      return jewels::unexpected(ClockworkSystemRunnerError::error_opening_log_reader);
    }
    auto maybe_log_start_time = log_reader.start_time();
    auto maybe_log_end_time = log_reader.end_time();
    if (!maybe_log_start_time || !maybe_log_end_time)
    {
      return jewels::unexpected(ClockworkSystemRunnerError::error_reading_input_log);
    }
    system_runner.input_log_uri_ = runner_config.input_log_config->log_uri;
    system_runner.channel_publisher_config_ = runner_config.input_log_config->log_config_path;

    system_runner.start_time_ = maybe_log_start_time->get_time();
    system_runner.end_time_ = maybe_log_end_time->get_time();
    start_time_set = true;
    end_time_set = true;
  }

  if (runner_config.output_log_config)
  {
    system_runner.log_writer_config_ = runner_config.output_log_config->log_config_path;
    system_runner.output_log_uri_ = runner_config.output_log_config->log_uri;
  }

  system_runner.metrics_channel_metadata_config_ = runner_config.metrics_channel_metadata_config_path;

  if (runner_config.start_time)
  {
    system_runner.start_time_ = *runner_config.start_time;
    start_time_set = true;
  }
  if (runner_config.end_time)
  {
    system_runner.end_time_ = *runner_config.end_time;
    end_time_set = true;
  }
  if (!start_time_set || !end_time_set)
  {
    jewels::log_cerr_error("If an input log is not specified than a start and end time must be");
    return jewels::unexpected(ClockworkSystemRunnerError::error_missing_start_or_end_time);
  }

  return system_runner;
}

jewels::expected<void, ClockworkSystemRunnerError> ClockworkSystemRunner::run()
{
  auto memres_channels = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  auto memres_casing = jewels::memory::MemoryResource(std::pmr::get_default_resource());

  auto casing = scaffolding::make_casing(memres_casing);
  if (!casing)
  {
    return jewels::unexpected(ClockworkSystemRunnerError::error_making_casing);
  }
  // Unique socket namespace for every test
  auto sock_ns = jewels::Uuid<int>::random_uuid().to_string();

  auto channel_factory =
    pinion::ShmChannelFactory::make(memres_channels, sock_ns, (test_dir_.get_path() / shm_dir_).string());

  if (!channel_factory)
  {
    return jewels::unexpected(ClockworkSystemRunnerError::error_making_channel_factory);
  }
  auto execution_params = ExecutionParams{
    .execution_mode = ExecutionMode::deterministic,
    .start_time = start_time_,
    .end_time = end_time_,
    .input_log_uri = input_log_uri_,
    .output_log_uri = output_log_uri_,
    .channel_publisher_config_path = channel_publisher_config_,
    .log_writer_config_path = log_writer_config_,
    .metrics_channel_metadata_config_path = metrics_channel_metadata_config_,
    .message_injectors = MessageInjectors{.message_writer_ = message_writer_, .message_fetcher_ = message_fetcher_}};
  jewels::cli::SimpleExitCondition exit;
  auto result =
    scaffolding::run_deterministic(*process_description_, *casing, *channel_factory, exit, execution_params);
  if (result == EXIT_FAILURE)
  {
    return jewels::unexpected(ClockworkSystemRunnerError::error_running);
  }
  return {};
}

jewels::expected<std::shared_ptr<common::ProcessDescriptionTap>, ClockworkSystemRunnerError>
ClockworkSystemRunner::load_process_description(std::string_view process_description_path)
{
  auto maybe_process_desription = read_tachyon_config_to_heap<common::ProcessDescriptionTap>(process_description_path);
  if (!maybe_process_desription)
  {
    jewels::log_cerr_error("Error loading process description");
    return jewels::unexpected(ClockworkSystemRunnerError::error_reading_process_description);
  }
  return *maybe_process_desription;
}

jewels::expected<MessageInjectorSystemRunner, ClockworkSystemRunnerError> MessageInjectorSystemRunner::create(
  const MessageInjectorSystemRunnerConfig& config, jewels::memory::MemoryResource memres)
{
  auto message_fetcher = jewels::memory::make_shared<testing::SyntheticMessageFetcher>(memres);
  auto message_container = jewels::memory::make_shared<testing::MessageWriterContainer>(memres);

  auto maybe_system_runner = ClockworkSystemRunner::create(config, message_container, message_fetcher);
  if (!maybe_system_runner)
  {
    return jewels::unexpected(maybe_system_runner.error());
  }
  auto message_injector_runner =
    MessageInjectorSystemRunner(config.start_time, config.end_time, message_container, message_fetcher);

  message_injector_runner.system_runner_ = std::move(*maybe_system_runner);
  return message_injector_runner;
}
jewels::expected<void, ClockworkSystemRunnerError> MessageInjectorSystemRunner::run()
{
  return system_runner_.run();
}

std::optional<MultiMessageInfoData> MessageInjectorSystemRunner::try_pop_message()
{
  return message_writer_->try_pop_message();
}

MessageInjectorSystemRunner::MessageInjectorSystemRunner(
  jewels::time::SyncTime start_time,
  jewels::time::SyncTime end_time,
  const jewels::memory::NonNullSharedPtr<testing::MessageWriterContainer>& message_writer,
  const jewels::memory::NonNullSharedPtr<testing::SyntheticMessageFetcher>& message_fetcher)
  : start_time_(start_time), end_time_(end_time), message_writer_(message_writer), message_fetcher_(message_fetcher)
{
}

} // namespace clockwork
