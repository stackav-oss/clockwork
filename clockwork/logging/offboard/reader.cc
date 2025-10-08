// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/reader.hh"

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/offboard/index_chunk_reader.hh"
#include "clockwork/logging/offboard/log_file_trailer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_metadata_helper.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/metadata_chunk_reader.hh"
#include "clockwork/logging/offboard/metrics_chunk_reader.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <functional>
#include <future>
#include <iterator>
#include <ranges>
#include <regex>
#include <span>
#include <unordered_map>
#include <utility>

namespace clockwork_logging::offboard
{

namespace
{

/// Merge logged channel metadata into a map containing metadata from all log files
/// @param[in] channel_metadata Logged channel metadata
/// @param[in,out] Map containing metadata from all log files
/// @return LogError on failure
[[nodiscard]] LogExpected<void> merge_metadata(
  const reader::LoggedChannelInfo& channel_metadata,
  std::pmr::map<std::string_view, LoggedChannelMetadata>& metadata_map)
{
  const auto& metadata_iter = metadata_map.find(channel_metadata.channel_name);
  if (metadata_iter == metadata_map.end())
  {
    metadata_map.emplace(
      channel_metadata.channel_name,
      LoggedChannelMetadata{
        .channel_name = channel_metadata.channel_name,
        .message_encoding = channel_metadata.message_encoding,
        .channel_type = channel_metadata.channel_type,
        .schema_name = channel_metadata.schema_name,
        .schema_encoding = channel_metadata.schema_encoding,
        .schema_definition = channel_metadata.schema_definition,
      });
  }
  else
  {
    if (metadata_iter->second.message_encoding != channel_metadata.message_encoding)
    {
      jewels::log_cerr_error("Message encoding mismatch for channel {}", channel_metadata.channel_name);
      return jewels::unexpected(LogError::metadata_mismatch);
    }
    if (metadata_iter->second.channel_type != channel_metadata.channel_type)
    {
      jewels::log_cerr_error("Channel type mismatch for channel {}", channel_metadata.channel_name);
      return jewels::unexpected(LogError::metadata_mismatch);
    }
    if (metadata_iter->second.schema_name != channel_metadata.schema_name)
    {
      jewels::log_cerr_error("Schema name mismatch for channel {}", channel_metadata.channel_name);
      return jewels::unexpected(LogError::metadata_mismatch);
    }
    if (metadata_iter->second.schema_encoding != channel_metadata.schema_encoding)
    {
      jewels::log_cerr_error("Schema encoding mismatch for channel {}", channel_metadata.channel_name);
      return jewels::unexpected(LogError::metadata_mismatch);
    }
    if (metadata_iter->second.schema_definition != channel_metadata.schema_definition)
    {
      jewels::log_cerr_error("Schema definition mismatch for channel {}", channel_metadata.channel_name);
      return jewels::unexpected(LogError::metadata_mismatch);
    }
  }
  return {};
}

/// Combine logged channel metrics
/// @param[in] metrics Metrics to combine
/// @param[in,out] combined_metrics Combined metrics
void combine_channel_metrics(const reader::LoggedChannelMetrics& metrics, LoggedChannelMetrics& combined_metrics)
{
  if (combined_metrics.message_count == 0U)
  {
    combined_metrics.transmit_time_interval = metrics.transmit_time_interval;
  }
  else if (metrics.message_count != 0U)
  {
    combined_metrics.transmit_time_interval.add_interval(metrics.transmit_time_interval);
  }
  combined_metrics.message_count += metrics.message_count;
  combined_metrics.byte_count += metrics.byte_count;
}

/// Combine log metrics
/// @param[in] metrics Metrics to combine
/// @param[in,out] combined_metrics Combined metrics
void combine_log_metrics(const reader::LogMetrics& metrics, LogMetrics& combined_metrics)
{
  if (combined_metrics.message_count == 0U)
  {
    combined_metrics.transmit_time_interval = metrics.transmit_time_interval;
  }
  else if (metrics.message_count != 0U)
  {
    combined_metrics.transmit_time_interval.add_interval(metrics.transmit_time_interval);
  }
  combined_metrics.message_count += metrics.message_count;
  combined_metrics.byte_count += metrics.byte_count;
  for (const auto& [channel_name, channel_metrics] : metrics.metrics_map)
  {
    const auto channel_iter = combined_metrics.metrics_map.find(channel_name);
    if (channel_iter == combined_metrics.metrics_map.end())
    {
      combined_metrics.metrics_map.emplace(
        channel_name,
        LoggedChannelMetrics{
          .message_count = channel_metrics.message_count,
          .byte_count = channel_metrics.byte_count,
          .transmit_time_interval = channel_metrics.transmit_time_interval,
        });
    }
    else
    {
      combine_channel_metrics(channel_metrics, channel_iter->second);
    }
  }
}

/// Prune persistent message chunks that aren't read because there is a later chunk for the channel that has the message
/// we want. The chunks that we do want to read are added to the message chunk list.
/// @param[in] maybe_log_interval Optional log interval
/// @param[in] persistent_message_chunk_map List of persistent message chunks for each channel
/// @param[in,out] message_chunk_list List of message chunks to be read
void prune_persistent_message_chunks(
  std::optional<LogInterval> maybe_log_interval,
  std::pmr::unordered_map<std::string_view, std::pmr::list<reader::MessageChunkHandle>>& persistent_message_chunk_map,
  std::pmr::list<reader::MessageChunkHandle>& message_chunk_list)
{
  if (maybe_log_interval)
  {
    for (auto& [channel_name, persistent_message_chunk_list] : persistent_message_chunk_map)
    {
      persistent_message_chunk_list.sort();
      while (persistent_message_chunk_list.size() > 1U)
      {
        const auto& first_handle = persistent_message_chunk_list.front();
        const auto& second_handle = *std::next(persistent_message_chunk_list.begin());
        if (
          (first_handle.min_transmit_time >= maybe_log_interval->get_start_timestamp()) ||
          (second_handle.min_transmit_time > maybe_log_interval->get_start_timestamp()))
        {
          break;
        }
        persistent_message_chunk_list.pop_front();
      }
      message_chunk_list.splice(message_chunk_list.end(), std::move(persistent_message_chunk_list));
    }
  }
}

/// Process the chunks to be read from a log file
/// @param[in] memory_resource Memory resource
/// @param[in] chunk_list_result Result containing the chunks to read from the log file
/// @param[in] maybe_log_interval Optional log interval
/// @param[in,out] persistent_message_chunk_map List of persistent message chunks for each channel
/// @param[in,out] message_chunk_list List of message chunks to be read
[[nodiscard]] LogExpected<void> process_chunk_list_result(
  jewels::memory::MemoryResource memory_resource,
  LogExpected<std::pmr::list<reader::MessageChunkHandle>>& chunk_list_result,
  std::optional<LogInterval> maybe_log_interval,
  std::pmr::unordered_map<std::string_view, std::pmr::list<reader::MessageChunkHandle>>& persistent_message_chunk_map,
  std::pmr::list<reader::MessageChunkHandle>& message_chunk_list)
{
  if (!chunk_list_result)
  {
    if (chunk_list_result.error() == LogError::s3_access_denied)
    {
      return {};
    }
    jewels::log_cerr_error("Failed to get message chunk list: {}", chunk_list_result.error());
    return jewels::unexpected(chunk_list_result.error());
  }
  if (!maybe_log_interval)
  {
    message_chunk_list.splice(message_chunk_list.end(), std::move(chunk_list_result).value());
  }
  else
  {
    for (auto& chunk_handle : chunk_list_result.value())
    {
      if (chunk_handle.channel_type != ChannelType::persistent)
      {
        message_chunk_list.emplace_back(std::move(chunk_handle));
      }
      else
      {
        if (!persistent_message_chunk_map.contains(chunk_handle.channel_name))
        {
          persistent_message_chunk_map.emplace(
            chunk_handle.channel_name, std::pmr::list<reader::MessageChunkHandle>{memory_resource});
        }
        persistent_message_chunk_map.at(chunk_handle.channel_name).emplace_back(std::move(chunk_handle));
      }
    }
  }
  return {};
}

} // namespace

Reader::MessageReader::MessageReader(
  jewels::memory::MemoryResource memory_resource,
  std::pmr::list<reader::MessageChunkHandle> chunk_handles,
  const std::pmr::unordered_set<std::pmr::string>& persistent_channels,
  DecompressOption decompress_option)
  : memory_resource_(std::move(memory_resource)),
    pending_chunk_handles_(std::move(chunk_handles)),
    prefetch_list_(memory_resource_),
    active_readers_(
      MessageChunkComparator{},
      std::pmr::vector<jewels::memory::NonNullSharedPtr<MessageChunkReader>>{memory_resource_}),
    async_work_queue_ptr_(
      jewels::memory::allocate_shared<AsyncWorkQueue, std::pmr::polymorphic_allocator<AsyncWorkQueue>>(
        memory_resource_, num_worker_threads)),
    persistent_channels_(memory_resource_),
    read_persistent_channels_(memory_resource_),
    decompress_option_(decompress_option),
    lite_compressor_(memory_resource_)
{
  for (const auto& persistent_channel : persistent_channels)
  {
    persistent_channels_.emplace(persistent_channel);
  }
}

[[nodiscard]] Reader::MessageReader::operator bool()
{
  if (!is_initialized_)
  {
    advance();
    is_initialized_ = true;
  }
  return !active_readers_.empty();
}

[[nodiscard]] LogExpected<LoggedMessage> Reader::MessageReader::read_next()
{
  while (true)
  {
    if (!*this)
    {
      return jewels::unexpected(LogError::end_of_log);
    }
    auto message_reader_ptr = active_readers_.top();
    active_readers_.pop();
    auto read_result = message_reader_ptr->read_next();
    if (!message_reader_ptr->is_empty())
    {
      active_readers_.push(message_reader_ptr);
    }
    advance();
    prev_message_reader_ptr_ = std::move(message_reader_ptr).get();
    if (!read_result)
    {
      return handle_lite_compression(read_result.value());
    }
    const auto persistent_channel_iter = persistent_channels_.find(read_result->channel_name);
    if (persistent_channel_iter == persistent_channels_.end())
    {
      return handle_lite_compression(read_result.value());
    }
    const auto is_first = !read_persistent_channels_.contains(read_result->channel_name);
    if (is_first)
    {
      read_persistent_channels_.insert(*persistent_channel_iter);
    }
    if (is_first || !read_result->is_repeated_persistent)
    {
      return handle_lite_compression(read_result.value());
    }
  }
}

void Reader::MessageReader::advance_prefetch()
{
  while ((prefetch_list_.size() < max_prefetch_requests) && !pending_chunk_handles_.empty())
  {
    auto message_chunk_handle = std::move(pending_chunk_handles_.front());
    pending_chunk_handles_.pop_front();
    auto promise_ptr = std::allocate_shared<
      std::promise<LogExpected<jewels::memory::NonNullSharedPtr<MessageChunkReader>>>,
      std::pmr::polymorphic_allocator<std::promise<LogExpected<jewels::memory::NonNullSharedPtr<MessageChunkReader>>>>>(
      memory_resource_);
    prefetch_list_.push_back(
      PrefetchListEntry{
        .min_transmit_time = message_chunk_handle.min_transmit_time,
        .channel_name = message_chunk_handle.channel_name,
        .channel_type = message_chunk_handle.channel_type,
        .chunk_reader_future = promise_ptr->get_future(),
      });
    async_work_queue_ptr_->schedule_work_item_no_wait(
      [promise_ptr = std::move(promise_ptr), message_chunk_handle = std::move(message_chunk_handle), this]()
      {
        auto message_reader_ptr =
          jewels::memory::allocate_shared<MessageChunkReader, std::pmr::polymorphic_allocator<MessageChunkReader>>(
            memory_resource_,
            memory_resource_,
            message_chunk_handle.channel_name,
            message_chunk_handle.channel_type,
            message_chunk_handle.maybe_log_interval,
            message_chunk_handle.chunk_handle.compression_type);
        if (const auto read_result = message_reader_ptr->read_chunk(
              message_chunk_handle.chunk_handle.location,
              *message_chunk_handle.chunk_handle.chunk_compressor_ptr,
              *message_chunk_handle.chunk_handle.chunk_reader_ptr);
            !read_result)
        {
          promise_ptr->set_value(jewels::unexpected(read_result.error()));
        }
        else
        {
          promise_ptr->set_value(std::move(message_reader_ptr));
        }
      });
  }
}

void Reader::MessageReader::advance()
{
  prev_message_reader_ptr_ = {};
  advance_prefetch();
  while (!prefetch_list_.empty() &&
         (active_readers_.empty() || active_readers_.top()->get_next_message_identifier()->transmit_time >=
                                       prefetch_list_.front().min_transmit_time))
  {
    auto prefetch_result = prefetch_list_.front().chunk_reader_future.get();
    const auto channel_name = prefetch_list_.front().channel_name;
    prefetch_list_.pop_front();
    if (!prefetch_result)
    {
      jewels::log_cerr_warn("Failed to read message chunk for channel {}: {}", channel_name, prefetch_result.error());
      advance_prefetch();
      continue;
    }
    auto message_reader_ptr = std::move(prefetch_result).value();
    if (!message_reader_ptr->is_empty())
    {
      active_readers_.push(std::move(message_reader_ptr));
    }
    advance_prefetch();
  }
}

[[nodiscard]] LogExpected<LoggedMessage> Reader::MessageReader::handle_lite_compression(const LoggedMessage& message)
{
  if (!message.is_lite_compressed || decompress_option_ != DecompressOption::decompress)
  {
    return message;
  }
  const auto decompress_result = lite_compressor_.decompress(message.data);
  if (!decompress_result)
  {
    return jewels::unexpected(decompress_result.error());
  }
  auto decompressed_message = message;
  decompressed_message.data = decompress_result.value();
  decompressed_message.is_lite_compressed = false;
  return decompressed_message;
}

Reader::FileReaderState::FileReaderState(
  jewels::memory::MemoryResource memory_resource,
  jewels::memory::NonNullSharedPtr<ChunkReader> chunk_reader_ptr,
  jewels::memory::NonNullSharedPtr<ChunkCompressor> chunk_compressor_ptr)
  : memory_resource_(std::move(memory_resource)),
    chunk_reader_ptr_(std::move(chunk_reader_ptr)),
    chunk_compressor_ptr_(std::move(chunk_compressor_ptr)),
    channel_metadata_map_(memory_resource_)
{
}

[[nodiscard]] LogExpected<std::pmr::list<reader::MessageChunkHandle>> Reader::FileReaderState::get_message_chunk_list(
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
Reader::FileReaderState::get_metadata()
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
Reader::FileReaderState::get_channel_metadata(std::string_view channel_name)
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

[[nodiscard]] LogExpected<jewels::memory::ObjectPtr<const reader::LogMetrics>> Reader::FileReaderState::get_metrics()
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

[[nodiscard]] const LogUri& Reader::FileReaderState::get_file_uri() const
{
  return chunk_reader_ptr_->file_uri();
}

[[nodiscard]] LogExpected<void> Reader::FileReaderState::load_metadata()
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

Reader::Reader(jewels::memory::MemoryResource memory_resource, std::string_view uri_str)
  : memory_resource_(std::move(memory_resource)),
    uri_str_(uri_str, memory_resource_),
    chunk_reader_factory_(memory_resource_),
    chunk_compressor_ptr_(
      jewels::memory::allocate_shared<ChunkCompressor, std::pmr::polymorphic_allocator<ChunkCompressor>>(
        memory_resource_, memory_resource_)),
    file_reader_state_map_(memory_resource_)
{
}

[[nodiscard]] LogExpected<void> Reader::open(
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  std::optional<LogInterval> maybe_log_interval,
  DecompressOption decompress_option)
{
  if (message_reader_ptr_)
  {
    jewels::log_cerr_error("Failed to open log: already open");
    return jewels::unexpected(LogError::already_open);
  }
  if (!log_metadata_helper_ptr_)
  {
    if (const auto init_result = initialize_log_metadata_helper(); !init_result)
    {
      return jewels::unexpected(init_result.error());
    }
  }
  if (!file_reader_state_ptr_)
  {
    if (const auto init_result = initialize_file_reader_state(); !init_result)
    {
      return jewels::unexpected(init_result.error());
    }
  }
  auto filter_result = log_metadata_helper_ptr_->list_log_files(maybe_desired_channels, maybe_log_interval);
  if (!filter_result)
  {
    return jewels::unexpected(filter_result.error());
  }
  std::pmr::unordered_set<std::pmr::string> filtered_log_file_set{memory_resource_};
  for (auto& log_file : filter_result.value())
  {
    filtered_log_file_set.insert(std::move(log_file));
  }
  std::pmr::list<reader::MessageChunkHandle> message_chunk_list{memory_resource_};
  std::pmr::unordered_map<std::string_view, std::pmr::list<reader::MessageChunkHandle>> persistent_message_chunk_map{
    memory_resource_};
  for (auto& reader_state : *file_reader_state_ptr_)
  {
    if (filtered_log_file_set.contains(reader_state.get_file_uri().string()))
    {
      auto chunk_list_result = reader_state.get_message_chunk_list(maybe_desired_channels, maybe_log_interval);
      if (const auto process_result = process_chunk_list_result(
            memory_resource_, chunk_list_result, maybe_log_interval, persistent_message_chunk_map, message_chunk_list);
          !process_result)
      {
        return jewels::unexpected(process_result.error());
      }
    }
  }
  prune_persistent_message_chunks(maybe_log_interval, persistent_message_chunk_map, message_chunk_list);
  message_chunk_list.sort();
  duplicate_message_filter_ =
    std::allocate_shared<DuplicateMessageFilter, std::pmr::polymorphic_allocator<DuplicateMessageFilter>>(
      memory_resource_, memory_resource_, duplicate_message_filter_size, duplicate_message_filter_expiration_interval);
  message_reader_ptr_ = std::allocate_shared<MessageReader, std::pmr::polymorphic_allocator<MessageReader>>(
    memory_resource_,
    memory_resource_,
    std::move(message_chunk_list),
    log_metadata_helper_ptr_->get_persistent_channels(),
    decompress_option);
  return {};
}

void Reader::close()
{
  message_reader_ptr_ = {};
  duplicate_message_filter_ = {};
}

[[nodiscard]] Reader::operator bool()
{
  return message_reader_ptr_ && *message_reader_ptr_;
}

[[nodiscard]] LogExpected<LoggedMessage> Reader::read_next()
{
  while (true)
  {
    if (!message_reader_ptr_)
    {
      jewels::log_cerr_error("Failed to read from {}: Not open", uri_str_);
      return jewels::unexpected(LogError::not_open);
    }
    auto read_result = message_reader_ptr_->read_next();
    if (!read_result)
    {
      return read_result;
    }
    if (duplicate_message_filter_->is_duplicate(
          read_result->channel_name, read_result->transmit_time, read_result->sequence_number))
    {
      continue;
    }
    // populate the message encoding
    if (auto channel_metadata = get_channel_metadata(read_result->channel_name))
    {
      read_result->message_encoding = channel_metadata->message_encoding;
    }
    return read_result;
  }
}

[[nodiscard]] LogExpected<jewels::memory::ObjectPtr<const std::pmr::unordered_set<std::pmr::string>>>
Reader::get_channels()
{
  if (!log_metadata_helper_ptr_)
  {
    if (const auto helper_result = initialize_log_metadata_helper(); !helper_result)
    {
      return jewels::unexpected(helper_result.error());
    }
  }
  return jewels::memory::make_non_null_from_ref(log_metadata_helper_ptr_->get_channels());
}

[[nodiscard]] LogExpected<jewels::memory::ObjectPtr<const std::pmr::map<std::string_view, LoggedChannelMetadata>>>
Reader::get_metadata()
{
  if (!metadata_map_ptr_)
  {
    if (!file_reader_state_ptr_)
    {
      if (const auto init_result = initialize_file_reader_state(); !init_result)
      {
        return jewels::unexpected(init_result.error());
      }
    }
    std::pmr::map<std::string_view, LoggedChannelMetadata> metadata_map{memory_resource_};
    for (auto& reader_state : *file_reader_state_ptr_)
    {
      const auto metadata_result = reader_state.get_metadata();
      if (!metadata_result)
      {
        if (metadata_result.error() == LogError::s3_access_denied)
        {
          continue;
        }
        jewels::log_cerr_error(
          "Failed to load metadata from {}: {}", reader_state.get_file_uri().string(), metadata_result.error());
        return jewels::unexpected(metadata_result.error());
      }
      for (const auto& channel_metadata : std::views::values(*metadata_result.value()))
      {
        if (const auto merge_result = merge_metadata(channel_metadata, metadata_map); !merge_result)
        {
          return jewels::unexpected(merge_result.error());
        }
      }
    }
    metadata_map_ptr_ = std::allocate_shared<
      std::pmr::map<std::string_view, LoggedChannelMetadata>,
      std::pmr::polymorphic_allocator<std::pmr::map<std::string_view, LoggedChannelMetadata>>>(
      memory_resource_, std::move(metadata_map));
  }
  return jewels::memory::make_non_null_from_ref(*metadata_map_ptr_);
}

[[nodiscard]] LogExpected<LoggedChannelMetadata> Reader::get_channel_metadata(std::string_view channel_name)
{
  if (!file_reader_state_ptr_)
  {
    if (const auto init_result = initialize_file_reader_state(); !init_result)
    {
      return jewels::unexpected(init_result.error());
    }
  }
  std::optional<std::pmr::unordered_set<std::pmr::string>> maybe_desired_channels;
  maybe_desired_channels.emplace(memory_resource_);
  // NOLINTNEXTLINE(modernize-use-emplace) Compiler doesn't accept emplace(channel_name, memory_resource_)
  maybe_desired_channels->emplace(std::pmr::string{channel_name, memory_resource_});
  const auto filter_result = log_metadata_helper_ptr_->list_log_files(maybe_desired_channels, {});
  if (!filter_result)
  {
    return jewels::unexpected(filter_result.error());
  }
  for (const auto& log_file : filter_result.value())
  {
    if (auto reader_state_iter = file_reader_state_map_.find(log_file);
        reader_state_iter != file_reader_state_map_.end())
    {
      const auto metadata_result = reader_state_iter->second->get_channel_metadata(channel_name);
      if (metadata_result)
      {
        const auto& channel_metadata = *metadata_result.value();
        return LoggedChannelMetadata{
          .channel_name = channel_metadata.channel_name,
          .message_encoding = channel_metadata.message_encoding,
          .channel_type = channel_metadata.channel_type,
          .schema_name = channel_metadata.schema_name,
          .schema_encoding = channel_metadata.schema_encoding,
          .schema_definition = channel_metadata.schema_definition,
        };
      }
      if (metadata_result.error() != LogError::unknown_channel && metadata_result.error() != LogError::s3_access_denied)
      {
        return jewels::unexpected(metadata_result.error());
      }
    }
  }
  return jewels::unexpected(LogError::unknown_channel);
}

[[nodiscard]] LogExpected<jewels::memory::ObjectPtr<const LogMetrics>> Reader::get_metrics()
{
  if (!metrics_ptr_)
  {
    if (!file_reader_state_ptr_)
    {
      if (const auto init_result = initialize_file_reader_state(); !init_result)
      {
        return jewels::unexpected(init_result.error());
      }
    }
    LogMetrics metrics{
      .message_count = 0U,
      .byte_count = 0U,
      .transmit_time_interval = {},
      .metrics_map = std::pmr::map<std::string_view, LoggedChannelMetrics>{memory_resource_},
    };
    for (auto& reader_state : *file_reader_state_ptr_)
    {
      const auto metrics_result = reader_state.get_metrics();
      if (!metrics_result)
      {
        if (metrics_result.error() == LogError::s3_access_denied)
        {
          continue;
        }
        jewels::log_cerr_error(
          "Failed to load metrics from {}: {}", reader_state.get_file_uri().string(), metrics_result.error());
        return jewels::unexpected(metrics_result.error());
      }
      const auto& log_metrics = *metrics_result.value();
      combine_log_metrics(log_metrics, metrics);
    }
    metrics_ptr_ = std::allocate_shared<LogMetrics, std::pmr::polymorphic_allocator<LogMetrics>>(
      memory_resource_, std::move(metrics));
  }
  return jewels::memory::make_non_null_from_ref(*metrics_ptr_);
}

[[nodiscard]] LogExpected<LogInterval> Reader::get_log_interval()
{
  if (!log_metadata_helper_ptr_)
  {
    if (const auto helper_result = initialize_log_metadata_helper(); !helper_result)
    {
      return jewels::unexpected(helper_result.error());
    }
  }
  return log_metadata_helper_ptr_->get_transmit_time_interval();
}

[[nodiscard]] LogExpected<void> Reader::initialize_log_metadata_helper()
{
  if (!log_metadata_helper_ptr_)
  {
    const auto uri_result = LogUri::try_make(uri_str_, memory_resource_);
    if (!uri_result)
    {
      jewels::log_cerr_error("Failed to open {}: Invalid log URI", uri_str_);
      return jewels::unexpected(LogError::invalid_log_uri);
    }
    auto helper_result = make_log_metadata_helper(memory_resource_, uri_result.value(), chunk_reader_factory_);
    if (!helper_result)
    {
      jewels::log_cerr_error("Failed to create log metadata helper for {}: {}", uri_str_, helper_result.error());
      return jewels::unexpected(helper_result.error());
    }
    log_metadata_helper_ptr_ = std::move(helper_result).value();
  }
  return {};
}

[[nodiscard]] LogExpected<void> Reader::initialize_file_reader_state()
{
  if (!log_metadata_helper_ptr_)
  {
    if (const auto helper_result = initialize_log_metadata_helper(); !helper_result)
    {
      return jewels::unexpected(helper_result.error());
    }
  }
  if (!file_reader_state_ptr_)
  {
    auto list_result = log_metadata_helper_ptr_->list_log_files({}, {});
    if (!list_result)
    {
      jewels::log_cerr_error("Failed to list log files under {}: {}", uri_str_, list_result.error());
      return jewels::unexpected(list_result.error());
    }
    auto& log_file_uris = list_result.value();
    std::pmr::vector<FileReaderState> reader_state_vector{memory_resource_};
    reader_state_vector.reserve(log_file_uris.size());
    for (auto& file_uri : log_file_uris)
    {
      auto reader_result = chunk_reader_factory_.make_chunk_reader(file_uri);
      if (!reader_result)
      {
        jewels::log_cerr_error("Failed to create chunk reader for {}: {}", file_uri, reader_result.error());
        return jewels::unexpected(reader_result.error());
      }
      if (const auto open_result = reader_result.value()->open(); !open_result)
      {
        jewels::log_cerr_error("Failed to open chunk reader for {}: {}", file_uri, reader_result.error());
        return jewels::unexpected(open_result.error());
      }
      reader_state_vector.emplace_back(memory_resource_, std::move(reader_result).value(), chunk_compressor_ptr_);
      file_reader_state_map_.emplace(
        std::move(file_uri), jewels::memory::make_non_null_from_ref(reader_state_vector.back()));
    }
    file_reader_state_ptr_ = std::allocate_shared<
      std::pmr::vector<FileReaderState>,
      std::pmr::polymorphic_allocator<std::pmr::vector<FileReaderState>>>(
      memory_resource_, std::move(reader_state_vector));
  }
  return {};
}

} // namespace clockwork_logging::offboard
