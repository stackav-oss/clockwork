// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/log_metadata_file_helper.hh"

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/v1/log_metadata.pb.h"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <google/protobuf/repeated_ptr_field.h>

#include <algorithm>
#include <functional>
#include <memory_resource>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

LogMetadataFileHelper::LogMetadataFileHelper(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)),
    log_file_metadata_map_(memory_resource_),
    channels_(memory_resource_),
    persistent_channels_(memory_resource_)
{
}

[[nodiscard]] LogExpected<void>
LogMetadataFileHelper::initialize(const LogUri& metadata_file_uri, ChunkReaderWriterFactory& chunk_reader_factory)
{
  const auto read_metadata_result =
    chunk_reader_factory.read_text_proto<::clockwork::logging::offboard::v1::LogMetadata>(metadata_file_uri.string());
  if (!read_metadata_result)
  {
    jewels::log_cerr_error("Failed to read log metadata file at {}", metadata_file_uri.string());
    return jewels::unexpected(LogError::failed_to_open_log_file);
  }
  const auto& log_metadata_protobuf = read_metadata_result.value();
  transmit_time_interval_ = {
    LogTimestamp{log_metadata_protobuf.min_transmit_time_ns()},
    LogTimestamp{log_metadata_protobuf.max_transmit_time_ns()}};
  for (const auto& writer_metadata : log_metadata_protobuf.log_writer_metadata())
  {
    for (const auto& file_metadata : writer_metadata.log_file_metadata())
    {
      std::pmr::unordered_set<std::pmr::string> channels{memory_resource_};
      for (const auto& channel : writer_metadata.channel())
      {
        channels_.insert(std::pmr::string{channel, memory_resource_});
        channels.insert(std::pmr::string{channel, memory_resource_});
      }
      std::pmr::unordered_set<std::pmr::string> persistent_channels{memory_resource_};
      for (const auto& persistent_channel : writer_metadata.persistent_channel())
      {
        persistent_channels_.insert(std::pmr::string{persistent_channel, memory_resource_});
        persistent_channels.insert(std::pmr::string{persistent_channel, memory_resource_});
      }
      log_file_metadata_map_.emplace(
        std::pmr::string{file_metadata.log_file_name(), memory_resource_},
        LogFileMetadataMapEntry{
          .channels = std::move(channels),
          .persistent_channels = std::move(persistent_channels),
          .transmit_time_interval =
            {LogTimestamp{file_metadata.min_transmit_time_ns()}, LogTimestamp{file_metadata.max_transmit_time_ns()}},
        });
    }
  }
  const auto log_uri_str = metadata_file_uri.parent_uri().string();
  auto readdir_result = chunk_reader_factory.list_log_files(log_uri_str);
  if (!readdir_result)
  {
    jewels::log_cerr_error("Failed to list log files under {}: {}", log_uri_str, readdir_result.error());
    return jewels::unexpected(readdir_result.error());
  }
  all_log_files_ = std::move(readdir_result).value();
  return {};
}

[[nodiscard]] LogExpected<std::pmr::vector<std::pmr::string>> LogMetadataFileHelper::list_log_files(
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

[[nodiscard]] LogInterval LogMetadataFileHelper::get_transmit_time_interval() const
{
  return transmit_time_interval_;
}

[[nodiscard]] const std::pmr::unordered_set<std::pmr::string>& LogMetadataFileHelper::get_channels() const
{
  return channels_;
}

[[nodiscard]] const std::pmr::unordered_set<std::pmr::string>& LogMetadataFileHelper::get_persistent_channels() const
{
  return persistent_channels_;
}

[[nodiscard]] LogExpected<std::shared_ptr<LogMetadataHelperInterface>> make_log_metadata_file_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_metadata_uri,
  ChunkReaderWriterFactory& chunk_reader_factory)
{
  if (const auto exists_result = chunk_reader_factory.exists(log_metadata_uri.string());
      exists_result && exists_result.value())
  {
    auto log_metadata_helper_ptr =
      std::allocate_shared<LogMetadataFileHelper, std::pmr::polymorphic_allocator<LogMetadataFileHelper>>(
        memory_resource, memory_resource);
    if (const auto init_result = log_metadata_helper_ptr->initialize(log_metadata_uri, chunk_reader_factory);
        !init_result)
    {
      return jewels::unexpected(init_result.error());
    }
    return {std::move(log_metadata_helper_ptr)};
  }
  jewels::log_cerr_error("No log metadata file found at {}: not a log", log_metadata_uri.string());
  return jewels::unexpected(LogError::not_a_log);
}

} // namespace clockwork_logging::offboard
