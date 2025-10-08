// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/message_chunk_reader.hh"

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <memory_resource>
#include <utility>

namespace clockwork_logging::offboard
{

MessageChunkReader::MessageChunkReader(
  jewels::memory::MemoryResource memory_resource,
  std::string_view channel_name,
  ChannelType channel_type,
  std::optional<LogInterval> maybe_log_interval,
  CompressionType compression_type)
  : memory_resource_(std::move(memory_resource)),
    channel_name_(channel_name, memory_resource_),
    channel_type_(channel_type),
    maybe_log_interval_(maybe_log_interval),
    compression_type_(compression_type)
{
}

[[nodiscard]] LogExpected<void> MessageChunkReader::read_chunk(
  const ChunkLocation& location, const ChunkCompressor& chunk_compressor, ChunkReader& chunk_reader)
{
  auto read_result = chunk_reader.read_chunk(location.chunk_offset, location.chunk_size);
  if (!read_result)
  {
    return jewels::unexpected(read_result.error());
  }
  auto decompress_result = chunk_compressor.decompress_chunk(std::move(read_result).value(), compression_type_);
  if (!decompress_result)
  {
    jewels::log_cerr_error("Failed to decompress message chunk from {}", chunk_reader.file_uri().path());
    return jewels::unexpected(decompress_result.error());
  }
  data_ = std::move(decompress_result).value();
  const auto trailer_offset = data_.size() - message_chunk_trailer_size;
  const auto trailer_result = nolint_helper::byte_span_to_value_ptr<MessageChunkTrailer>(
    std::span{&data_.at(trailer_offset), message_chunk_trailer_size});
  if (!trailer_result)
  {
    return jewels::unexpected(trailer_result.error());
  }
  const auto* trailer_ptr = trailer_result.value();
  size_t index_size{};
  if (trailer_ptr->flags.use_message_index_version_2)
  {
    index_size = (trailer_offset - trailer_ptr->index_offset) / message_chunk_index_entry_v2_size;
    index_span_ = nolint_helper::byte_span_to_value_span<MessageChunkIndexEntryV2>(
      std::span{&data_.at(trailer_ptr->index_offset), index_size * message_chunk_index_entry_v2_size});
  }
  else
  {
    index_size = (trailer_offset - trailer_ptr->index_offset) / message_chunk_index_entry_v1_size;
    const auto v1_index_span = nolint_helper::byte_span_to_value_span<MessageChunkIndexEntryV1>(
      std::span{&data_.at(trailer_ptr->index_offset), index_size * message_chunk_index_entry_v1_size});
    index_storage_ = std::pmr::vector<MessageChunkIndexEntryV2>{memory_resource_};
    index_storage_.reserve(index_size);
    for (const auto& index_entry : v1_index_span)
    {
      const auto chunk_offset = index_entry.chunk_offset;
      const auto message_header_result = nolint_helper::byte_span_to_value_ptr<MessageChunkMessageHeader>(
        std::span{&data_[chunk_offset], message_chunk_message_header_size});
      if (!message_header_result)
      {
        return jewels::unexpected(message_header_result.error());
      }
      const auto* message_header_ptr = message_header_result.value();
      index_storage_.emplace_back(
        MessageChunkIndexEntryV2{
          .transmit_time_ns = index_entry.transmit_time_ns,
          .sequence_number = WrappingCounter<uint32_t>{message_header_ptr->sequence_number},
          .chunk_offset = chunk_offset,
        });
    }
    std::sort(index_storage_.begin(), index_storage_.end());
    index_span_ = std::span<const MessageChunkIndexEntryV2>{index_storage_};
  }
  next_message_index_ = 0U;
  if (is_empty() || !maybe_log_interval_)
  {
    return {};
  }
  while (!index_span_.empty() &&
         (index_span_.back().transmit_time_ns > maybe_log_interval_->get_end_timestamp().get_nanoseconds()))
  {
    index_span_ = index_span_.first(index_span_.size() - 1U);
  }
  if (channel_type_ == ChannelType::persistent)
  {
    while ((index_span_.size() > 1U) &&
           (index_span_.front().transmit_time_ns < maybe_log_interval_->get_start_timestamp().get_nanoseconds()) &&
           (index_span_[1U].transmit_time_ns <= maybe_log_interval_->get_start_timestamp().get_nanoseconds()))
    {
      index_span_ = index_span_.last(index_span_.size() - 1U);
    }
  }
  else
  {
    while (!index_span_.empty() &&
           (index_span_.front().transmit_time_ns < maybe_log_interval_->get_start_timestamp().get_nanoseconds()))
    {
      index_span_ = index_span_.last(index_span_.size() - 1U);
    }
  }
  return {};
}

[[nodiscard]] bool MessageChunkReader::is_empty() const noexcept
{
  return next_message_index_ >= index_span_.size();
}

[[nodiscard]] LogExpected<MessageChunkReader::MessageIdentifier>
MessageChunkReader::get_next_message_identifier() const noexcept
{
  if (is_empty())
  {
    return jewels::unexpected(LogError::end_of_chunk);
  }
  return MessageIdentifier{
    .transmit_time = LogTimestamp{index_span_[next_message_index_].transmit_time_ns},
    .sequence_number = WrappingCounter<uint32_t>{index_span_[next_message_index_].sequence_number},
    .channel_name = channel_name_,
  };
}

[[nodiscard]] ChannelType MessageChunkReader::get_channel_type() const noexcept
{
  return channel_type_;
}

[[nodiscard]] LogExpected<LoggedMessage> MessageChunkReader::read_next()
{
  if (is_empty())
  {
    return jewels::unexpected(LogError::end_of_chunk);
  }
  const auto chunk_offset = index_span_[next_message_index_].chunk_offset;
  ++next_message_index_;
  const auto message_header_result = nolint_helper::byte_span_to_value_ptr<MessageChunkMessageHeader>(
    std::span{&data_[chunk_offset], message_chunk_message_header_size});
  if (!message_header_result)
  {
    return jewels::unexpected(message_header_result.error());
  }
  const auto* message_header_ptr = message_header_result.value();
  const auto* header_ptr = &data_[chunk_offset + message_chunk_message_header_size];
  const auto* data_ptr = &data_[chunk_offset + message_chunk_message_header_size + message_header_ptr->header_size];
  auto transmit_time = LogTimestamp{message_header_ptr->transmit_time_ns};
  auto is_repeated_persistent = message_header_ptr->flags.is_repeated_persistent != 0U;
  if (
    maybe_log_interval_ && (channel_type_ == ChannelType::persistent) &&
    (transmit_time < maybe_log_interval_->get_start_timestamp()))
  {
    transmit_time = maybe_log_interval_->get_start_timestamp();
    is_repeated_persistent = true;
  }
  return LoggedMessage{
    .channel_name = channel_name_,
    .sequence_number = message_header_ptr->sequence_number,
    .log_time = LogTimestamp{message_header_ptr->log_time_ns},
    .transmit_time = transmit_time,
    .header = std::span<const std::byte>{header_ptr, message_header_ptr->header_size},
    .data = std::span<const std::byte>{data_ptr, message_header_ptr->data_size},
    .is_repeated_persistent = is_repeated_persistent,
    .is_lite_compressed = message_header_ptr->flags.is_lite_compressed != 0U,
  };
}

} // namespace clockwork_logging::offboard
