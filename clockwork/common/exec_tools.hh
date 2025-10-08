// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/runners/channel_publisher.hh"
#include "clockwork/runners/deterministic_channel_handler.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/meta/concepts.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <tclap/ArgContainer.h>
#include <tclap/SwitchArg.h>
#include <tclap/ValueArg.h>
#include <tclap/ValuesConstraint.h>
#include <wise_enum.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork
{
class PinionArgs
{
public:
  PinionArgs(jewels::memory::MemoryResource memres, TCLAP::ArgContainer& parser);

  [[nodiscard]] jewels::expected<pinion::ShmChannelFactory, jewels::MonoError> make_factory() const;

private:
  jewels::memory::MemoryResource memres_;
  TCLAP::ValueArg<std::string> arg_pinion_dir_;
  TCLAP::ValueArg<std::string> arg_pinion_ns_;
  std::vector<std::string> resume_strings_;
  TCLAP::ValuesConstraint<std::string> arg_resume_constraint_;
  TCLAP::ValueArg<std::string> arg_resume_;
};

using MessageWriterSharedPtr = jewels::memory::NonNullSharedPtr<AbstractMessageWriter>;
using MessageFetcherSharedPtr = jewels::memory::NonNullSharedPtr<MessageFetcher>;

struct MessageInjectors
{
  std::optional<MessageWriterSharedPtr> message_writer_;
  std::optional<MessageFetcherSharedPtr> message_fetcher_;
};

WISE_ENUM_CLASS((ExecutionMode, uint8_t), online, deterministic);
struct ExecutionParams
{
  ExecutionMode execution_mode = ExecutionMode::online;
  std::optional<jewels::time::SyncTime> start_time{};
  std::optional<jewels::time::SyncTime> end_time{};
  std::optional<std::string> input_log_uri{};
  std::optional<std::string> output_log_uri{};
  std::optional<std::string> channel_publisher_config_path{};
  std::optional<std::string> log_writer_config_path{};
  std::optional<std::string> cog_gpu_assignment_config_path{};
  std::optional<bool> suppress_schema_mismatch_errors{};
  std::optional<std::string> metrics_channel_metadata_config_path{};
  MessageInjectors message_injectors{};
};

class ExecutionArgs
{
public:
  explicit ExecutionArgs(TCLAP::ArgContainer& parser);

  [[nodiscard]] jewels::expected<ExecutionParams, jewels::MonoError> make_execution_params() const;

private:
  TCLAP::SwitchArg deterministic_runner_;
  TCLAP::ValueArg<std::string> input_log_uri_;
  TCLAP::ValueArg<std::string> output_log_uri_;
  TCLAP::ValueArg<std::string> channel_publisher_config_path_;
  TCLAP::ValueArg<std::string> log_writer_config_path_;
  TCLAP::ValueArg<uint64_t> start_time_ns_;
  TCLAP::ValueArg<uint64_t> end_time_ns_;
  TCLAP::ValueArg<std::string> cog_gpu_assignment_config_path_;
  TCLAP::ValueArg<std::string> metrics_channel_metadata_config_path_;
  TCLAP::SwitchArg suppress_schema_mismatch_errors_;
};

///
/// Attempts to read a Tappy<> type from the file at `path`, logging and returning an error if one is encountered
///
template <jewels::meta::ImplicitLifetimeType T>
jewels::expected<T, jewels::MonoError> read_tachyon_config(std::string_view file_path);

///
/// Attempts to read a Tappy<> type from the file at `path`, logging and returning an error if one is
/// encountered. Stores the result in a shared ptr on the heap in case the object is too large to go on the stack.
///
template <jewels::meta::ImplicitLifetimeType T>
jewels::expected<std::shared_ptr<T>, jewels::MonoError> read_tachyon_config_to_heap(std::string_view file_path);

} // namespace clockwork

#include "clockwork/common/exec_tools.inl"
