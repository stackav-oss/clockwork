// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/log_metadata_recovery_helper.hh"

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/log_file_reader.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/reader_types.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

using jewels::Out;

LogMetadataRecoveryHelper::LogMetadataRecoveryHelper(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)),
    log_file_metadata_map_(memory_resource_),
    channels_(memory_resource_),
    persistent_channels_(memory_resource_)
{
}

LogOutcome LogMetadataRecoveryHelper::initialize(
  const LogUri& metadata_file_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  ChunkReaderWriterFactory<>& chunk_reader_factory)
{
  const auto& log_uri_str = metadata_file_uri.string();
  const auto chunk_compressor_ptr =
    jewels::memory::allocate_shared<ChunkCompressor, std::pmr::polymorphic_allocator<ChunkCompressor>>(
      memory_resource_, memory_resource_);
  auto readdir_result = chunk_reader_factory.list_log_files(log_uri_str);
  if (!readdir_result)
  {
    jewels::log_cerr_error("Failed to list log files under {}: {}", log_uri_str, readdir_result.error());
    return readdir_result.error();
  }
  if (readdir_result->empty())
  {
    jewels::log_cerr_error("No log files found under {}: not a log", log_uri_str);
    return LogError::not_a_log;
  }
  const LogInterval empty_log_interval{LogTimestamp{0}, LogTimestamp{0}};
  LogTimestamp min_transmit_time{std::numeric_limits<int64_t>::max()};
  LogTimestamp max_transmit_time{std::numeric_limits<int64_t>::min()};
  for (const auto& log_file_uri : readdir_result.value())
  {
    auto reader_result = chunk_reader_factory.make_chunk_reader(log_file_uri.string());
    if (!reader_result)
    {
      jewels::log_cerr_error("Failed to create chunk reader for {}: {}", log_file_uri.string(), reader_result.error());
      continue;
    }
    if (const auto open_result = reader_result.value()->open(); !open_result)
    {
      jewels::log_cerr_error("Failed to open chunk reader for {}: {}", log_file_uri.string(), open_result.error());
      continue;
    }
    LogFileReader log_file_reader{memory_resource_, std::move(reader_result).value(), chunk_compressor_ptr, {}};
    const auto metadata_result = log_file_reader.get_metadata();
    if (!metadata_result)
    {
      jewels::log_cerr_error("Failed to get metadata for {}: {}", log_file_uri.string(), metadata_result.error());
      continue;
    }
    const auto& metadata_map = *metadata_result.value();
    const auto metrics_result = log_file_reader.get_metrics();
    if (!metrics_result)
    {
      jewels::log_cerr_error("Failed to get metrics for {}: {}", log_file_uri.string(), metrics_result.error());
      continue;
    }
    const auto& metrics = *metrics_result.value();
    std::pmr::unordered_set<std::pmr::string> channels{memory_resource_};
    std::pmr::unordered_set<std::pmr::string> persistent_channels{memory_resource_};
    for (const auto& metadata : std::views::values(metadata_map))
    {
      if (!excluded_channels.contains(metadata.channel_name))
      {
        channels_.insert(std::pmr::string{metadata.channel_name, memory_resource_});
        channels.insert(std::pmr::string{metadata.channel_name, memory_resource_});
        if (metadata.channel_type == ChannelType::persistent)
        {
          persistent_channels_.insert(std::pmr::string{metadata.channel_name, memory_resource_});
          persistent_channels.insert(std::pmr::string{metadata.channel_name, memory_resource_});
        }
      }
    }
    if (!channels.empty())
    {
      log_file_metadata_map_.emplace(
        std::pmr::string{log_file_uri.filename(), memory_resource_},
        LogFileMetadataMapEntry{
          .channels = std::move(channels),
          .persistent_channels = std::move(persistent_channels),
          .transmit_time_interval = metrics.transmit_time_interval,
        });
      all_log_files_.emplace_back(log_file_uri);
    }
    if (metrics.transmit_time_interval != empty_log_interval)
    {
      min_transmit_time = std::min(metrics.transmit_time_interval.get_start_timestamp(), min_transmit_time);
      max_transmit_time = std::max(metrics.transmit_time_interval.get_end_timestamp(), max_transmit_time);
    }
  }
  transmit_time_interval_ =
    min_transmit_time <= max_transmit_time ? LogInterval{min_transmit_time, max_transmit_time} : empty_log_interval;
  jewels::log_cerr_warn("Recovered missing metadata in {}", log_uri_str);
  return LogError::success;
}

LogOutcome LogMetadataRecoveryHelper::get_log_file_map(
  Out<std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>>> log_file_map,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  const std::optional<LogInterval>& maybe_transmit_time_interval,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_excluded_channels) const
{
  *log_file_map =
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>>{memory_resource_};
  std::pmr::unordered_set<std::pmr::string> desired_persistent_channels{memory_resource_};
  for (const auto& channel : persistent_channels_)
  {
    if (!maybe_desired_channels || maybe_desired_channels->contains(channel))
    {
      desired_persistent_channels.insert(channel);
    }
  }
  for (const auto& file_uri : all_log_files_)
  {
    std::pmr::string filename_str{file_uri.filename(), memory_resource_};
    const auto map_iter = log_file_metadata_map_.find(filename_str);
    if (map_iter == log_file_metadata_map_.end())
    {
      jewels::log_cerr_warn("File {} not found in the log metadata: ignoring", filename_str);
      continue;
    }
    const auto& map_entry = map_iter->second;
    std::pmr::unordered_set<std::pmr::string> channel_name_set{memory_resource_};
    for (const auto& channel_name : map_entry.channels)
    {
      if (
        map_entry.channels.contains(channel_name) &&
        (!maybe_desired_channels || maybe_desired_channels->contains(channel_name)) &&
        (!maybe_excluded_channels || !maybe_excluded_channels->contains(channel_name)))
      {
        channel_name_set.emplace(channel_name);
      }
    }
    if (channel_name_set.empty())
    {
      continue;
    }
    if (maybe_transmit_time_interval)
    {
      if (
        maybe_transmit_time_interval->overlaps(map_entry.transmit_time_interval) ||
        (std::ranges::any_of(
           desired_persistent_channels,
           [&channel_name_set](const auto& channel_name) { return channel_name_set.contains(channel_name); }) &&
         map_entry.transmit_time_interval.get_start_timestamp() < maybe_transmit_time_interval->get_start_timestamp()))
      {
        log_file_map->emplace(file_uri.string(), std::move(channel_name_set));
      }
    }
    else
    {
      log_file_map->emplace(file_uri.string(), std::move(channel_name_set));
    }
  }
  return LogError::success;
}

[[nodiscard]] LogInterval LogMetadataRecoveryHelper::get_transmit_time_interval() const
{
  return transmit_time_interval_;
}

[[nodiscard]] const std::pmr::unordered_set<std::pmr::string>& LogMetadataRecoveryHelper::get_channels() const
{
  return channels_;
}

[[nodiscard]] const std::pmr::unordered_set<std::pmr::string>&
LogMetadataRecoveryHelper::get_persistent_channels() const
{
  return persistent_channels_;
}

LogOutcome make_log_metadata_recovery_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  ChunkReaderWriterFactory<>& chunk_reader_factory,
  Out<std::shared_ptr<LogMetadataHelperInterface>> helper_ptr)
{
  *helper_ptr = jewels::memory::make_pmr_shared<LogMetadataRecoveryHelper>(memory_resource, memory_resource);
  return (*helper_ptr)->initialize(log_uri, excluded_channels, chunk_reader_factory);
}

} // namespace clockwork_logging::offboard
