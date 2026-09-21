// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/log_metadata_file_helper.hh"

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_metadata_recovery_helper.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/v1/log_amendment.pb.h"
#include "clockwork/logging/offboard/v1/log_metadata.pb.h"
#include "clockwork/logging/offboard/v1/log_union.pb.h"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/std/expected.hh"

#include <google/protobuf/repeated_ptr_field.h>

#include <algorithm>
#include <functional>
#include <memory_resource>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

using jewels::Out;

namespace
{

/// Get the set of desired persistent channels
/// @param[in] memory_resouce Memory resource
/// @param[in] persistent_channels Set of persistent channel names
/// @param[in] maybe_desired_channels Optional set of desired channels
/// @param[in] maybe_excluded_channels Optional set of excluded channels
/// @return Set of desired persistent channels
[[nodiscard]] std::pmr::unordered_set<std::pmr::string> get_desired_persistent_channels(
  const jewels::memory::MemoryResource& memory_resource,
  const std::pmr::unordered_set<std::pmr::string>& persistent_channels,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_excluded_channels)
{
  std::pmr::unordered_set<std::pmr::string> desired_persistent_channels{memory_resource};
  for (const auto& channel : persistent_channels)
  {
    if (
      (!maybe_desired_channels || maybe_desired_channels->contains(channel)) &&
      (!maybe_excluded_channels || !maybe_excluded_channels->contains(channel)))
    {
      desired_persistent_channels.insert(channel);
    }
  }
  return desired_persistent_channels;
}

} // namespace

LogMetadataFileHelper::LogMetadataFileHelper(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)),
    log_file_metadata_map_(memory_resource_),
    channels_(memory_resource_),
    persistent_channels_(memory_resource_)
{
}

LogOutcome LogMetadataFileHelper::initialize_from_protobuf(
  const ::clockwork::logging::offboard::v1::LogMetadata& log_metadata_protobuf,
  const LogUri& log_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  ChunkReaderWriterFactory<>& chunk_reader_factory)
{
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
        if (!excluded_channels.contains(channel))
        {
          channels_.insert(std::pmr::string{channel, memory_resource_});
          channels.insert(std::pmr::string{channel, memory_resource_});
        }
      }
      std::pmr::unordered_set<std::pmr::string> persistent_channels{memory_resource_};
      for (const auto& persistent_channel : writer_metadata.persistent_channel())
      {
        if (!excluded_channels.contains(persistent_channel))
        {
          persistent_channels_.insert(std::pmr::string{persistent_channel, memory_resource_});
          persistent_channels.insert(std::pmr::string{persistent_channel, memory_resource_});
        }
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
  const auto log_uri_str = log_uri.string();
  auto readdir_result = chunk_reader_factory.list_log_files(log_uri_str);
  if (!readdir_result)
  {
    jewels::log_cerr_error("Failed to list log files under {}: {}", log_uri_str, readdir_result.error());
    return readdir_result.error();
  }
  for (const auto& log_file_uri : readdir_result.value())
  {
    std::pmr::string log_file_name{log_file_uri.filename(), memory_resource_};
    if (!log_file_metadata_map_.contains(log_file_name))
    {
      jewels::log_cerr_warn("File {} not found in the log metadata: ignoring", log_file_name);
      continue;
    }
    all_log_files_.emplace_back(log_file_uri);
  }
  return LogError::success;
}

LogOutcome LogMetadataFileHelper::initialize(
  const LogUri& metadata_file_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  ChunkReaderWriterFactory<>& chunk_reader_factory)
{
  const auto read_metadata_result =
    chunk_reader_factory.read_text_proto<::clockwork::logging::offboard::v1::LogMetadata>(metadata_file_uri.string());
  if (!read_metadata_result)
  {
    jewels::log_cerr_error("Failed to read log metadata file at {}", metadata_file_uri.string());
    return LogError::failed_to_open_log_file;
  }
  return initialize_from_protobuf(
    read_metadata_result.value(), metadata_file_uri.parent_uri(), excluded_channels, chunk_reader_factory);
}

LogOutcome LogMetadataFileHelper::get_log_file_map(
  Out<std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>>> log_file_map,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  const std::optional<LogInterval>& maybe_transmit_time_interval,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_excluded_channels) const
{
  *log_file_map =
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>>{memory_resource_};
  const auto desired_persistent_channels = get_desired_persistent_channels(
    memory_resource_, persistent_channels_, maybe_desired_channels, maybe_excluded_channels);
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

LogAmendmentFileHelper::LogAmendmentFileHelper(jewels::memory::MemoryResource memory_resource, bool is_merge_union)
  : memory_resource_(std::move(memory_resource)),
    channels_(memory_resource_),
    persistent_channels_(memory_resource_),
    is_merge_union_(is_merge_union)
{
}

LogOutcome LogAmendmentFileHelper::initialize(
  const LogUri& metadata_file_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  ChunkReaderWriterFactory<>& chunk_reader_factory)
{
  const auto read_amendment_result =
    chunk_reader_factory.read_text_proto<::clockwork::logging::offboard::v1::LogAmendment>(metadata_file_uri.string());
  if (!read_amendment_result)
  {
    jewels::log_cerr_error("Failed to read log amendment file at {}", metadata_file_uri.string());
    return LogError::failed_to_open_log_file;
  }
  const auto& log_amendment_protobuf = read_amendment_result.value();
  const auto log_uri = metadata_file_uri.parent_uri();
  amendment_log_ptr_ = jewels::memory::make_pmr_shared<LogMetadataFileHelper>(memory_resource_, memory_resource_);
  if (const auto init_outcome = amendment_log_ptr_->initialize_from_protobuf(
        log_amendment_protobuf.amendment_metadata(), log_uri, excluded_channels, chunk_reader_factory);
      !ok(init_outcome))
  {
    return init_outcome;
  }
  transmit_time_interval_ = amendment_log_ptr_->get_transmit_time_interval();
  for (const auto& channel : amendment_log_ptr_->get_channels())
  {
    channels_.insert(std::pmr::string{channel, memory_resource_});
  }
  for (const auto& persistent_channel : amendment_log_ptr_->get_persistent_channels())
  {
    persistent_channels_.insert(std::pmr::string{persistent_channel, memory_resource_});
  }
  if (!is_merge_union_)
  {
    LogUri amended_uri{memory_resource_};
    std::pmr::unordered_set<std::string_view> amended_excluded_channels{memory_resource_};
    if (const auto uri_outcome = get_log_path_uri(
          memory_resource_,
          log_uri,
          log_amendment_protobuf.amended_log_path(),
          Out{amended_uri},
          Out{amended_excluded_channels});
        !ok(uri_outcome))
    {
      return uri_outcome;
    }
    amended_excluded_channels.insert(excluded_channels.begin(), excluded_channels.end());
    if (const auto make_outcome = make_log_metadata_helper(
          memory_resource_, amended_uri, amended_excluded_channels, false, chunk_reader_factory, Out{amended_log_ptr_});
        !ok(make_outcome))
    {
      return make_outcome;
    }
    constexpr LogInterval empty_interval{LogTimestamp{0}, LogTimestamp{0}};
    const auto& amended_interval = amended_log_ptr_->get_transmit_time_interval();
    if (amended_interval != empty_interval)
    {
      if (transmit_time_interval_ == empty_interval)
      {
        transmit_time_interval_ = amended_interval;
      }
      else
      {
        transmit_time_interval_.add_interval(amended_interval);
      }
    }
    for (const auto& channel : amended_log_ptr_->get_channels())
    {
      channels_.insert(std::pmr::string{channel, memory_resource_});
    }
    for (const auto& persistent_channel : amended_log_ptr_->get_persistent_channels())
    {
      if (!amendment_log_ptr_->get_channels().contains(persistent_channel))
      {
        persistent_channels_.insert(std::pmr::string{persistent_channel, memory_resource_});
      }
    }
  }
  return LogError::success;
}

LogOutcome LogAmendmentFileHelper::get_log_file_map(
  Out<std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>>> log_file_map,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  const std::optional<LogInterval>& maybe_transmit_time_interval,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_excluded_channels) const
{
  if (const auto get_map_outcome = amendment_log_ptr_->get_log_file_map(
        Out{*log_file_map}, maybe_desired_channels, maybe_transmit_time_interval, maybe_excluded_channels);
      !ok(get_map_outcome))
  {
    return get_map_outcome;
  }
  if (!is_merge_union_)
  {
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> amended_file_map;
    if (const auto get_map_outcome = amended_log_ptr_->get_log_file_map(
          Out{amended_file_map}, maybe_desired_channels, maybe_transmit_time_interval, maybe_excluded_channels);
        !ok(get_map_outcome))
    {
      return get_map_outcome;
    }
    for (auto& [amended_file, amended_channels] : amended_file_map)
    {
      log_file_map->emplace(amended_file, std::move(amended_channels));
    }
  }
  return LogError::success;
}

[[nodiscard]] LogInterval LogAmendmentFileHelper::get_transmit_time_interval() const
{
  return transmit_time_interval_;
}

[[nodiscard]] const std::pmr::unordered_set<std::pmr::string>& LogAmendmentFileHelper::get_channels() const
{
  return channels_;
}

[[nodiscard]] const std::pmr::unordered_set<std::pmr::string>& LogAmendmentFileHelper::get_persistent_channels() const
{
  return persistent_channels_;
}

LogUnionFileHelper::LogUnionFileHelper(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)),
    union_entry_ptrs_(memory_resource_),
    channels_(memory_resource_),
    persistent_channels_(memory_resource_)
{
}

LogOutcome LogUnionFileHelper::initialize(
  const LogUri& metadata_file_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  ChunkReaderWriterFactory<>& chunk_reader_factory)
{
  const auto read_union_result =
    chunk_reader_factory.read_text_proto<::clockwork::logging::offboard::v1::LogUnion>(metadata_file_uri.string());
  if (!read_union_result)
  {
    jewels::log_cerr_error("Failed to read log union file at {}", metadata_file_uri.string());
    return LogError::failed_to_open_log_file;
  }
  const auto& log_union_protobuf = read_union_result.value();
  const auto log_uri = metadata_file_uri.parent_uri();
  for (const auto& union_entry : log_union_protobuf.log_union_entry())
  {
    LogUri path_uri{memory_resource_};
    std::pmr::unordered_set<std::string_view> uri_excluded_channels{memory_resource_};
    if (const auto uri_outcome =
          get_log_path_uri(memory_resource_, log_uri, union_entry, Out{path_uri}, Out{uri_excluded_channels});
        !ok(uri_outcome))
    {
      return uri_outcome;
    }
    uri_excluded_channels.insert(excluded_channels.begin(), excluded_channels.end());
    std::shared_ptr<LogMetadataHelperInterface> helper_ptr;
    if (const auto make_outcome = make_log_metadata_helper(
          memory_resource_,
          path_uri,
          uri_excluded_channels,
          log_union_protobuf.is_merge_union(),
          chunk_reader_factory,
          Out{helper_ptr});
        !ok(make_outcome))
    {
      return make_outcome;
    }
    union_entry_ptrs_.push_back(std::move(helper_ptr));
  }
  constexpr LogInterval empty_interval{LogTimestamp{0}, LogTimestamp{0}};
  transmit_time_interval_ = empty_interval;
  for (const auto& union_entry_ptr : union_entry_ptrs_)
  {
    const auto& entry_interval = union_entry_ptr->get_transmit_time_interval();
    if (entry_interval != empty_interval)
    {
      if (transmit_time_interval_ == empty_interval)
      {
        transmit_time_interval_ = entry_interval;
      }
      else
      {
        transmit_time_interval_.add_interval(entry_interval);
      }
    }
    for (const auto& channel : union_entry_ptr->get_channels())
    {
      channels_.insert(std::pmr::string{channel, memory_resource_});
    }
    for (const auto& persistent_channel : union_entry_ptr->get_persistent_channels())
    {
      persistent_channels_.insert(std::pmr::string{persistent_channel, memory_resource_});
    }
  }
  return LogError::success;
}

LogOutcome LogUnionFileHelper::get_log_file_map(
  Out<std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>>> log_file_map,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  const std::optional<LogInterval>& maybe_transmit_time_interval,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_excluded_channels) const
{
  *log_file_map =
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>>{memory_resource_};
  for (const auto& union_entry_ptr : union_entry_ptrs_)
  {
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> union_file_map;
    if (const auto get_map_outcome = union_entry_ptr->get_log_file_map(
          Out{union_file_map}, maybe_desired_channels, maybe_transmit_time_interval, maybe_excluded_channels);
        !ok(get_map_outcome))
    {
      return get_map_outcome;
    }
    for (auto& [union_file, union_channels] : union_file_map)
    {
      log_file_map->emplace(union_file, std::move(union_channels));
    }
  }
  return LogError::success;
}

[[nodiscard]] LogInterval LogUnionFileHelper::get_transmit_time_interval() const
{
  return transmit_time_interval_;
}

[[nodiscard]] const std::pmr::unordered_set<std::pmr::string>& LogUnionFileHelper::get_channels() const
{
  return channels_;
}

[[nodiscard]] const std::pmr::unordered_set<std::pmr::string>& LogUnionFileHelper::get_persistent_channels() const
{
  return persistent_channels_;
}

LogOutcome make_log_union_file_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_union_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  ChunkReaderWriterFactory<>& chunk_reader_factory,
  Out<std::shared_ptr<LogMetadataHelperInterface>> helper_ptr)
{
  if (const auto exists_result = chunk_reader_factory.exists(log_union_uri.string());
      exists_result && exists_result.value())
  {
    *helper_ptr = jewels::memory::make_pmr_shared<LogUnionFileHelper>(memory_resource, memory_resource);
    return (*helper_ptr)->initialize(log_union_uri, excluded_channels, chunk_reader_factory);
  }
  jewels::log_cerr_error("No log union metadata found at {}: not a log", log_union_uri.string());
  return LogError::not_a_log;
}

LogOutcome make_log_amendment_file_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_metadata_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  bool is_merge_union,
  ChunkReaderWriterFactory<>& chunk_reader_factory,
  Out<std::shared_ptr<LogMetadataHelperInterface>> helper_ptr)
{
  if (const auto exists_result = chunk_reader_factory.exists(log_metadata_uri.string());
      exists_result && exists_result.value())
  {
    *helper_ptr =
      jewels::memory::make_pmr_shared<LogAmendmentFileHelper>(memory_resource, memory_resource, is_merge_union);
    return (*helper_ptr)->initialize(log_metadata_uri, excluded_channels, chunk_reader_factory);
  }
  jewels::log_cerr_error("No log amendment metadata found at {}: not a log", log_metadata_uri.string());
  return LogError::not_a_log;
}

LogOutcome make_log_metadata_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  bool is_merge_union,
  ChunkReaderWriterFactory<>& chunk_reader_factory,
  Out<std::shared_ptr<LogMetadataHelperInterface>> helper_ptr)
{
  if (const auto exists_result = chunk_reader_factory.exists(log_uri.string());
      !exists_result || !exists_result.value())
  {
    if (exists_result)
    {
      jewels::log_cerr_error("Cannot access log under {}: {}", log_uri.string(), LogError::no_such_file_or_directory);
      return LogError::no_such_file_or_directory;
    }
    jewels::log_cerr_error("Cannot access log under {}: {}", log_uri.string(), exists_result.error());
    return exists_result.error();
  }
  const auto log_amendment_uri = log_uri / log_amendment_filename;
  if (const auto exists_result = chunk_reader_factory.exists(log_amendment_uri.string());
      exists_result && exists_result.value())
  {
    return make_log_amendment_file_helper(
      memory_resource, log_amendment_uri, excluded_channels, is_merge_union, chunk_reader_factory, Out{*helper_ptr});
  }
  const auto log_union_uri = log_uri / log_union_filename;
  if (const auto exists_result = chunk_reader_factory.exists(log_union_uri.string());
      exists_result && exists_result.value())
  {
    if (is_merge_union)
    {
      jewels::log_cerr_error("Unsupported merge union within a union: {}", log_uri.string());
      return LogError::invalid_argument;
    }
    return make_log_union_file_helper(
      memory_resource, log_union_uri, excluded_channels, chunk_reader_factory, Out{*helper_ptr});
  }
  const auto log_metadata_uri = log_uri / log_metadata_filename;
  return make_log_metadata_file_helper(
    memory_resource, log_metadata_uri, excluded_channels, chunk_reader_factory, Out{*helper_ptr});
}

LogOutcome make_log_metadata_file_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_metadata_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  ChunkReaderWriterFactory<>& chunk_reader_factory,
  Out<std::shared_ptr<LogMetadataHelperInterface>> helper_ptr)
{
  if (const auto exists_result = chunk_reader_factory.exists(log_metadata_uri.string());
      exists_result && exists_result.value())
  {
    *helper_ptr = jewels::memory::make_pmr_shared<LogMetadataFileHelper>(memory_resource, memory_resource);
    return (*helper_ptr)->initialize(log_metadata_uri, excluded_channels, chunk_reader_factory);
  }
  return make_log_metadata_recovery_helper(
    memory_resource, log_metadata_uri.parent_uri(), excluded_channels, chunk_reader_factory, Out{*helper_ptr});
}

LogOutcome make_log_metadata_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  ChunkReaderWriterFactory<>& chunk_reader_factory,
  Out<std::shared_ptr<LogMetadataHelperInterface>> helper_ptr,
  bool is_merge_union)
{
  if (const auto exists_result = chunk_reader_factory.exists(log_uri.string());
      !exists_result || !exists_result.value())
  {
    if (exists_result)
    {
      jewels::log_cerr_error("Cannot access log under {}: {}", log_uri.string(), LogError::no_such_file_or_directory);
      return LogError::no_such_file_or_directory;
    }
    jewels::log_cerr_error("Cannot access log under {}: {}", log_uri.string(), exists_result.error());
    return exists_result.error();
  }
  const auto log_amendment_uri = log_uri / log_amendment_filename;
  if (const auto exists_result = chunk_reader_factory.exists(log_amendment_uri.string());
      exists_result && exists_result.value())
  {
    return make_log_amendment_file_helper(
      memory_resource, log_amendment_uri, excluded_channels, is_merge_union, chunk_reader_factory, Out{*helper_ptr});
  }
  const auto log_union_uri = log_uri / log_union_filename;
  if (const auto exists_result = chunk_reader_factory.exists(log_union_uri.string());
      exists_result && exists_result.value())
  {
    return make_log_union_file_helper(
      memory_resource, log_union_uri, excluded_channels, chunk_reader_factory, Out{*helper_ptr});
  }
  const auto log_metadata_uri = log_uri / log_metadata_filename;
  return make_log_metadata_file_helper(
    memory_resource, log_metadata_uri, excluded_channels, chunk_reader_factory, Out{*helper_ptr});
}

[[nodiscard]] LogOutcome get_log_path_uri(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_uri,
  const ::clockwork::logging::offboard::v1::LogPath& log_path,
  Out<LogUri> path_uri,
  Out<std::pmr::unordered_set<std::string_view>> excluded_channels)
{
  if (log_path.has_absolute_path())
  {
    auto make_result = LogUri::try_make(log_path.absolute_path(), memory_resource);
    if (!make_result)
    {
      jewels::log_cerr_error("Invalid log path URI: {}", log_path.absolute_path());
      return LogError::invalid_log_uri;
    }
    *path_uri = std::move(make_result).value();
  }
  else
  {
    *path_uri = log_uri.apply_relative_path(log_path.relative_path());
  }
  *excluded_channels = std::pmr::unordered_set<std::string_view>{memory_resource};
  for (const auto& excluded_channel : log_path.excluded_channel())
  {
    excluded_channels->emplace(excluded_channel);
  }
  return LogError::success;
}

} // namespace clockwork_logging::offboard
