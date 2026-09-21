// IWYU pragma: private, include "clockwork/logging/offboard/writer.hh"
#pragma once

#include "clockwork/logging/offboard/writer.hh"

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/lite_compressor_interface.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/offboard/async_work_queue.hh"
#include "clockwork/logging/offboard/channel_message_writer.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/index_chunk_writer.hh"
#include "clockwork/logging/offboard/log_file_trailer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/metadata_chunk_writer.hh"
#include "clockwork/logging/offboard/metrics_chunk_writer.hh"
#include "clockwork/logging/offboard/reader_types.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/v1/log_metadata.pb.h"
#include "clockwork/logging/offboard/writer_config.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/tachyon_lite_compressor.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <fmt/base.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <iterator>
#include <list>
#include <memory>
#include <memory_resource>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

using jewels::ok;
using jewels::Out;

template <typename S3UtilsType>
Writer<S3UtilsType>::FileWriterState::FileWriterState(
  jewels::memory::MemoryResource memory_resource,
  MessageChunkIndexFormat message_chunk_index_format,
  std::string_view file_uri_prefix,
  jewels::memory::NonNullSharedPtr<AsyncWorkQueue> async_work_queue_ptr)
  : memory_resource_(std::move(memory_resource)),
    message_chunk_index_format_(message_chunk_index_format),
    metadata_writer_(memory_resource_),
    index_writer_(memory_resource_),
    metrics_writer_(memory_resource_),
    channel_writer_map_(memory_resource_),
    log_file_metadata_list_(memory_resource_),
    file_uri_prefix_(file_uri_prefix, memory_resource_),
    chunk_compressor_ptr_(
      jewels::memory::allocate_shared<ChunkCompressor, std::pmr::polymorphic_allocator<ChunkCompressor>>(
        memory_resource_, memory_resource_)),
    async_work_queue_ptr_(std::move(async_work_queue_ptr))
{
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<void>
Writer<S3UtilsType>::FileWriterState::open(ChunkReaderWriterFactory<S3UtilsType>& chunk_writer_factory)
{
  if (chunk_writer_ptr_)
  {
    return jewels::unexpected(LogError::already_open);
  }
  std::pmr::string file_uri{memory_resource_};
  fmt::format_to(std::back_inserter(file_uri), "{}_{}{}", file_uri_prefix_, file_sequence_number_, log_file_suffix);
  ++file_sequence_number_;
  const auto uri_result = LogUri::try_make(file_uri, memory_resource_);
  if (!uri_result)
  {
    jewels::log_cerr_error("Invalid log URI: {}", file_uri);
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  log_file_metadata_list_.push_back(
    LogFileMetadata{
      .log_file_name = std::pmr::string{uri_result.value().filename(), memory_resource_},
      .maybe_transmit_time_interval = std::nullopt,
    });
  auto writer_result = chunk_writer_factory.make_chunk_writer(file_uri);
  if (!writer_result)
  {
    jewels::log_cerr_error("Failed to create chunk writer for {}: {}", file_uri, writer_result.error());
    return jewels::unexpected(writer_result.error());
  }
  if (const auto open_result = writer_result.value()->open(); !open_result)
  {
    jewels::log_cerr_error("Failed to open {}: {}", file_uri, open_result.error());
    return jewels::unexpected(open_result.error());
  }
  chunk_writer_ptr_ = writer_result.value().get();
  return {};
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<void>
Writer<S3UtilsType>::FileWriterState::split_log_file(ChunkReaderWriterFactory<S3UtilsType>& chunk_writer_factory)
{
  if (const auto close_result = close(); !close_result)
  {
    return jewels::unexpected(close_result.error());
  }
  return open(chunk_writer_factory);
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<size_t> Writer<S3UtilsType>::FileWriterState::get_file_size() const
{
  if (!chunk_writer_ptr_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  return chunk_writer_ptr_->get_file_size();
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<ChunkWriter::WriteMetrics> Writer<S3UtilsType>::FileWriterState::close()
{
  if (!chunk_writer_ptr_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  for (auto& [channel_id, channel_writer] : channel_writer_map_)
  {
    const auto& finalize_result = channel_writer.finalize();
    if (!finalize_result)
    {
      jewels::log_cerr_error("Failed to finalize channel writer chunk for {}", chunk_writer_ptr_->file_uri().string());
      return jewels::unexpected(finalize_result.error());
    }
    index_writer_.add_channel_index(channel_id, finalize_result.value());
  }
  const auto index_result = index_writer_.write_chunk(*chunk_compressor_ptr_, *chunk_writer_ptr_);
  if (!index_result)
  {
    jewels::log_cerr_error("Failed to write index chunk for {}", chunk_writer_ptr_->file_uri().string());
    return jewels::unexpected(index_result.error());
  }
  const auto metrics_result = metrics_writer_.write_chunk(*chunk_compressor_ptr_, *chunk_writer_ptr_);
  if (!metrics_result)
  {
    jewels::log_cerr_error("Failed to write metrics chunk for {}", chunk_writer_ptr_->file_uri().string());
    return jewels::unexpected(metrics_result.error());
  }
  const auto metadata_result = metadata_writer_.write_chunk(*chunk_compressor_ptr_, *chunk_writer_ptr_);
  if (!metadata_result)
  {
    jewels::log_cerr_error("Failed to write metadata chunk for {}", chunk_writer_ptr_->file_uri().string());
    return jewels::unexpected(metadata_result.error());
  }
  if (const auto trailer_result = write_log_file_trailer(
        memory_resource_,
        metadata_result.value(),
        metrics_result.value(),
        index_result.value(),
        *chunk_writer_ptr_,
        *chunk_compressor_ptr_);
      !trailer_result)
  {
    jewels::log_cerr_error("Failed to write trailer chunk for {}", chunk_writer_ptr_->file_uri().string());
    return jewels::unexpected(trailer_result.error());
  }
  const auto close_result = chunk_writer_ptr_->close();
  if (!close_result)
  {
    jewels::log_cerr_error("Failed to close {}: {}", chunk_writer_ptr_->file_uri().string(), close_result.error());
    return jewels::unexpected(close_result.error());
  }
  write_metrics_ += close_result.value();
  channel_writer_map_.clear();
  chunk_writer_ptr_.reset();
  return {write_metrics_};
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<void> Writer<S3UtilsType>::FileWriterState::create_channel(
  const LoggedChannelMetadata& channel_metadata, CompressionType compression_type)
{
  if (!chunk_writer_ptr_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  const auto metadata_result = metadata_writer_.add_channel(
    reader::LoggedChannelInfo{
      .compression_type = compression_type,
      .channel_name = std::pmr::string{channel_metadata.channel_name, memory_resource_},
      .message_encoding = channel_metadata.message_encoding,
      .channel_type = channel_metadata.channel_type,
      .schema_name = std::pmr::string{channel_metadata.schema_name, memory_resource_},
      .schema_encoding = channel_metadata.schema_encoding,
      .schema_definition = std::pmr::string{channel_metadata.schema_definition, memory_resource_},
      .is_amended = channel_metadata.is_amended,
    });
  if (!metadata_result)
  {
    jewels::log_cerr_error("Failed to create channel {}: {}", channel_metadata.channel_name, metadata_result.error());
    return jewels::unexpected(metadata_result.error());
  }
  return {};
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<void> Writer<S3UtilsType>::FileWriterState::write(const ZeroCopyLoggedMessage& message)
{
  if (!chunk_writer_ptr_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  const auto channel_id_result = metadata_writer_.get_channel_id(message.channel_name);
  if (!channel_id_result)
  {
    jewels::log_cerr_error("Failed to get channel ID for {}: {}", message.channel_name, channel_id_result.error());
    return jewels::unexpected(channel_id_result.error());
  }
  const auto channel_id = channel_id_result.value();
  auto writer_map_iter = channel_writer_map_.find(channel_id);
  if (writer_map_iter == channel_writer_map_.end())
  {
    const auto compression_type_result = metadata_writer_.get_compression_type(channel_id);
    if (!compression_type_result)
    {
      return jewels::unexpected(compression_type_result.error());
    }
    writer_map_iter = channel_writer_map_
                        .emplace(
                          std::piecewise_construct,
                          std::forward_as_tuple(channel_id),
                          std::forward_as_tuple(
                            memory_resource_,
                            message_chunk_index_format_,
                            compression_type_result.value(),
                            chunk_compressor_ptr_,
                            jewels::memory::NonNullSharedPtr<ChunkWriter>{chunk_writer_ptr_},
                            async_work_queue_ptr_))
                        .first;
  }
  const auto data_size = std::accumulate(
    message.data.begin(), message.data.end(), size_t{0U}, [](size_t lhs, auto& rhs) { return lhs + rhs.size(); });
  if (const auto write_result = writer_map_iter->second.add_message(data_size, message); !write_result)
  {
    jewels::log_cerr_error("Failed to write to channel {}: {}", message.channel_name, write_result.error());
    return jewels::unexpected(write_result.error());
  }
  auto& maybe_transmit_time_interval = log_file_metadata_list_.back().maybe_transmit_time_interval;
  if (maybe_transmit_time_interval)
  {
    maybe_transmit_time_interval->add_timestamp(message.transmit_time);
  }
  else
  {
    maybe_transmit_time_interval = LogInterval{message.transmit_time};
  }
  metrics_writer_.count_message(channel_id, message.transmit_time, message.header.size() + data_size);
  return {};
}

template <typename S3UtilsType>
[[nodiscard]] const std::pmr::list<typename Writer<S3UtilsType>::FileWriterState::LogFileMetadata>&
Writer<S3UtilsType>::FileWriterState::get_log_file_metadata_list() const
{
  return log_file_metadata_list_;
}

template <typename S3UtilsType>
Writer<S3UtilsType>::Writer(
  jewels::memory::MemoryResource memory_resource, MessageChunkIndexFormat message_chunk_index_format)
  : memory_resource_(std::move(memory_resource)),
    lite_compressor_map_(memory_resource_),
    default_lite_compressor_(jewels::memory::make_pmr_shared<LiteCompressor>(memory_resource_, memory_resource_)),
    message_chunk_index_format_(message_chunk_index_format),
    async_work_queue_ptr_(
      jewels::memory::allocate_shared<AsyncWorkQueue, std::pmr::polymorphic_allocator<AsyncWorkQueue>>(
        memory_resource_, num_worker_threads)),
    chunk_writer_factory_(memory_resource_),
    file_name_prefix_to_writer_map_(memory_resource_),
    channel_name_to_writer_map_(memory_resource_),
    persistent_channels_(memory_resource_),
    channel_names_(memory_resource_),
    writer_config_(memory_resource_)
{
}

template <typename S3UtilsType>
[[nodiscard]] bool Writer<S3UtilsType>::is_open() const
{
  return maybe_file_writer_state_.has_value();
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<void>
Writer<S3UtilsType>::open(std::string_view uri_str, std::string_view config_str, OverwriteMode overwrite_mode)
{
  if (maybe_file_writer_state_)
  {
    return jewels::unexpected(LogError::already_open);
  }
  if (overwrite_mode != OverwriteMode::overwrite)
  {
    const auto exists_result = chunk_writer_factory_.exists(uri_str);
    if (!exists_result)
    {
      jewels::log_cerr_error("Failed to create writer for {}: {}", uri_str, exists_result.error());
      return jewels::unexpected(exists_result.error());
    }
    if (exists_result.value())
    {
      jewels::log_cerr_error("Failed to create writer for {}: {}", uri_str, "Directory exists");
      return jewels::unexpected(LogError::log_already_exists);
    }
  }
  if (const auto config_result = writer_config_.set_config_proto(config_str); !config_result)
  {
    jewels::log_cerr_error("Faileld to set writer config: {}", config_result.error());
    return jewels::unexpected(config_result.error());
  }
  if (const auto mkdir_result = chunk_writer_factory_.create_directories(uri_str); !mkdir_result)
  {
    jewels::log_cerr_error("Failed to create chunk writer for {}: {}", uri_str, mkdir_result.error());
    return jewels::unexpected(mkdir_result.error());
  }
  uri_str_ = std::pmr::string{uri_str, memory_resource_};
  maybe_file_writer_state_.emplace(memory_resource_);
  return {};
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<void> Writer<S3UtilsType>::split_log_files()
{
  if (!maybe_file_writer_state_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  for (auto& file_writer : maybe_file_writer_state_.value())
  {
    if (const auto split_result = file_writer.split_log_file(chunk_writer_factory_); !split_result)
    {
      return jewels::unexpected(split_result.error());
    }
  }
  return {};
}

template <typename S3UtilsType>
LogOutcome Writer<S3UtilsType>::close(
  Out<ChunkWriter::WriteMetrics> write_metrics, Out<::clockwork::logging::offboard::v1::LogMetadata> log_metadata)
{
  if (!maybe_file_writer_state_)
  {
    return LogError::not_open;
  }
  std::pmr::vector<std::future<LogExpected<ChunkWriter::WriteMetrics>>> close_futures{memory_resource_};
  close_futures.reserve(maybe_file_writer_state_->size());
  for (auto& file_writer : *maybe_file_writer_state_)
  {
    close_futures.emplace_back(std::async(std::launch::async, [&file_writer]() { return file_writer.close(); }));
  }
  LogOutcome close_outcome = LogError::success;
  *write_metrics = {};
  for (auto& close_future : close_futures)
  {
    const auto writer_result = close_future.get();
    if (!writer_result)
    {
      close_outcome = writer_result.error();
    }
    else if (ok(close_outcome))
    {
      *write_metrics += writer_result.value();
    }
  }
  if (ok(close_outcome))
  {
    *log_metadata = get_log_metadata_protobuf();
  }
  file_name_prefix_to_writer_map_.clear();
  channel_name_to_writer_map_.clear();
  persistent_channels_.clear();
  channel_names_.clear();
  maybe_file_writer_state_.reset();
  return close_outcome;
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<ChunkWriter::WriteMetrics> Writer<S3UtilsType>::close()
{
  ChunkWriter::WriteMetrics write_metrics;
  ::clockwork::logging::offboard::v1::LogMetadata log_metadata;
  if (const auto close_outcome = close(Out{write_metrics}, Out{log_metadata}); !ok(close_outcome))
  {
    return jewels::unexpected(close_outcome.get());
  }
  std::pmr::string log_metadata_uri{memory_resource_};
  fmt::format_to(std::back_inserter(log_metadata_uri), "{}/{}", uri_str_, log_metadata_filename);
  if (const auto write_result =
        chunk_writer_factory_.write_text_proto(log_metadata_uri, log_metadata_proto_header, log_metadata);
      !write_result)
  {
    return jewels::unexpected(write_result.error());
  }
  return write_metrics;
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<void> Writer<S3UtilsType>::create_channel(const LoggedChannelMetadata& channel_metadata)
{
  if (!maybe_file_writer_state_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (channel_name_to_writer_map_.contains(channel_metadata.channel_name))
  {
    return jewels::unexpected(LogError::channel_already_exists);
  }
  auto channel_config = writer_config_.get_channel_config(channel_metadata.channel_name);
  auto writer_iter = file_name_prefix_to_writer_map_.find(channel_config.file_name_prefix);
  if (writer_iter == file_name_prefix_to_writer_map_.end())
  {
    std::pmr::string log_file_uri{memory_resource_};
    log_file_uri.reserve(uri_str_.size() + channel_config.file_name_prefix.size() + 1U);
    log_file_uri.append(uri_str_).append("/").append(channel_config.file_name_prefix);
    maybe_file_writer_state_->emplace_back(
      memory_resource_, message_chunk_index_format_, log_file_uri, async_work_queue_ptr_);
    writer_iter = file_name_prefix_to_writer_map_
                    .emplace(
                      std::pmr::string{channel_config.file_name_prefix, memory_resource_},
                      jewels::memory::make_non_null_from_ref(maybe_file_writer_state_->back()))
                    .first;
    if (const auto open_result = writer_iter->second->open(chunk_writer_factory_); !open_result)
    {
      return jewels::unexpected(open_result.error());
    }
  }
  if (const auto create_result = writer_iter->second->create_channel(channel_metadata, channel_config.compression_type);
      !create_result)
  {
    return jewels::unexpected(create_result.error());
  }
  std::pmr::string channel_name{channel_metadata.channel_name, memory_resource_};
  channel_names_.push_back(std::move(channel_name));
  if (channel_metadata.channel_type == ChannelType::persistent)
  {
    persistent_channels_.emplace(channel_names_.back());
  }
  channel_name_to_writer_map_.emplace(channel_names_.back(), writer_iter->second);
  if (channel_metadata.schema_encoding == SchemaEncoding::clockwork_tachyon)
  {
    try
    {
      auto lite_compressor = clockwork::serialization::TachyonLiteCompressor::make_compressor(
        memory_resource_,
        channel_names_.back(),
        std::as_bytes(std::span{channel_metadata.schema_definition.data(), channel_metadata.schema_definition.size()}));
      lite_compressor_map_.emplace(channel_names_.back(), std::move(lite_compressor));
    }
    catch (const std::runtime_error& exc)
    {
      jewels::log_cerr_warn("Failed to make compressor for {}, using default", channel_metadata.channel_name);
      lite_compressor_map_.emplace(channel_names_.back(), default_lite_compressor_);
    }
  }
  else
  {
    lite_compressor_map_.emplace(channel_names_.back(), default_lite_compressor_);
  }
  return {};
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<void> Writer<S3UtilsType>::write(const LoggedMessage& message)
{
  auto data = std::span{&message.data, 1U};
  bool is_lite_compressed = message.is_lite_compressed;
  if (!is_lite_compressed)
  {
    std::span<const std::span<const std::byte>> compressed_data;
    const auto lite_compressor_iter = lite_compressor_map_.find(message.channel_name);
    if (lite_compressor_iter == lite_compressor_map_.end())
    {
      compressed_data = default_lite_compressor_->compress(message.data);
    }
    else
    {
      compressed_data = lite_compressor_iter->second->compress(message.data);
    }
    const auto compressed_data_size = std::accumulate(
      compressed_data.begin(),
      compressed_data.end(),
      size_t{0U},
      [](size_t lhs, auto& rhs) { return lhs + rhs.size(); });
    if (compressed_data_size < message.data.size())
    {
      data = compressed_data;
      is_lite_compressed = true;
    }
  }
  return write(
    ZeroCopyLoggedMessage{
      .channel_name = message.channel_name,
      .sequence_number = message.sequence_number,
      .log_time = message.log_time,
      .transmit_time = message.transmit_time,
      .header = message.header,
      .data = data,
      .is_repeated_persistent = message.is_repeated_persistent,
      .message_encoding = message.message_encoding,
      .is_lite_compressed = is_lite_compressed,
    });
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<void> Writer<S3UtilsType>::write(const ZeroCopyLoggedMessage& message)
{
  if (!maybe_file_writer_state_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  const auto writer_iter = channel_name_to_writer_map_.find(message.channel_name);
  if (writer_iter == channel_name_to_writer_map_.end())
  {
    return jewels::unexpected(LogError::unknown_channel);
  }
  if (const auto write_result = writer_iter->second->write(message); !write_result)
  {
    return jewels::unexpected(write_result.error());
  }
  const auto get_size_result = writer_iter->second->get_file_size();
  if (!get_size_result)
  {
    return jewels::unexpected(get_size_result.error());
  }
  if (get_size_result.value() > max_file_size)
  {
    if (const auto split_result = writer_iter->second->split_log_file(chunk_writer_factory_); !split_result)
    {
      return jewels::unexpected(split_result.error());
    }
  }
  return {};
}

template <typename S3UtilsType>
[[nodiscard]] ::clockwork::logging::offboard::v1::LogMetadata Writer<S3UtilsType>::get_log_metadata_protobuf() const
{
  std::pmr::unordered_map<std::pmr::string, ::clockwork::logging::offboard::v1::LogWriterMetadata>
    prefix_to_metadata_map{memory_resource_};
  std::optional<LogInterval> maybe_transmit_time_interval;
  for (const auto& channel_name : channel_names_)
  {
    auto channel_config = writer_config_.get_channel_config(channel_name);
    auto prefix_map_iter = prefix_to_metadata_map.find(channel_config.file_name_prefix);
    if (prefix_map_iter != prefix_to_metadata_map.end())
    {
      prefix_map_iter->second.add_channel(std::string{channel_name});
      if (persistent_channels_.contains(channel_name))
      {
        prefix_map_iter->second.add_persistent_channel(std::string{channel_name});
      }
      continue;
    }
    ::clockwork::logging::offboard::v1::LogWriterMetadata writer_metadata;
    writer_metadata.add_channel(std::string{channel_name});
    if (persistent_channels_.contains(channel_name))
    {
      writer_metadata.add_persistent_channel(std::string{channel_name});
    }
    const auto& writer_ptr = file_name_prefix_to_writer_map_.at(channel_config.file_name_prefix);
    const auto& log_file_metadata_list = writer_ptr->get_log_file_metadata_list();
    for (const auto& log_file_metadata : log_file_metadata_list)
    {
      auto* file_metadata_ptr = writer_metadata.add_log_file_metadata();
      file_metadata_ptr->set_log_file_name(std::string{log_file_metadata.log_file_name});
      if (log_file_metadata.maybe_transmit_time_interval)
      {
        file_metadata_ptr->set_min_transmit_time_ns(
          log_file_metadata.maybe_transmit_time_interval->get_start_timestamp().get_nanoseconds());
        file_metadata_ptr->set_max_transmit_time_ns(
          log_file_metadata.maybe_transmit_time_interval->get_end_timestamp().get_nanoseconds());
        if (maybe_transmit_time_interval)
        {
          maybe_transmit_time_interval->add_interval(log_file_metadata.maybe_transmit_time_interval.value());
        }
        else
        {
          maybe_transmit_time_interval = log_file_metadata.maybe_transmit_time_interval;
        }
      }
    }
    prefix_to_metadata_map.emplace(std::move(channel_config.file_name_prefix), std::move(writer_metadata));
  }
  ::clockwork::logging::offboard::v1::LogMetadata log_metadata;
  for (auto& writer_metadata : std::views::values(prefix_to_metadata_map))
  {
    *log_metadata.add_log_writer_metadata() = std::move(writer_metadata);
  }
  if (maybe_transmit_time_interval)
  {
    log_metadata.set_min_transmit_time_ns(maybe_transmit_time_interval->get_start_timestamp().get_nanoseconds());
    log_metadata.set_max_transmit_time_ns(maybe_transmit_time_interval->get_end_timestamp().get_nanoseconds());
  }
  return log_metadata;
}

template <typename S3UtilsType>
template <clockwork::TappyType T>
[[nodiscard]] LogExpected<void>
Writer<S3UtilsType>::create_channel(std::string_view channel_name, ChannelType channel_type, bool is_amended)
{
  return create_channel(
    LoggedChannelMetadata{
      .channel_name = channel_name,
      .message_encoding = static_cast<MessageEncoding>(clockwork::LoggingTraits<T>::message_encoding),
      .channel_type = channel_type,
      .schema_name = clockwork::LoggingTraits<T>::schema_name,
      .schema_encoding = static_cast<SchemaEncoding>(clockwork::LoggingTraits<T>::schema_encoding),
      .schema_definition =
        std::string_view{
          clockwork::LoggingTraits<T>::schema_definition.data(), clockwork::LoggingTraits<T>::schema_definition.size()},
      .is_amended = is_amended,
    });
}

template <typename S3UtilsType>
template <clockwork::TappyType T>
[[nodiscard]] LogExpected<void> Writer<S3UtilsType>::write(
  std::string_view channel_name,
  uint32_t sequence_number,
  LogTimestamp log_time,
  LogTimestamp transmit_time,
  const T& message,
  bool is_repeated_persistent)
{
  const auto data_span = std::as_bytes(std::span{&message, 1U});
  return write(
    LoggedMessage{
      .channel_name = channel_name,
      .sequence_number = sequence_number,
      .log_time = log_time,
      .transmit_time = transmit_time,
      .header = {},
      .data = data_span,
      .is_repeated_persistent = is_repeated_persistent,
      .message_encoding = static_cast<MessageEncoding>(clockwork::LoggingTraits<T>::message_encoding),
      .is_lite_compressed = false,
    });
}

} // namespace clockwork_logging::offboard
