// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/message_chunk_writer.hh"

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/wrapping_counter.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

namespace
{

/// Copy a span of data spans into the destination buffer
/// Caller is expected to ensure that the desination is large enough
/// @param[in,out] dest Destination buffer
/// @param[in] data_spans Data spans to copy
void copy_spans(std::span<std::byte> dest, std::span<const std::span<const std::byte>> data_spans)
{
  size_t dest_offset = 0U;
  for (const auto data : data_spans)
  {
    std::memcpy(&dest[dest_offset], data.data(), data.size());
    dest_offset += data.size();
  }
}

} // namespace

MessageChunkWriter::MessageChunkWriter(
  jewels::memory::MemoryResource memory_resource,
  MessageChunkIndexFormat index_format,
  CompressionType compression_type)
  : memory_resource_(std::move(memory_resource)),
    index_format_(index_format),
    compression_type_(compression_type),
    data_(memory_resource_),
    index_(memory_resource_)
{
}

[[nodiscard]] bool MessageChunkWriter::is_empty() const noexcept
{
  return data_.empty();
}

[[nodiscard]] bool MessageChunkWriter::is_full() const noexcept
{
  return data_.size() >= target_file_chunk_size;
}

[[nodiscard]] LogExpected<void> MessageChunkWriter::add_message(size_t data_size, const ZeroCopyLoggedMessage& message)
{
  if (data_size > max_message_data_size)
  {
    return jewels::unexpected(LogError::message_data_exceeds_max_size);
  }
  if (message.header.size() > max_message_header_size)
  {
    return jewels::unexpected(LogError::message_header_exceeds_max_size);
  }
  if (is_empty())
  {
    transmit_time_interval_ = LogInterval{message.transmit_time};
  }
  else
  {
    transmit_time_interval_.add_timestamp(message.transmit_time);
  }
  reserve_capacity(message_chunk_message_header_size + message.header.size() + data_size);
  const auto chunk_offset = data_.size();
  data_.resize(data_.size() + message_chunk_message_header_size + message.header.size() + data_size);
  MessageChunkMessageHeader message_header{
    .data_size = static_cast<uint32_t>(data_size),
    .header_size = static_cast<uint16_t>(message.header.size()),
    .flags =
      MessageChunkMessageHeaderFlags{
        .is_repeated_persistent = static_cast<uint8_t>(message.is_repeated_persistent ? 1U : 0U),
        .is_lite_compressed = static_cast<uint8_t>(message.is_lite_compressed ? 1U : 0U),
      },
    .sequence_number = message.sequence_number,
    .log_time_ns = message.log_time.get_nanoseconds(),
    .transmit_time_ns = message.transmit_time.get_nanoseconds(),
  };
  std::memcpy(&data_.at(chunk_offset), &message_header, message_chunk_message_header_size);
  if (!message.header.empty())
  {
    std::memcpy(
      &data_.at(chunk_offset + message_chunk_message_header_size), message.header.data(), message.header.size());
  }
  if (data_size != 0U)
  {
    copy_spans(
      std::span{&data_.at(chunk_offset + message_chunk_message_header_size + message.header.size()), data_size},
      message.data);
  }
  add_index_entry(
    MessageChunkIndexEntryV2{
      .transmit_time_ns = message.transmit_time.get_nanoseconds(),
      .sequence_number = WrappingCounter<uint32_t>{message.sequence_number},
      .chunk_offset = static_cast<uint32_t>(chunk_offset),
    });
  return {};
}

[[nodiscard]] LogExpected<IndexChunkIndexEntry>
MessageChunkWriter::write_chunk(const ChunkCompressor& chunk_compressor, ChunkWriter& chunk_writer)
{
  std::sort(index_.begin(), index_.end());
  const auto index_offset = data_.size();
  size_t index_size{};
  const void* index_data_ptr{};
  std::pmr::vector<MessageChunkIndexEntryV1> v1_index_storage{memory_resource_};
  if (index_format_ == MessageChunkIndexFormat::v2)
  {
    index_size = index_.size() * message_chunk_index_entry_v2_size;
    index_data_ptr = index_.data();
  }
  else
  {
    index_size = index_.size() * message_chunk_index_entry_v1_size;
    v1_index_storage.reserve(index_.size());
    for (const auto& index_entry : index_)
    {
      v1_index_storage.emplace_back(
        MessageChunkIndexEntryV1{
          .transmit_time_ns = index_entry.transmit_time_ns,
          .chunk_offset = index_entry.chunk_offset,
        });
    }
    index_data_ptr = v1_index_storage.data();
  }
  reserve_capacity(index_size + message_chunk_trailer_size);
  if (!index_.empty())
  {
    data_.resize(data_.size() + index_size);
    std::memcpy(&data_.at(index_offset), index_data_ptr, index_size);
  }
  MessageChunkTrailer trailer{
    .index_offset = static_cast<uint32_t>(index_offset),
    .flags =
      MessageChunkTrailerFlags{
        .use_message_index_version_2 = index_format_ == MessageChunkIndexFormat::v2 ? uint8_t{1U} : uint8_t{0U},
      },
  };
  const auto trailer_offset = data_.size();
  data_.resize(data_.size() + message_chunk_trailer_size);
  std::memcpy(&data_.at(trailer_offset), &trailer, message_chunk_trailer_size);
  auto compress_result = chunk_compressor.compress_chunk(std::move(data_), compression_type_);
  if (!compress_result)
  {
    jewels::log_cerr_error("Failed to compress message chunk for {}", chunk_writer.file_uri().path());
    return jewels::unexpected(compress_result.error());
  }
  const auto chunk_interval = transmit_time_interval_;
  const auto chunk_size = compress_result.value().size();
  reinitialize();
  const auto write_result = chunk_writer.write_chunk(std::move(compress_result).value());
  if (!write_result)
  {
    return jewels::unexpected(write_result.error());
  }
  return IndexChunkIndexEntry{
    .min_transmit_time_ns = chunk_interval.get_start_timestamp().get_nanoseconds(),
    .max_transmit_time_ns = chunk_interval.get_end_timestamp().get_nanoseconds(),
    .location =
      ChunkLocation{
        .chunk_offset = write_result.value(),
        .chunk_size = static_cast<uint32_t>(chunk_size),
      },
  };
}

void MessageChunkWriter::reserve_capacity(size_t length)
{
  if (data_.size() + length > data_.capacity())
  {
    data_.reserve(std::max(initial_buffer_capacity, data_.size() + length + buffer_growth_size));
  }
}

void MessageChunkWriter::add_index_entry(const MessageChunkIndexEntryV2& entry)
{
  if (index_.size() == index_.capacity())
  {
    index_.reserve(std::max(initial_index_capacity, index_.size() + index_growth_size));
  }
  index_.push_back(entry);
}

void MessageChunkWriter::reinitialize()
{
  data_ = std::pmr::vector<std::byte>(memory_resource_);
  index_.resize(0U);
  transmit_time_interval_ = {};
}

} // namespace clockwork_logging::offboard
