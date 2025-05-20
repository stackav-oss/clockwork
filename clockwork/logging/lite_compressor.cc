// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/lite_compressor.hh"

#include "clockwork/logging/nolint_helper.hh"
#include "jewels/aligner/aligner.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

namespace clockwork_logging
{

namespace
{

/// Minimum size of a chunk of zero uint64s
constexpr size_t min_zero_chunk_size = 3U;

/// Find the first zero in a span of uint64_t
/// @param[in] data Span of uint64_t
/// @param[in] start_index Search start index
/// @return Index of the first zero or the size of the span if no zero is found
[[nodiscard]] size_t find_first_zero(std::span<const uint64_t> data, size_t start_index)
{
  for (auto i = start_index; i < data.size(); ++i)
  {
    if (data[i] == 0U)
    {
      return i;
    }
  }
  return data.size();
}

/// Find the first non-zero in a span of uint64_t
/// @param[in] data Span of uint64_t
/// @param[in] start_index Search start index
/// @return Index of the first non-zero or the size of the span if no zero is found
[[nodiscard]] size_t find_first_non_zero(std::span<const uint64_t> data, size_t start_index)
{
  for (auto i = start_index; i < data.size(); ++i)
  {
    if (data[i] != 0U)
    {
      return i;
    }
  }
  return data.size();
}

} // namespace

LiteCompressor::SpanCursor::SpanCursor(std::span<const std::span<const std::byte>> data_spans)
  : data_spans_(data_spans)
{
}

[[nodiscard]] LogExpected<void> LiteCompressor::SpanCursor::copy_out(std::span<std::byte> dest_span)
{
  size_t dest_offset = 0U;
  while (span_index_ < data_spans_.size() && dest_offset < dest_span.size())
  {
    if (span_offset_ == data_spans_[span_index_].size())
    {
      ++span_index_;
      span_offset_ = 0U;
      continue;
    }
    const auto bytes_to_copy = std::min(dest_span.size() - dest_offset, data_spans_[span_index_].size() - span_offset_);
    std::memcpy(&dest_span[dest_offset], &data_spans_[span_index_][span_offset_], bytes_to_copy);
    span_offset_ += bytes_to_copy;
    dest_offset += bytes_to_copy;
  }
  if (dest_offset < dest_span.size())
  {
    return jewels::unexpected(LogError::decompression_failure);
  }
  return {};
}

[[nodiscard]] LogExpected<std::span<const std::byte>> LiteCompressor::SpanCursor::zero_copy_out(size_t byte_count)
{
  if (span_index_ < data_spans_.size() && span_offset_ == data_spans_[span_index_].size())
  {
    ++span_index_;
    span_offset_ = 0U;
  }
  if (span_index_ >= data_spans_.size())
  {
    return jewels::unexpected(LogError::decompression_failure);
  }
  const auto bytes_to_zero_copy = std::min(byte_count, data_spans_[span_index_].size() - span_offset_);
  const auto data_span = std::span{&data_spans_[span_index_][span_offset_], bytes_to_zero_copy};
  span_offset_ += bytes_to_zero_copy;
  return data_span;
}

[[nodiscard]] bool LiteCompressor::SpanCursor::empty() const
{
  return span_index_ == data_spans_.size() ||
         (span_index_ + 1U == data_spans_.size() && span_offset_ == data_spans_[span_index_].size());
}

LiteCompressor::LiteCompressor(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)),
    byte_counts_(memory_resource_),
    data_spans_(memory_resource_),
    buffer_(memory_resource_),
    zeros_buffer_(memory_resource_),
    zero_copy_spans_(memory_resource_)
{
}

[[nodiscard]] std::span<const std::span<const std::byte>> LiteCompressor::compress(std::span<const std::byte> data)
{
  byte_counts_.resize(2U);
  byte_counts_.at(1U) = static_cast<int32_t>(data.size());
  data_spans_.resize(1U);

  // Compression operates on aligned 64 bit values. Any data before the aligned region isn't compressed.
  using Aligner = jewels::Aligner<sizeof(uint64_t)>;
  const auto start_offset = static_cast<size_t>(Aligner::ptr_aligned_remainder(data.data()));
  if (start_offset != 0U)
  {
    byte_counts_.emplace_back(static_cast<int32_t>(start_offset));
    data_spans_.emplace_back(data.data(), start_offset);
  }

  if (start_offset < data.size())
  {
    // Compress the data that is aligned to 64 bits.
    const auto aligned_data = nolint_helper::byte_span_to_value_span<uint64_t>(
      std::span{&data[start_offset], ((data.size() - start_offset) / sizeof(uint64_t) * sizeof(uint64_t))});
    compress_aligned_data(aligned_data);

    // Any data after the aligned region isn't compressed.
    if (const auto end_remainder = data.size() - start_offset - (aligned_data.size() * sizeof(uint64_t));
        end_remainder != 0U)
    {
      byte_counts_.emplace_back(static_cast<int32_t>(end_remainder));
      data_spans_.emplace_back(&data[start_offset + (aligned_data.size() * sizeof(uint64_t))], end_remainder);
    }
  }
  byte_counts_.at(0U) = static_cast<int32_t>(byte_counts_.size() * sizeof(int32_t));
  data_spans_.at(0U) = std::as_bytes(std::span(byte_counts_.data(), byte_counts_.size()));
  return {data_spans_.data(), data_spans_.size()};
}

void LiteCompressor::compress_aligned_data(std::span<const uint64_t> aligned_data)
{
  size_t offset = 0U;
  size_t non_zero_count = 0U;
  while (offset < aligned_data.size())
  {
    const auto prev_offset = offset;
    if (aligned_data[offset] == 0U)
    {
      offset = find_first_non_zero(aligned_data, offset + 1U);
      if (offset - prev_offset < min_zero_chunk_size)
      {
        non_zero_count += offset - prev_offset;
      }
      else
      {
        if (non_zero_count != 0U)
        {
          byte_counts_.emplace_back(static_cast<int32_t>(non_zero_count * sizeof(uint64_t)));
          data_spans_.emplace_back(
            std::as_bytes(std::span{&aligned_data[prev_offset - non_zero_count], non_zero_count}));
          non_zero_count = 0U;
        }
        byte_counts_.push_back(-static_cast<int32_t>((offset - prev_offset) * sizeof(int64_t)));
      }
    }
    else
    {
      offset = find_first_zero(aligned_data, offset + 1U);
      non_zero_count += offset - prev_offset;
    }
  }
  if (non_zero_count != 0U)
  {
    byte_counts_.emplace_back(static_cast<int32_t>(non_zero_count * sizeof(uint64_t)));
    data_spans_.emplace_back(std::as_bytes(std::span{&aligned_data[offset - non_zero_count], non_zero_count}));
  }
}

[[nodiscard]] LogExpected<std::span<const std::byte>>
LiteCompressor::decompress(std::span<const std::span<const std::byte>> data_spans)
{
  SpanCursor cursor{data_spans};
  int32_t counts_size{};
  if (const auto copy_result = cursor.copy_out(std::as_writable_bytes(std::span{&counts_size, 1U})); !copy_result)
  {
    return jewels::unexpected(copy_result.error());
  }
  byte_counts_.resize(static_cast<size_t>(counts_size - 1) / sizeof(int32_t));
  if (const auto copy_result =
        cursor.copy_out(std::as_writable_bytes(std::span{byte_counts_.data(), byte_counts_.size()}));
      !copy_result)
  {
    return jewels::unexpected(copy_result.error());
  }
  buffer_.resize(static_cast<size_t>(byte_counts_.at(0U)));
  return decompress_common(
    cursor, std::span{byte_counts_.data(), byte_counts_.size()}, std::span{buffer_.data(), buffer_.size()});
}

[[nodiscard]] LogExpected<std::span<const std::span<const std::byte>>>
LiteCompressor::zero_copy_decompress(std::span<const std::span<const std::byte>> data_spans)
{
  SpanCursor cursor{data_spans};
  int32_t counts_size{};
  if (const auto copy_result = cursor.copy_out(std::as_writable_bytes(std::span{&counts_size, 1U})); !copy_result)
  {
    return jewels::unexpected(copy_result.error());
  }
  byte_counts_.resize(static_cast<size_t>(counts_size - 1) / sizeof(int32_t));
  if (const auto copy_result =
        cursor.copy_out(std::as_writable_bytes(std::span{byte_counts_.data(), byte_counts_.size()}));
      !copy_result)
  {
    return jewels::unexpected(copy_result.error());
  }
  size_t max_zero_chunk_size = 0U;
  for (const auto byte_count : byte_counts_)
  {
    if (byte_count < 0)
    {
      max_zero_chunk_size = std::max(max_zero_chunk_size, static_cast<size_t>(-byte_count));
    }
  }
  if (max_zero_chunk_size > zeros_buffer_.size())
  {
    zeros_buffer_.resize(max_zero_chunk_size);
  }
  int32_t decompressed_size = 0U;
  zero_copy_spans_.clear();
  zero_copy_spans_.reserve(byte_counts_.size() * 2U);
  for (size_t i = 1U; i < byte_counts_.size(); ++i)
  {
    const auto byte_count = byte_counts_[i];

    if (byte_count < 0)
    {
      zero_copy_spans_.emplace_back(zeros_buffer_.data(), static_cast<size_t>(-byte_count));
      decompressed_size += -byte_count;
    }
    else if (byte_count > 0)
    {
      auto bytes_to_zero_copy = static_cast<size_t>(byte_count);
      while (bytes_to_zero_copy != 0U)
      {
        const auto zero_copy_result = cursor.zero_copy_out(bytes_to_zero_copy);
        if (!zero_copy_result)
        {
          return jewels::unexpected(zero_copy_result.error());
        }
        zero_copy_spans_.emplace_back(zero_copy_result.value());
        bytes_to_zero_copy -= zero_copy_result->size();
      }
      decompressed_size += byte_count;
    }
  }
  if (decompressed_size != byte_counts_[0U] || !cursor.empty())
  {
    return jewels::unexpected(LogError::decompression_failure);
  }
  return std::span<const std::span<const std::byte>>{zero_copy_spans_.data(), zero_copy_spans_.size()};
}

[[nodiscard]] LogExpected<std::span<const std::byte>>
LiteCompressor::decompress(std::span<const std::span<const std::byte>> data_spans, std::span<std::byte> dest_span)
{
  SpanCursor cursor{data_spans};
  int32_t counts_size{};
  if (const auto copy_result = cursor.copy_out(std::as_writable_bytes(std::span{&counts_size, 1U})); !copy_result)
  {
    return jewels::unexpected(copy_result.error());
  }
  byte_counts_.resize(static_cast<size_t>(counts_size - 1) / sizeof(int32_t));
  if (const auto copy_result =
        cursor.copy_out(std::as_writable_bytes(std::span{byte_counts_.data(), byte_counts_.size()}));
      !copy_result)
  {
    return jewels::unexpected(copy_result.error());
  }
  if (dest_span.size() != static_cast<size_t>(byte_counts_.at(0U)))
  {
    return jewels::unexpected(LogError::decompression_failure);
  }
  return decompress_common(cursor, std::span{byte_counts_.data(), byte_counts_.size()}, dest_span);
}

[[nodiscard]] LogExpected<std::span<const std::byte>> LiteCompressor::decompress(std::span<const std::byte> data)
{
  return decompress(std::span<const std::span<const std::byte>>{&data, 1U});
}

[[nodiscard]] LogExpected<std::span<const std::span<const std::byte>>>
LiteCompressor::zero_copy_decompress(std::span<const std::byte> data)
{
  return zero_copy_decompress(std::span<const std::span<const std::byte>>{&data, 1U});
}

[[nodiscard]] LogExpected<std::span<const std::byte>>
LiteCompressor::decompress(std::span<const std::byte> data, std::span<std::byte> dest_span)
{
  return decompress(std::span<const std::span<const std::byte>>{&data, 1U}, dest_span);
}

[[nodiscard]] LogExpected<std::span<const std::byte>> LiteCompressor::decompress_common(
  SpanCursor cursor, std::span<const int32_t> byte_counts, std::span<std::byte> dest_span)
{
  size_t dest_offset = 0U;
  for (size_t i = 1U; i < byte_counts.size(); ++i)
  {
    const auto byte_count = byte_counts[i];

    if (byte_count < 0)
    {
      const auto bytes_to_zero = static_cast<size_t>(-byte_count);
      if (dest_offset + bytes_to_zero > dest_span.size())
      {
        return jewels::unexpected(LogError::decompression_failure);
      }
      std::memset(&dest_span[dest_offset], 0, bytes_to_zero);
      dest_offset += bytes_to_zero;
    }
    else if (byte_count > 0)
    {
      const auto bytes_to_copy = static_cast<size_t>(byte_count);
      if (dest_offset + bytes_to_copy > dest_span.size())
      {
        return jewels::unexpected(LogError::decompression_failure);
      }
      if (const auto copy_result = cursor.copy_out(std::span{&dest_span[dest_offset], bytes_to_copy}); !copy_result)
      {
        return jewels::unexpected(copy_result.error());
      }
      dest_offset += bytes_to_copy;
    }
  }
  if (dest_offset != dest_span.size() || !cursor.empty())
  {
    return jewels::unexpected(LogError::decompression_failure);
  }
  return dest_span;
}

[[nodiscard]] LogExpected<size_t> LiteCompressor::get_decompressed_size(std::span<const std::byte> data)
{
  return get_decompressed_size(std::span<const std::span<const std::byte>>{&data, 1U});
}

[[nodiscard]] LogExpected<size_t>
LiteCompressor::get_decompressed_size(std::span<const std::span<const std::byte>> data_spans)
{
  SpanCursor cursor{data_spans};
  std::array<int32_t, 2U> counts{};
  if (const auto copy_result = cursor.copy_out(std::as_writable_bytes(std::span{counts})); !copy_result)
  {
    return jewels::unexpected(copy_result.error());
  }
  if (counts.at(0U) < 2)
  {
    return jewels::unexpected(LogError::decompression_failure);
  }
  return static_cast<size_t>(counts.at(1U));
}

} // namespace clockwork_logging
