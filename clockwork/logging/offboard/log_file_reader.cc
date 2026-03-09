// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/log_file_reader.hh"

#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/offboard/index_chunk_reader.hh"
#include "clockwork/logging/offboard/log_file_trailer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/metadata_chunk_reader.hh"
#include "clockwork/logging/offboard/metrics_chunk_reader.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <functional>
#include <ranges>
#include <regex>
#include <unordered_map>
#include <utility>

namespace clockwork_logging::offboard
{

LogFileReader::LogFileReader(
  jewels::memory::MemoryResource memory_resource,
  jewels::memory::NonNullSharedPtr<ChunkReader> chunk_reader_ptr,
  jewels::memory::NonNullSharedPtr<ChunkCompressor> chunk_compressor_ptr)
  : memory_resource_(std::move(memory_resource)),
    chunk_reader_ptr_(std::move(chunk_reader_ptr)),
    chunk_compressor_ptr_(std::move(chunk_compressor_ptr)),
    channel_metadata_map_(memory_resource_)
{
}

[[nodiscard]] LogExpected<std::pmr::list<reader::MessageChunkHandle>> LogFileReader::get_message_chunk_list(
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  std::optional<LogInterval> maybe_log_interval)
{
  if (!metadata_map_ptr_)
  {
    if (const auto metadata_result = load_metadata(); !metadata_result)
    {
      return jewels::unexpected(metadata_result.error());
    }
  }
  const auto& index_chunk_handle = log_file_trailer_info_ptr_->index_chunk_handle;
  return read_index_chunk(
    memory_resource_,
    index_chunk_handle.location,
    *metadata_map_ptr_,
    maybe_log_interval,
    maybe_desired_channels,
    chunk_reader_ptr_,
    chunk_compressor_ptr_);
}

[[nodiscard]] LogExpected<jewels::memory::ObjectPtr<const std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo>>>
LogFileReader::get_metadata()
{
  if (!metadata_map_ptr_)
  {
    if (const auto metadata_result = load_metadata(); !metadata_result)
    {
      return jewels::unexpected(metadata_result.error());
    }
  }
  return jewels::memory::make_non_null_from_ref(*metadata_map_ptr_); // metadata_map_ptr_ set in load_metadata()
}

[[nodiscard]] LogExpected<jewels::memory::ObjectPtr<const reader::LoggedChannelInfo>>
LogFileReader::get_channel_metadata(std::string_view channel_name)
{
  if (!metadata_map_ptr_)
  {
    if (const auto metadata_result = load_metadata(); !metadata_result)
    {
      return jewels::unexpected(metadata_result.error());
    }
  }
  const auto iter = channel_metadata_map_.find(channel_name);
  if (iter == channel_metadata_map_.end())
  {
    return jewels::unexpected(LogError::unknown_channel);
  }
  return iter->second;
}

[[nodiscard]] LogExpected<jewels::memory::ObjectPtr<const reader::LogMetrics>> LogFileReader::get_metrics()
{
  if (!metadata_map_ptr_)
  {
    if (const auto metadata_result = load_metadata(); !metadata_result)
    {
      return jewels::unexpected(metadata_result.error());
    }
  }
  if (!log_metrics_ptr_)
  {
    const auto& metrics_chunk_handle = log_file_trailer_info_ptr_->metrics_chunk_handle;
    auto metrics_result = read_metrics_chunk(
      memory_resource_,
      metrics_chunk_handle.location,
      *metadata_map_ptr_,
      *metrics_chunk_handle.chunk_reader_ptr,
      *metrics_chunk_handle.chunk_compressor_ptr);
    if (!metrics_result)
    {
      jewels::log_cerr_error(
        "Failed to metrics metadata chunk from {}: {}", get_file_uri().string(), metrics_result.error());
      return jewels::unexpected(metrics_result.error());
    }
    log_metrics_ptr_ = std::allocate_shared<reader::LogMetrics, std::pmr::polymorphic_allocator<reader::LogMetrics>>(
      memory_resource_, std::move(metrics_result).value());
  }
  return jewels::memory::make_non_null_from_ref(*log_metrics_ptr_); // log_metrics_ptr_ set above
}

[[nodiscard]] const LogUri& LogFileReader::get_file_uri() const
{
  return chunk_reader_ptr_->file_uri();
}

[[nodiscard]] LogExpected<void> LogFileReader::load_metadata()
{
  if (s3_access_is_denied_)
  {
    return jewels::unexpected(LogError::s3_access_denied);
  }
  if (!log_file_trailer_info_ptr_)
  {
    auto trailer_result = read_log_file_trailer(chunk_reader_ptr_, chunk_compressor_ptr_);
    if (!trailer_result)
    {
      if (trailer_result.error() == LogError::s3_access_denied)
      {
        s3_access_is_denied_ = true;
      }
      else
      {
        jewels::log_cerr_error("Failed to read trailer from {}: {}", get_file_uri().string(), trailer_result.error());
      }
      return jewels::unexpected(trailer_result.error());
    }
    log_file_trailer_info_ptr_ =
      std::allocate_shared<reader::LogFileTrailerInfo, std::pmr::polymorphic_allocator<reader::LogFileTrailerInfo>>(
        memory_resource_, std::move(trailer_result).value());
  }
  if (!metadata_map_ptr_)
  {
    const auto& metadata_chunk_handle = log_file_trailer_info_ptr_->metadata_chunk_handle;
    auto metadata_result = read_metadata_chunk(
      memory_resource_,
      metadata_chunk_handle.location,
      *metadata_chunk_handle.chunk_reader_ptr,
      *metadata_chunk_handle.chunk_compressor_ptr);
    if (!metadata_result)
    {
      jewels::log_cerr_error(
        "Failed to read metadata chunk from {}: {}", get_file_uri().string(), metadata_result.error());
      return jewels::unexpected(metadata_result.error());
    }
    metadata_map_ptr_ = std::allocate_shared<
      std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo>,
      std::pmr::polymorphic_allocator<std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo>>>(
      memory_resource_, std::move(metadata_result).value());
    for (const auto& channel_metadata : std::views::values(*metadata_map_ptr_))
    {
      channel_metadata_map_.emplace(
        channel_metadata.channel_name, jewels::memory::make_non_null_from_ref(channel_metadata));
    }
  }
  return {};
}

} // namespace clockwork_logging::offboard
