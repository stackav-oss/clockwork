// IWYU pragma: private, include "clockwork/logging/onboard/buffered_reader_base.hh"
#pragma once

#include "clockwork/logging/onboard/buffered_reader_base.hh"

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
#include <ranges>
#include <span>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace clockwork_logging::onboard
{

template <typename Derived, typename Policy>
BufferedReaderBase<Derived, Policy>::BufferedReaderBase(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)),
    buffer_pool_(memory_resource_, read_window_size),
    read_window_buffers_storage_(read_window_size, memory_resource_),
    read_window_buffers_(std::in_place, read_window_buffers_storage_.data(), read_window_size),
    read_window_spans_storage_(read_window_size, memory_resource_),
    read_window_spans_(std::in_place, read_window_spans_storage_.data(), read_window_size),
    zero_copy_array_spans_(
      {std::pmr::vector<std::span<const std::byte>>{memory_resource_},
       std::pmr::vector<std::span<const std::byte>>{memory_resource_},
       std::pmr::vector<std::span<const std::byte>>{memory_resource_}})
{
  std::ranges::for_each(zero_copy_array_spans_, [](auto& span) { span.reserve(read_window_size); });
}

template <typename Derived, typename Policy>
[[nodiscard]] BufferedReaderBase<Derived, Policy>::operator bool() const noexcept
{
  return static_cast<const Derived*>(this)->is_open() && !maybe_reader_error_ && get_bytes_remaining() != 0U;
}

template <typename Derived, typename Policy>
[[nodiscard]] size_t BufferedReaderBase<Derived, Policy>::get_bytes_remaining() const noexcept
{
  if (!static_cast<const Derived*>(this)->is_open() || maybe_reader_error_)
  {
    return 0U;
  }
  return file_size_ - read_window_offset_ - first_buffer_offset_;
}

template <typename Derived, typename Policy>
[[nodiscard]] size_t BufferedReaderBase<Derived, Policy>::get_window_offset() const noexcept
{
  if (!static_cast<const Derived*>(this)->is_open() || maybe_reader_error_)
  {
    return 0U;
  }
  return read_window_offset_ + first_buffer_offset_;
}

template <typename Derived, typename Policy>
[[nodiscard]] LogExpected<void> BufferedReaderBase<Derived, Policy>::copy_out(size_t offset, std::span<std::byte> dest)
{
  auto zero_copy_result = zero_copy_out(offset, dest.size(), 0U, 0U);
  if (!zero_copy_result)
  {
    return jewels::unexpected(zero_copy_result.error());
  }
  copy_data_spans(zero_copy_result->at(0U), dest);
  return {};
}

template <typename Derived, typename Policy>
[[nodiscard]] LogExpected<std::span<const std::span<const std::byte>>>
BufferedReaderBase<Derived, Policy>::zero_copy_out(size_t offset, size_t length)
{
  const auto zero_copy_result = zero_copy_out(offset, length, 0U, 0U);
  if (!zero_copy_result)
  {
    return jewels::unexpected(zero_copy_result.error());
  }
  return zero_copy_result->at(0U);
}

template <typename Derived, typename Policy>
[[nodiscard]] LogExpected<typename BufferedReaderBase<Derived, Policy>::ZeroCopyArray>
BufferedReaderBase<Derived, Policy>::zero_copy_out(
  size_t offset, size_t header_length, size_t data_length, size_t checksum_length)
{
  if (!static_cast<const Derived*>(this)->is_open())
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (maybe_reader_error_)
  {
    return jewels::unexpected{*maybe_reader_error_};
  }
  clear_zero_copy_array_spans();
  const auto file_bytes_remaining = get_bytes_remaining();
  const auto total_length = header_length + data_length + checksum_length;
  if (total_length > max_read_size)
  {
    jewels::log_cerr_error("Read size {} is greater than the max read size ({})", total_length, max_read_size);
    maybe_reader_error_ = LogError::failed;
    return jewels::unexpected{*maybe_reader_error_};
  }
  if (offset + total_length > file_bytes_remaining)
  {
    jewels::log_cerr_warn("Cannot copy out {} bytes, bytes remaining is {}", total_length, file_bytes_remaining);
    return jewels::unexpected{LogError::end_of_log};
  }
  while (window_size_ < offset + total_length)
  {
    if (const auto grow_result = static_cast<Derived*>(this)->grow_window(); !grow_result)
    {
      return jewels::unexpected(grow_result.error());
    }
  }
  auto window_spans_iter = read_window_spans_.begin();
  auto window_buffers_iter = read_window_buffers_.begin();
  size_t buffer_offset = first_buffer_offset_;
  auto bytes_to_skip = offset;
  while (bytes_to_skip != 0U)
  {
    const auto window_span = *window_spans_iter;
    if (bytes_to_skip < window_span.size() - buffer_offset)
    {
      buffer_offset += bytes_to_skip;
      bytes_to_skip = 0U;
      break;
    }
    bytes_to_skip -= window_span.size() - buffer_offset;
    buffer_offset = 0U;
    ++window_spans_iter;
    ++window_buffers_iter;
  }
  const std::array read_lengths = {header_length, data_length, checksum_length};
  ZeroCopyArray read_spans;
  for (size_t read_index = 0U; read_index < read_lengths.size(); ++read_index)
  {
    size_t bytes_to_zero_copy = read_lengths.at(read_index);
    while (bytes_to_zero_copy > 0U)
    {
      const auto window_span = *window_spans_iter;
      size_t span_size = std::min(bytes_to_zero_copy, window_span.size() - buffer_offset);
      zero_copy_array_spans_.at(read_index)
        .emplace_back(&jewels::at(window_span, static_cast<ssize_t>(buffer_offset)), span_size);
      bytes_to_zero_copy -= span_size;
      buffer_offset += span_size;
      if (buffer_offset >= window_span.size())
      {
        buffer_offset = 0U;
        ++window_spans_iter;
        ++window_buffers_iter;
      }
    }
    read_spans.at(read_index) = std::span<const std::span<const std::byte>>{
      zero_copy_array_spans_.at(read_index).data(), zero_copy_array_spans_.at(read_index).size()};
  }
  return read_spans;
}

template <typename Derived, typename Policy>
[[nodiscard]] LogExpected<void> BufferedReaderBase<Derived, Policy>::advance(size_t count)
{
  if (!static_cast<const Derived*>(this)->is_open())
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (maybe_reader_error_)
  {
    return jewels::unexpected{*maybe_reader_error_};
  }
  if (count > window_size_)
  {
    jewels::log_cerr_error("Tried to advance past the end of the window");
    maybe_reader_error_ = LogError::failed;
    return jewels::unexpected{*maybe_reader_error_};
  }
  clear_zero_copy_array_spans();
  advance_internal(count);
  return {};
}

template <typename Derived, typename Policy>
[[nodiscard]] LogExpected<void> BufferedReaderBase<Derived, Policy>::skip_pad_bytes()
{
  if (!static_cast<const Derived*>(this)->is_open())
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (maybe_reader_error_)
  {
    return jewels::unexpected{*maybe_reader_error_};
  }
  clear_zero_copy_array_spans();
  while (read_window_offset_ + first_buffer_offset_ < file_size_)
  {
    if (read_window_spans_.empty())
    {
      if (const auto grow_result = static_cast<Derived*>(this)->grow_window(); !grow_result)
      {
        return grow_result;
      }
    }
    const auto first_buffer_span = *read_window_spans_.begin();
    while (first_buffer_offset_ < first_buffer_span.size())
    {
      if (jewels::at(first_buffer_span, static_cast<ssize_t>(first_buffer_offset_)) != std::byte{0})
      {
        return {};
      }
      ++first_buffer_offset_;
      --window_size_;
    }
    remove_first_buffer_from_window();
  }
  return {};
}

template <typename Derived, typename Policy>
[[nodiscard]] BufferedReaderBase<Derived, Policy>::ReadMetrics
BufferedReaderBase<Derived, Policy>::get_read_metrics() const
{
  return read_metrics_;
}

template <typename Derived, typename Policy>
[[nodiscard]] LogExpected<void> BufferedReaderBase<Derived, Policy>::advance(std::span<const std::byte> pattern)
{
  if (!static_cast<const Derived*>(this)->is_open())
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (maybe_reader_error_)
  {
    return jewels::unexpected{*maybe_reader_error_};
  }
  if (pattern.empty() || (pattern.size() > read_buffer_size))
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
  while (read_window_offset_ + first_buffer_offset_ < file_size_)
  {
    if (read_window_spans_.empty())
    {
      if (const auto grow_result = static_cast<Derived*>(this)->grow_window(); !grow_result)
      {
        return grow_result;
      }
    }
    const auto advance_result = advance_in_first_buffer(pattern);
    if (!advance_result)
    {
      return jewels::unexpected(advance_result.error());
    }
    if (*advance_result)
    {
      return {};
    }
    remove_first_buffer_from_window();
  }
  return {};
}

template <typename Derived, typename Policy>
[[nodiscard]] LogExpected<bool>
BufferedReaderBase<Derived, Policy>::advance_in_first_buffer(std::span<const std::byte> pattern)
{
  const auto first_buffer_span = *read_window_spans_.begin();
  const auto search_span =
    first_buffer_span.subspan(first_buffer_offset_, first_buffer_span.size() - first_buffer_offset_);
  const auto search_state = search_for_pattern_in_span(pattern, search_span);
  first_buffer_offset_ += search_state.match_offset;
  window_size_ -= search_state.match_offset;
  if (search_state.match_count == pattern.size())
  {
    return true;
  }
  if (search_state.match_count == 0U)
  {
    return false;
  }
  if (read_window_offset_ + first_buffer_offset_ + pattern.size() >= file_size_)
  {
    first_buffer_offset_ += search_state.match_count;
    window_size_ -= search_state.match_count;
    return false;
  }
  const auto bytes_to_compare = pattern.size() - search_state.match_count;
  const auto zero_copy_result = zero_copy_out(search_state.match_count, bytes_to_compare);
  if (!zero_copy_result)
  {
    return jewels::unexpected(zero_copy_result.error());
  }
  if (!std::ranges::equal(*zero_copy_result | std::views::join, pattern.last(bytes_to_compare)))
  {
    first_buffer_offset_ += search_state.match_count;
    window_size_ -= search_state.match_count;
    return false;
  }
  return true;
}

template <typename Derived, typename Policy>
[[nodiscard]] typename BufferedReaderBase<Derived, Policy>::PatternSearchState
BufferedReaderBase<Derived, Policy>::search_for_pattern_in_span(
  std::span<const std::byte> pattern, std::span<const std::byte> data)
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
  return PatternSearchState{match_count == 0 ? data.size() : match_offset, match_count};
}

template <typename Derived, typename Policy>
void BufferedReaderBase<Derived, Policy>::remove_first_buffer_from_window()
{
  const auto first_buffer_span = *read_window_spans_.begin();
  read_window_offset_ += first_buffer_span.size();
  first_buffer_offset_ = 0U;
  read_window_spans_.pop_front();
  read_window_buffers_.pop_front();
}

template <typename Derived, typename Policy>
void BufferedReaderBase<Derived, Policy>::advance_internal(size_t count)
{
  auto bytes_to_advance = count;
  while (bytes_to_advance != 0U)
  {
    const auto window_span = *read_window_spans_.begin();
    if (bytes_to_advance < window_span.size() - first_buffer_offset_)
    {
      first_buffer_offset_ += bytes_to_advance;
      bytes_to_advance = 0U;
      break;
    }
    bytes_to_advance -= window_span.size() - first_buffer_offset_;
    remove_first_buffer_from_window();
  }
  window_size_ -= count;
}

template <typename Derived, typename Policy>
void BufferedReaderBase<Derived, Policy>::clear_zero_copy_array_spans()
{
  std::ranges::for_each(zero_copy_array_spans_, [](auto& span) { span.clear(); });
}

} // namespace clockwork_logging::onboard
