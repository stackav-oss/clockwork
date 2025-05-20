// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/log_union_file_helper.hh"

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_metadata_file_helper.hh"
#include "clockwork/logging/offboard/log_metadata_helper_interface.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/v1/log_union.pb.h"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <google/protobuf/repeated_ptr_field.h>

#include <forward_list>
#include <memory_resource>
#include <string>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

namespace
{

/// Get the URI for a log union entry
/// @param[in] memory_resource Memory resource
/// @param[in] log_uri Log URI
/// @param[in] union_entry Log union entry
/// @return URI for the log union entry or LogError on failure
[[nodiscard]] LogExpected<LogUri> get_log_entry_uri(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_uri,
  const ::clockwork::logging::offboard::v1::LogUnionEntry& union_entry)
{
  if (union_entry.has_absolute_path())
  {
    auto make_result = LogUri::try_make(union_entry.absolute_path(), memory_resource);
    if (!make_result)
    {
      jewels::log_cerr_error("Invalid log union entry URI: {}", union_entry.absolute_path());
      return jewels::unexpected(LogError::invalid_log_uri);
    }
    return std::move(make_result).value();
  }
  return log_uri.apply_relative_path(union_entry.relative_path());
}

} // namespace

LogUnionFileHelper::LogUnionFileHelper(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)),
    union_entry_ptrs_(memory_resource_),
    channels_(memory_resource_),
    persistent_channels_(memory_resource_)
{
}

[[nodiscard]] LogExpected<void>
LogUnionFileHelper::initialize(const LogUri& metadata_file_uri, ChunkReaderWriterFactory& chunk_reader_factory)
{
  const auto read_union_result =
    chunk_reader_factory.read_text_proto<::clockwork::logging::offboard::v1::LogUnion>(metadata_file_uri.string());
  if (!read_union_result)
  {
    jewels::log_cerr_error("Failed to read log union file at {}", metadata_file_uri.string());
    return jewels::unexpected(LogError::failed_to_open_log_file);
  }
  const auto& log_union_protobuf = read_union_result.value();
  const auto log_uri = metadata_file_uri.parent_uri();
  for (const auto& union_entry : log_union_protobuf.log_union_entry())
  {
    const auto uri_result = get_log_entry_uri(memory_resource_, log_uri, union_entry);
    if (!uri_result)
    {
      return jewels::unexpected(uri_result.error());
    }
    const auto log_metadata_uri = uri_result.value() / log_metadata_filename;
    auto helper_result = make_log_metadata_file_helper(memory_resource_, log_metadata_uri, chunk_reader_factory);
    if (!helper_result)
    {
      return jewels::unexpected(helper_result.error());
    }
    union_entry_ptrs_.push_back(std::move(helper_result).value());
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
  return {};
}

[[nodiscard]] LogExpected<std::pmr::vector<std::pmr::string>> LogUnionFileHelper::list_log_files(
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  const std::optional<LogInterval>& maybe_transmit_time_interval) const
{
  std::pmr::vector<std::pmr::string> filtered_log_files{memory_resource_};
  for (const auto& union_entry_ptr : union_entry_ptrs_)
  {
    auto list_result = union_entry_ptr->list_log_files(maybe_desired_channels, maybe_transmit_time_interval);
    if (!list_result)
    {
      return jewels::unexpected(list_result.error());
    }
    for (auto log_file : list_result.value())
    {
      filtered_log_files.push_back(std::move(log_file));
    }
  }
  return {std::move(filtered_log_files)};
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

[[nodiscard]] LogExpected<std::shared_ptr<LogMetadataHelperInterface>> make_log_union_file_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_union_uri,
  ChunkReaderWriterFactory& chunk_reader_factory)
{
  if (const auto exists_result = chunk_reader_factory.exists(log_union_uri.string());
      exists_result && exists_result.value())
  {
    auto log_union_helper_ptr =
      std::allocate_shared<LogUnionFileHelper, std::pmr::polymorphic_allocator<LogUnionFileHelper>>(
        memory_resource, memory_resource);
    if (const auto init_result = log_union_helper_ptr->initialize(log_union_uri, chunk_reader_factory); !init_result)
    {
      return jewels::unexpected(init_result.error());
    }
    return {std::move(log_union_helper_ptr)};
  }
  jewels::log_cerr_error("No log union metadata found at {}: not a log", log_union_uri.string());
  return jewels::unexpected(LogError::not_a_log);
}

} // namespace clockwork_logging::offboard
