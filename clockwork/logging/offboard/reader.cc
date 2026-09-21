// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/reader.hh"

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/log_metadata_file_helper.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <future>
#include <iterator>
#include <ranges>
#include <span>
#include <unordered_map>
#include <utility>

namespace clockwork_logging::offboard
{

using jewels::ok;
using jewels::Out;

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
        .is_amended = channel_metadata.is_amended,
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

Reader::Reader(
  jewels::memory::MemoryResource memory_resource,
  std::string_view uri_str,
  std::shared_ptr<ChunkReaderWriterFactory<>> chunk_reader_factory)
  : memory_resource_(std::move(memory_resource)),
    uri_str_(uri_str, memory_resource_),
    chunk_reader_factory_(std::move(chunk_reader_factory)),
    chunk_compressor_ptr_(
      jewels::memory::allocate_shared<ChunkCompressor, std::pmr::polymorphic_allocator<ChunkCompressor>>(
        memory_resource_, memory_resource_)),
    log_file_reader_map_(memory_resource_)
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
  if (!log_file_readers_ptr_)
  {
    if (const auto init_result = initialize_log_file_readers(); !init_result)
    {
      return jewels::unexpected(init_result.error());
    }
  }
  std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
  if (const auto get_map_outcome =
        log_metadata_helper_ptr_->get_log_file_map(Out{log_file_map}, maybe_desired_channels, maybe_log_interval, {});
      !ok(get_map_outcome))
  {
    return jewels::unexpected(get_map_outcome.get());
  }
  std::pmr::list<reader::MessageChunkHandle> message_chunk_list{memory_resource_};
  std::pmr::unordered_map<std::string_view, std::pmr::list<reader::MessageChunkHandle>> persistent_message_chunk_map{
    memory_resource_};
  for (auto& log_file_reader : *log_file_readers_ptr_)
  {
    const auto& log_file_entry = log_file_map.find(log_file_reader.get_file_uri().string());
    if (log_file_entry != log_file_map.end())
    {
      auto chunk_list_result = log_file_reader.get_message_chunk_list(log_file_entry->second, maybe_log_interval);
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
    if (!log_file_readers_ptr_)
    {
      if (const auto init_result = initialize_log_file_readers(); !init_result)
      {
        return jewels::unexpected(init_result.error());
      }
    }
    std::pmr::map<std::string_view, LoggedChannelMetadata> metadata_map{memory_resource_};
    for (auto& log_file_reader : *log_file_readers_ptr_)
    {
      const auto metadata_result = log_file_reader.get_metadata();
      if (!metadata_result)
      {
        if (metadata_result.error() == LogError::s3_access_denied)
        {
          continue;
        }
        jewels::log_cerr_error(
          "Failed to load metadata from {}: {}", log_file_reader.get_file_uri().string(), metadata_result.error());
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
  const auto metadata_result = get_metadata();
  if (!metadata_result)
  {
    return jewels::unexpected(metadata_result.error());
  }
  const auto& metadata_map = *metadata_result.value();
  const auto metadata_iter = metadata_map.find(channel_name);
  if (metadata_iter == metadata_map.end())
  {
    return jewels::unexpected(LogError::unknown_channel);
  }
  const auto& channel_metadata = metadata_iter->second;
  return LoggedChannelMetadata{
    .channel_name = channel_metadata.channel_name,
    .message_encoding = channel_metadata.message_encoding,
    .channel_type = channel_metadata.channel_type,
    .schema_name = channel_metadata.schema_name,
    .schema_encoding = channel_metadata.schema_encoding,
    .schema_definition = channel_metadata.schema_definition,
    .is_amended = channel_metadata.is_amended,
  };
}

[[nodiscard]] LogExpected<jewels::memory::ObjectPtr<const LogMetrics>> Reader::get_metrics()
{
  if (!metrics_ptr_)
  {
    if (!log_file_readers_ptr_)
    {
      if (const auto init_result = initialize_log_file_readers(); !init_result)
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
    for (auto& log_file_reader : *log_file_readers_ptr_)
    {
      const auto metrics_result = log_file_reader.get_metrics();
      if (!metrics_result)
      {
        if (metrics_result.error() == LogError::s3_access_denied)
        {
          continue;
        }
        jewels::log_cerr_error(
          "Failed to load metrics from {}: {}", log_file_reader.get_file_uri().string(), metrics_result.error());
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
    std::shared_ptr<LogMetadataHelperInterface> helper_ptr;
    if (const auto make_outcome = make_log_metadata_helper(
          memory_resource_, uri_result.value(), {}, false, *chunk_reader_factory_, Out{helper_ptr});
        !ok(make_outcome))
    {
      jewels::log_cerr_error("Failed to create log metadata helper for {}: {}", uri_str_, make_outcome.get());
      return jewels::unexpected(make_outcome.get());
    }
    log_metadata_helper_ptr_ = std::move(helper_ptr);
  }
  return {};
}

[[nodiscard]] LogExpected<void> Reader::initialize_log_file_readers()
{
  if (!log_metadata_helper_ptr_)
  {
    if (const auto helper_result = initialize_log_metadata_helper(); !helper_result)
    {
      return jewels::unexpected(helper_result.error());
    }
  }
  if (!log_file_readers_ptr_)
  {
    std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>> log_file_map;
    if (const auto get_map_outcome = log_metadata_helper_ptr_->get_log_file_map(Out{log_file_map}, {}, {}, {});
        !ok(get_map_outcome))
    {
      jewels::log_cerr_error("Failed to list log files under {}: {}", uri_str_, get_map_outcome.get());
      return jewels::unexpected(get_map_outcome.get());
    }
    std::pmr::vector<LogFileReader> log_file_reader_vector{memory_resource_};
    log_file_reader_vector.reserve(log_file_map.size());
    for (const auto& [file_uri, desired_channels] : log_file_map)
    {
      auto reader_result = chunk_reader_factory_->make_chunk_reader(file_uri);
      if (!reader_result)
      {
        jewels::log_cerr_error("Failed to create chunk reader for {}: {}", file_uri, reader_result.error());
        return jewels::unexpected(reader_result.error());
      }
      if (const auto open_result = reader_result.value()->open(); !open_result)
      {
        jewels::log_cerr_error("Failed to open chunk reader for {}: {}", file_uri, open_result.error());
        return jewels::unexpected(open_result.error());
      }
      log_file_reader_vector.emplace_back(
        memory_resource_, std::move(reader_result).value(), chunk_compressor_ptr_, desired_channels);
      log_file_reader_map_.emplace(file_uri, jewels::memory::make_non_null_from_ref(log_file_reader_vector.back()));
    }
    log_file_readers_ptr_ = std::allocate_shared<
      std::pmr::vector<LogFileReader>,
      std::pmr::polymorphic_allocator<std::pmr::vector<LogFileReader>>>(
      memory_resource_, std::move(log_file_reader_vector));
  }
  return {};
}

} // namespace clockwork_logging::offboard
