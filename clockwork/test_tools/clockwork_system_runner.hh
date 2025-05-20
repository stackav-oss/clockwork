// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/process_description.hh"
#include "clockwork/runners/channel_publisher.hh"
#include "clockwork/test_tools/synthetic_message_fetcher.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <wise_enum.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace clockwork
{

// Log Configuration for the input and output logs.
struct LogConfig
{
  LogConfig(std::string_view uri, std::string_view config_path);
  std::string log_uri;
  std::string log_config_path;
};

// Configuration for the Clockwork System Runner. Note that if no input log config is provided the start and end times
// must be set.
struct ClockworkSystemRunnerConfig
{
  std::string process_description_path;
  std::optional<jewels::time::SyncTime> start_time{};
  std::optional<jewels::time::SyncTime> end_time{};
  std::optional<LogConfig> input_log_config{};
  std::optional<LogConfig> output_log_config{};
};

WISE_ENUM_CLASS(
  (ClockworkSystemRunnerError, uint8_t),
  error_reading_process_description,
  error_reading_input_log,
  error_reading_log_publisher_config,
  error_opening_log_reader,
  error_making_casing,
  error_making_channel_factory,
  error_running,
  error_missing_start_or_end_time);

struct MessageInjectorSystemRunnerConfig
{
  std::string process_description_path;
  jewels::time::SyncTime start_time;
  jewels::time::SyncTime end_time;
  /// Path to the log publisher config
  std::string log_publisher_config_;

  /// Path to the log writer config
  std::string log_writer_config_;
};

class ClockworkSystemRunner
{
public:
  ///
  /// Build the clockwork system runner
  /// @param runner_config The configuration of the runner.
  /// @return Either the instantiated clockwork system runner, or an error.
  static jewels::expected<ClockworkSystemRunner, ClockworkSystemRunnerError>
  create(const ClockworkSystemRunnerConfig& runner_config);

  static jewels::expected<ClockworkSystemRunner, ClockworkSystemRunnerError> create(
    const MessageInjectorSystemRunnerConfig& config,
    const jewels::memory::NonNullSharedPtr<testing::MessageWriterContainer>& message_writer,
    const jewels::memory::NonNullSharedPtr<testing::SyntheticMessageFetcher>& message_fetcher);
  ///
  /// Run the clockwork system.
  /// @return Whether the system was able to run successfully.
  jewels::expected<void, ClockworkSystemRunnerError> run();

private:
  static jewels::expected<std::shared_ptr<common::ProcessDescriptionTap>, ClockworkSystemRunnerError>
  load_process_description(std::string_view process_description_path);

  /// Simulation start time.
  jewels::time::SyncTime start_time_;

  /// Simulation end time
  jewels::time::SyncTime end_time_;

  /// The process description
  std::shared_ptr<common::ProcessDescriptionTap> process_description_;

  /// Path to the log publisher config
  std::optional<std::string> log_publisher_config_;

  /// Path to the log writer config
  std::optional<std::string> log_writer_config_;

  /// Input log uri
  std::optional<std::string> input_log_uri_;

  /// Output log uri
  std::optional<std::string> output_log_uri_;

  /// Message writer
  std::optional<jewels::memory::NonNullSharedPtr<testing::MessageWriterContainer>> message_writer_;

  /// Message fetcher
  std::optional<jewels::memory::NonNullSharedPtr<testing::SyntheticMessageFetcher>> message_fetcher_;

  /// The tmp directory to use
  jewels::testing::TmpDirectoryGuard test_dir_;

  /// The shm directory to use
  std::string shm_dir_ = "shm";
};

/// Class that wraps a clockwork system runner. Intended to be used when working purely with synthetic data.
class MessageInjectorSystemRunner
{

public:
  /// Factory function for creating the MessageInjectorSystemRunner
  /// @param config The configuration for the message injector system runner
  /// @param memres The underlying memory resource
  /// @return jewels::expected<MessageInjectorSystemRunner, ClockworkSystemRunnerError>
  static jewels::expected<MessageInjectorSystemRunner, ClockworkSystemRunnerError>
  create(const MessageInjectorSystemRunnerConfig& config, jewels::memory::MemoryResource memres);

  /// Run the underlying clockwork system.
  jewels::expected<void, ClockworkSystemRunnerError> run();

  /// Add a tap/tachyon message to be played out in the underlying system runner.
  /// @tparam MessageType Tap message type.
  /// @param message The message to be played out.
  /// @param publish_time The time to publish this message.
  /// @param channel_name The channel to publish this message to.
  template <typename MessageType>
  void add_message(const MessageType& message, jewels::time::SyncTime publish_time, std::string_view channel_name);

  /// Get the next message that was recorded after running the system runner.
  /// @return std::optional<MultiMessageInfoData> The message info of the next message recorded, this is fifo. Return
  /// nullopt if there are none to return.
  std::optional<MultiMessageInfoData> try_pop_message();

private:
  MessageInjectorSystemRunner(
    jewels::time::SyncTime start_time,
    jewels::time::SyncTime end_time,
    const jewels::memory::NonNullSharedPtr<testing::MessageWriterContainer>& message_writer,
    const jewels::memory::NonNullSharedPtr<testing::SyntheticMessageFetcher>& message_fetcher);

  /// Simulation start time.
  jewels::time::SyncTime start_time_;

  /// Simulation end time.
  jewels::time::SyncTime end_time_;

  /// Message writer.
  jewels::memory::NonNullSharedPtr<testing::MessageWriterContainer> message_writer_;

  /// Message fetcher.
  jewels::memory::NonNullSharedPtr<testing::SyntheticMessageFetcher> message_fetcher_;

  /// The underlying system runner
  ClockworkSystemRunner system_runner_;
};
} // namespace clockwork

#include "clockwork/test_tools/clockwork_system_runner.inl"
