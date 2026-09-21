// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/deterministic_runner_setup.hh"

#include "clockwork/common/cog_gpu_assignment_config_clk_cc.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/writers/deterministic_log_writer.hh"
#include "clockwork/logging/writers/persistent_log_entry.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/deterministic_logging_config.hh"
#include "clockwork/tools/metrics_channel_metadata/metrics_channel_metadata_config_clk_cc.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/span.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <xxh3.h>

#include <initializer_list>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork
{

jewels::expected<clockwork_logging::ChannelMap, jewels::MonoError> convert_channel_map(
  jewels::memory::MemoryResource memory_resource, const scaffolding::ChannelMap& scaffolding_channel_map)
{
  clockwork_logging::ChannelMap publisher_channel_map(memory_resource);
  for (const auto& channel_pair : scaffolding_channel_map)
  {
    auto publisher_channel = std::dynamic_pointer_cast<pinion::ShmPublisher>(channel_pair.second);
    if (!publisher_channel)
    {
      jewels::log_cerr_error(
        "Got a non-publisher channel when running with the deterministic runner. Please make sure "
        "your system is single process.");
      return jewels::unexpected(jewels::MonoError{});
    }
    publisher_channel_map.emplace(channel_pair.first, std::move(publisher_channel));
  }

  return publisher_channel_map;
}

namespace
{

template <typename T>
clockwork_logging::PersistentLogEntry
make_tachyon_persistent_entry(std::string_view channel_name, std::shared_ptr<const T> data)
{
  using Traits = clockwork::LoggingTraits<T>;
  const auto data_bytes = std::as_bytes(jewels::as_single_item_span(*data));
  return clockwork_logging::PersistentLogEntry{
    .channel_name = std::string(channel_name),
    .schema_name = std::string(Traits::schema_name),
    .schema_encoding = static_cast<clockwork_logging::SchemaEncoding>(Traits::schema_encoding),
    .schema_definition = std::string(std::begin(Traits::schema_definition), std::end(Traits::schema_definition)),
    .data_owner = std::move(data),
    .data = data_bytes,
  };
}

std::pmr::vector<clockwork_logging::PersistentLogEntry>
build_persistent_entries(jewels::memory::MemoryResource memres, const DeterministicLoggingConfig& logging_config)
{
  std::pmr::vector<clockwork_logging::PersistentLogEntry> entries{memres};

  if (logging_config.metrics_channel_metadata_config)
  {
    const auto& config = *logging_config.metrics_channel_metadata_config;
    auto report = jewels::memory::make_pmr_shared<Tappy<clockwork::tools::MetricsChannelMetadataReport<>>>(memres);
    report->get_underlying_metrics_channels() = config.get_underlying_metrics_channels();
    entries.push_back(make_tachyon_persistent_entry(
      clockwork_logging::metrics_channel_metadata_channel_name,
      std::shared_ptr<const Tappy<clockwork::tools::MetricsChannelMetadataReport<>>>(std::move(report))));
  }

  if (logging_config.signal_metadata_config)
  {
    entries.push_back(make_tachyon_persistent_entry(
      clockwork_logging::signal_metadata_channel_name, logging_config.signal_metadata_config));
  }

  return entries;
}

} // namespace

jewels::expected<std::shared_ptr<DeterministicChannelHandler>, jewels::MonoError> setup_deterministic_log_writer(
  jewels::memory::MemoryResource memres,
  const ExecutionParams& execution_params,
  const DeterministicLoggingConfig& logging_config,
  const scaffolding::ChannelMap& scaffolding_channel_map,
  jewels::time::SyncTime init_time)
{
  if (
    (!execution_params.output_log_uri || !logging_config.log_writer_config) &&
    !execution_params.message_injectors.message_writer_)
  {
    return nullptr;
  }
  auto channel_map = convert_channel_map(memres, scaffolding_channel_map);
  if (!channel_map)
  {
    return jewels::unexpected(jewels::MonoError{});
  }

  std::optional<jewels::memory::NonNullSharedPtr<AbstractMessageWriter>> message_writer;
  if (execution_params.message_injectors.message_writer_)
  {
    message_writer = *execution_params.message_injectors.message_writer_;
  }
  else
  {
    message_writer = jewels::memory::allocate_shared<
      clockwork_logging::LogMessageWriter,
      std::pmr::polymorphic_allocator<clockwork_logging::LogMessageWriter>>(
      memres,
      memres,
      jewels::memory::make_non_null_from_ref(*logging_config.log_writer_config),
      build_persistent_entries(memres, logging_config),
      *channel_map,
      *execution_params.output_log_uri,
      init_time);
  }
  auto log_writer =
    std::allocate_shared<DeterministicChannelHandler, std::pmr::polymorphic_allocator<DeterministicChannelHandler>>(
      memres,
      memres,
      *message_writer,
      jewels::memory::make_non_null_from_ref(*logging_config.log_writer_config),
      *channel_map);

  auto log_writer_status = log_writer->initialize();
  if (!log_writer_status)
  {
    jewels::log_cerr_error("Error initializing log writer");
    return jewels::unexpected(jewels::MonoError{});
  }
  return log_writer;
}

jewels::expected<GpuAssignmentConfig, jewels::MonoError>
get_cog_gpu_assignment_config(const ExecutionParams& execution_params)
{
  if (execution_params.execution_mode == ExecutionMode::online)
  {
    return GpuAssignmentConfig{};
  }
  GpuAssignmentConfig cog_gpu_assignment_config;
  if (execution_params.cog_gpu_assignment_config_path)
  {
    auto cog_gpu_assignment_config_status =
      read_tachyon_config_to_heap<Tappy<CogGpuAssignmentConfig>>(*execution_params.cog_gpu_assignment_config_path);
    if (!cog_gpu_assignment_config_status)
    {
      return jewels::unexpected(jewels::MonoError{});
    }
    for (const auto& cog_gpu_assignment : (*cog_gpu_assignment_config_status)->get_cog_gpu_assignments())
    {
      auto result = cog_gpu_assignment_config.cog_uuid_to_gpu_id.emplace(
        cog_gpu_assignment.get_cog_uuid(), cog_gpu_assignment.get_gpu_id());
      if (!result.second)
      {
        jewels::log_cerr_error("A cog was assigned to a gpu multiple times: {}", cog_gpu_assignment.get_cog_uuid());
        return jewels::unexpected(jewels::MonoError{});
      }
    }
  }
  return cog_gpu_assignment_config;
}

} // namespace clockwork
