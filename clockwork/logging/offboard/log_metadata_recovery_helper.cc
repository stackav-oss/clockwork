// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/log_metadata_recovery_helper.hh"

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/log_file_reader.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/reader_types.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

LogMetadataRecoveryHelper::LogMetadataRecoveryHelper(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)),
    log_file_metadata_map_(memory_resource_),
    channels_(memory_resource_),
    persistent_channels_(memory_resource_)
{
}

[[nodiscard]] LogExpected<void>
LogMetadataRecoveryHelper::initialize(const LogUri& metadata_file_uri, ChunkReaderWriterFactory<>& chunk_reader_factory)
{
  const auto& log_uri_str = metadata_file_uri.string();
  const auto chunk_compressor_ptr =
    jewels::memory::allocate_shared<ChunkCompressor, std::pmr::polymorphic_allocator<ChunkCompressor>>(
      memory_resource_, memory_resource_);
  auto readdir_result = chunk_reader_factory.list_log_files(log_uri_str);
  if (!readdir_result)
  {
    jewels::log_cerr_error("Failed to list log files under {}: {}", log_uri_str, readdir_result.error());
    return jewels::unexpected(readdir_result.error());
  }
  const LogInterval empty_log_interval{LogTimestamp{0}, LogTimestamp{0}};
  LogTimestamp min_transmit_time{std::numeric_limits<int64_t>::max()};
  LogTimestamp max_transmit_time{std::numeric_limits<int64_t>::min()};
  for (const auto& log_file : readdir_result.value())
  {
    const auto uri_result = LogUri::try_make(log_file, memory_resource_);
    if (!uri_result)
    {
      jewels::log_cerr_error("Failed to process metadata for {}: invalid URI", log_file);
      continue;
    }
    const auto& log_file_uri = uri_result.value();
    auto reader_result = chunk_reader_factory.make_chunk_reader(log_file);
    if (!reader_result)
    {
      jewels::log_cerr_error("Failed to create chunk reader for {}: {}", log_file, reader_result.error());
      continue;
    }
    if (const auto open_result = reader_result.value()->open(); !open_result)
    {
      jewels::log_cerr_error("Failed to open chunk reader for {}: {}", log_file, open_result.error());
      continue;
    }
    LogFileReader log_file_reader{memory_resource_, std::move(reader_result).value(), chunk_compressor_ptr};
    const auto metadata_result = log_file_reader.get_metadata();
    if (!metadata_result)
    {
      jewels::log_cerr_error("Failed to get metadata for {}: {}", log_file, metadata_result.error());
      continue;
    }
    const auto& metadata_map = *metadata_result.value();
    const auto metrics_result = log_file_reader.get_metrics();
    if (!metrics_result)
    {
      jewels::log_cerr_error("Failed to get metrics for {}: {}", log_file, metrics_result.error());
      continue;
    }
    const auto& metrics = *metrics_result.value();
    std::pmr::unordered_set<std::pmr::string> channels{memory_resource_};
    std::pmr::unordered_set<std::pmr::string> persistent_channels{memory_resource_};
    for (const auto& metadata : std::views::values(metadata_map))
    {
      channels_.insert(std::pmr::string{metadata.channel_name, memory_resource_});
      channels.insert(std::pmr::string{metadata.channel_name, memory_resource_});
      if (metadata.channel_type == ChannelType::persistent)
      {
        persistent_channels_.insert(std::pmr::string{metadata.channel_name, memory_resource_});
        persistent_channels.insert(std::pmr::string{metadata.channel_name, memory_resource_});
      }
    }
    log_file_metadata_map_.emplace(
      std::pmr::string{log_file_uri.filename(), memory_resource_},
      LogFileMetadataMapEntry{
        .channels = std::move(channels),
        .persistent_channels = std::move(persistent_channels),
        .transmit_time_interval = metrics.transmit_time_interval,
      });
    if (metrics.transmit_time_interval != empty_log_interval)
    {
      min_transmit_time = std::min(metrics.transmit_time_interval.get_start_timestamp(), min_transmit_time);
      max_transmit_time = std::max(metrics.transmit_time_interval.get_end_timestamp(), max_transmit_time);
    }
    all_log_files_.emplace_back(log_file);
  }
  if (all_log_files_.empty())
  {
    jewels::log_cerr_error("No log files found under {}: not a log", log_uri_str);
    return jewels::unexpected(LogError::not_a_log);
  }
  transmit_time_interval_ =
    min_transmit_time <= max_transmit_time ? LogInterval{min_transmit_time, max_transmit_time} : empty_log_interval;
  jewels::log_cerr_warn("Recovered missing metadata in {}", log_uri_str);
  return {};
}

[[nodiscard]] LogExpected<std::pmr::vector<std::pmr::string>> LogMetadataRecoveryHelper::list_log_files(
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  const std::optional<LogInterval>& maybe_transmit_time_interval) const
{
  std::pmr::unordered_set<std::pmr::string> desired_persistent_channels{memory_resource_};
  for (const auto& channel : persistent_channels_)
  {
    if (!maybe_desired_channels || maybe_desired_channels->contains(channel))
    {
      desired_persistent_channels.insert(channel);
    }
  }
  std::pmr::vector<std::pmr::string> filtered_log_files{memory_resource_};
  filtered_log_files.reserve(all_log_files_.size());
  for (const auto& file_uri_str : all_log_files_)
  {
    const auto uri_result = LogUri::try_make(file_uri_str, memory_resource_);
    if (!uri_result)
    {
      jewels::log_cerr_error("Invalid URI: {}", file_uri_str);
      return jewels::unexpected(LogError::invalid_log_uri);
    }
    const auto& file_uri = uri_result.value();
    std::pmr::string filename_str{file_uri.filename(), memory_resource_};
    const auto map_iter = log_file_metadata_map_.find(filename_str);
    if (map_iter == log_file_metadata_map_.end())
    {
      jewels::log_cerr_warn("File {} not found in the log metadata: ignoring", filename_str);
      continue;
    }
    const auto& map_entry = map_iter->second;
    if (
      maybe_desired_channels &&
      !std::ranges::any_of(
        maybe_desired_channels.value(),
        [&map_entry](const auto& channel_name) { return map_entry.channels.contains(channel_name); }))
    {
      continue;
    }
    if (maybe_transmit_time_interval)
    {
      if (
        maybe_transmit_time_interval->overlaps(map_entry.transmit_time_interval) ||
        (std::ranges::any_of(
           desired_persistent_channels,
           [&map_entry](const auto& channel_name) { return map_entry.channels.contains(channel_name); }) &&
         map_entry.transmit_time_interval.get_start_timestamp() < maybe_transmit_time_interval->get_start_timestamp()))
      {
        filtered_log_files.push_back(file_uri_str);
      }
    }
    else
    {
      filtered_log_files.push_back(file_uri_str);
    }
  }
  return {std::move(filtered_log_files)};
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

[[nodiscard]] LogExpected<std::shared_ptr<LogMetadataHelperInterface>> make_log_metadata_recovery_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_uri,
  ChunkReaderWriterFactory<>& chunk_reader_factory)
{
  auto log_metadata_helper_ptr =
    std::allocate_shared<LogMetadataRecoveryHelper, std::pmr::polymorphic_allocator<LogMetadataRecoveryHelper>>(
      memory_resource, memory_resource);
  if (const auto init_result = log_metadata_helper_ptr->initialize(log_uri, chunk_reader_factory); !init_result)
  {
    return jewels::unexpected(init_result.error());
  }
  return {std::move(log_metadata_helper_ptr)};
}

} // namespace clockwork_logging::offboard
