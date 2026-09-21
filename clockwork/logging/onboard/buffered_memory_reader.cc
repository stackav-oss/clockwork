// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/onboard/buffered_memory_reader.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/onboard/types.hh"
#include "jewels/container/at.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory_resource>
#include <optional>
#include <span>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace clockwork_logging::onboard
{

BufferedMemoryReader::BufferedMemoryReader(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)),
    zero_copy_array_spans_(
      {std::pmr::vector<std::span<const std::byte>>{memory_resource_},
       std::pmr::vector<std::span<const std::byte>>{memory_resource_},
       std::pmr::vector<std::span<const std::byte>>{memory_resource_}})
{
  std::ranges::for_each(zero_copy_array_spans_, [](auto& span) { span.reserve(1U); });
}

[[nodiscard]] LogExpected<void> BufferedMemoryReader::open(std::span<const std::byte> buffer_span)
{
  if (!buffer_span_.empty())
  {
    return jewels::unexpected(LogError::already_open);
  }
  buffer_span_ = buffer_span;
  current_buffer_offset_ = 0U;
  return {};
}

void BufferedMemoryReader::close()
{
  maybe_reader_error_ = std::nullopt;
  clear_zero_copy_array_spans();
  buffer_span_ = {};
}

[[nodiscard]] BufferedMemoryReader::operator bool() const noexcept
{
  return get_bytes_remaining() != 0U;
}

[[nodiscard]] size_t BufferedMemoryReader::get_bytes_remaining() const noexcept
{
  if (buffer_span_.empty() || maybe_reader_error_)
  {
    return 0U;
  }
  return buffer_span_.size() - current_buffer_offset_;
}

[[nodiscard]] size_t BufferedMemoryReader::get_current_offset() const noexcept
{
  if (buffer_span_.empty() || maybe_reader_error_)
  {
    return 0U;
  }
  return current_buffer_offset_;
}

[[nodiscard]] LogExpected<void> BufferedMemoryReader::copy_out(size_t offset, std::span<std::byte> dest)
{
  auto zero_copy_result = zero_copy_out(offset, dest.size(), 0U, 0U);
  if (!zero_copy_result)
  {
    return jewels::unexpected(zero_copy_result.error());
  }
  copy_data_spans(zero_copy_result->at(0U), dest);
  return {};
}

[[nodiscard]] LogExpected<std::span<const std::span<const std::byte>>>
BufferedMemoryReader::zero_copy_out(size_t offset, size_t length)
{
  const auto zero_copy_result = zero_copy_out(offset, length, 0U, 0U);
  if (!zero_copy_result)
  {
    return jewels::unexpected(zero_copy_result.error());
  }
  return zero_copy_result->at(0U);
}

[[nodiscard]] LogExpected<typename BufferedMemoryReader::ZeroCopyArray>
BufferedMemoryReader::zero_copy_out(size_t offset, size_t header_length, size_t data_length, size_t checksum_length)
{
  if (buffer_span_.empty())
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (maybe_reader_error_)
  {
    return jewels::unexpected{*maybe_reader_error_};
  }
  clear_zero_copy_array_spans();
  const auto bytes_remaining = get_bytes_remaining();
  const auto total_length = header_length + data_length + checksum_length;
  if (offset + total_length > bytes_remaining)
  {
    jewels::log_cerr_warn("Cannot copy out {} bytes, bytes remaining is {}", total_length, bytes_remaining);
    return jewels::unexpected{LogError::end_of_log};
  }
  size_t buffer_offset = current_buffer_offset_ + offset;
  const std::array read_lengths = {header_length, data_length, checksum_length};
  ZeroCopyArray read_spans;
  for (size_t read_index = 0U; read_index < read_lengths.size(); ++read_index)
  {
    if (read_lengths.at(read_index) == 0U)
    {
      static std::byte byte_value{};
      zero_copy_array_spans_.at(read_index).emplace_back(&byte_value, 0U);
    }
    else
    {
      zero_copy_array_spans_.at(read_index)
        .emplace_back(&jewels::at(buffer_span_, static_cast<ssize_t>(buffer_offset)), read_lengths.at(read_index));
      buffer_offset += read_lengths.at(read_index);
    }
    read_spans.at(read_index) = std::span<const std::span<const std::byte>>{
      zero_copy_array_spans_.at(read_index).data(), zero_copy_array_spans_.at(read_index).size()};
  }
  return read_spans;
}

[[nodiscard]] LogExpected<void> BufferedMemoryReader::advance(size_t count)
{
  if (buffer_span_.empty())
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (maybe_reader_error_)
  {
    return jewels::unexpected{*maybe_reader_error_};
  }
  if (current_buffer_offset_ + count > buffer_span_.size())
  {
    jewels::log_cerr_error("Tried to advance past the end of the window");
    maybe_reader_error_ = LogError::failed;
    return jewels::unexpected{*maybe_reader_error_};
  }
  clear_zero_copy_array_spans();
  current_buffer_offset_ += count;
  return {};
}

[[nodiscard]] LogExpected<void> BufferedMemoryReader::skip_pad_bytes()
{
  if (buffer_span_.empty())
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (maybe_reader_error_)
  {
    return jewels::unexpected{*maybe_reader_error_};
  }
  clear_zero_copy_array_spans();
  while (current_buffer_offset_ < buffer_span_.size())
  {
    if (jewels::at(buffer_span_, static_cast<ssize_t>(current_buffer_offset_)) != std::byte{0})
    {
      return {};
    }
    ++current_buffer_offset_;
  }
  return {};
}

[[nodiscard]] LogExpected<void> BufferedMemoryReader::advance(std::span<const std::byte> pattern)
{
  if (buffer_span_.empty())
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (maybe_reader_error_)
  {
    return jewels::unexpected{*maybe_reader_error_};
  }
  if (pattern.empty())
  {
    jewels::log_cerr_error("Invalid advance pattern size: {}", pattern.size());
    maybe_reader_error_ = LogError::failed;
    return jewels::unexpected{*maybe_reader_error_};
  }
  if (!std::ranges::all_of(
        pattern.last(pattern.size() - 1U),
        [first_value = pattern.front()](const auto value) { return value != first_value; }))
  {
    jewels::log_cerr_error("First byte in advance pattern cannot repeat");
    maybe_reader_error_ = LogError::failed;
    return jewels::unexpected{*maybe_reader_error_};
  }
  clear_zero_copy_array_spans();
  const auto search_span = buffer_span_.subspan(current_buffer_offset_, buffer_span_.size() - current_buffer_offset_);
  const auto search_state = search_for_pattern_in_span(pattern, search_span);
  if (search_state.match_count == pattern.size())
  {
    current_buffer_offset_ += search_state.match_offset;
  }
  else
  {
    current_buffer_offset_ = buffer_span_.size();
  }
  return {};
}

[[nodiscard]] typename BufferedMemoryReader::PatternSearchState
BufferedMemoryReader::search_for_pattern_in_span(std::span<const std::byte> pattern, std::span<const std::byte> data)
{
  size_t match_offset{};
  size_t match_count = 0U;
  for (size_t offset = 0U; offset < data.size(); ++offset)
  {
    if (jewels::at(data, static_cast<ssize_t>(offset)) == jewels::at(pattern, 0))
    {
      match_count = 1U;
      match_offset = offset;
    }
    else if (jewels::at(data, static_cast<ssize_t>(offset)) == jewels::at(pattern, static_cast<ssize_t>(match_count)))
    {
      ++match_count;
      if (match_count == pattern.size())
      {
        break;
      }
    }
    else
    {
      match_count = 0U;
    }
  }
  return PatternSearchState{
    .match_offset = match_count == 0 ? data.size() : match_offset,
    .match_count = match_count,
  };
}

void BufferedMemoryReader::clear_zero_copy_array_spans()
{
  std::ranges::for_each(zero_copy_array_spans_, [](auto& span) { span.clear(); });
}

} // namespace clockwork_logging::onboard
