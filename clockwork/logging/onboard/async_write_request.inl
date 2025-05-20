// IWYU pragma: private, include "clockwork/logging/onboard/async_write_request.hh"
#pragma once

#include "clockwork/logging/onboard/async_write_request.hh"

#include "jewels/container/at.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/time/sync_time.hh"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <optional>
#include <span>
#include <sys/uio.h>

namespace clockwork_logging::onboard
{

template <typename Policy>
[[nodiscard]] bool AsyncWriteRequest<Policy>::add_buffer(BufferReferenceType buffer_reference)
{
  pad_for_write();
  add_current_buffer_to_io_vector();
  if (is_full())
  {
    return false;
  }
  maybe_current_buffer_ = {buffer_reference.value()};
  current_buffer_offset_ = 0U;
  buffer_references_.emplace_back(std::move(buffer_reference));
  return true;
}

template <typename Policy>
[[nodiscard]] std::span<iovec> AsyncWriteRequest<Policy>::get_io_vector()
{
  pad_for_write();
  add_current_buffer_to_io_vector();
  return {io_vectors_.data(), io_vectors_.size()};
}

template <typename Policy>
void AsyncWriteRequest<Policy>::pad_for_write()
{
  if (!maybe_current_buffer_ || is_full())
  {
    return;
  }
  const auto aligned_remainder = AlignerType::aligned_remainder(current_buffer_offset_);
  std::memset(&jewels::at(*maybe_current_buffer_, static_cast<ssize_t>(current_buffer_offset_)), 0, aligned_remainder);
  write_size_ += aligned_remainder;
  current_buffer_offset_ += aligned_remainder;
  if (current_buffer_offset_ == maybe_current_buffer_->size())
  {
    add_current_buffer_to_io_vector();
  }
}

template <typename Policy>
[[nodiscard]] bool AsyncWriteRequest<Policy>::pad_for_alignment(size_t byte_count)
{
  if (!maybe_current_buffer_ || is_full())
  {
    return byte_count == 0U;
  }
  const auto misaligned_byte_count = byte_count & ~AlignerType::alignment_mask;
  const auto aligned_remainder = AlignerType::aligned_remainder(current_buffer_offset_ + misaligned_byte_count);
  const auto bytes_remaining =
    std::min(maybe_current_buffer_->size() - current_buffer_offset_, max_write_size - write_size_);
  if (bytes_remaining < aligned_remainder + misaligned_byte_count)
  {
    std::memset(&jewels::at(*maybe_current_buffer_, static_cast<ssize_t>(current_buffer_offset_)), 0, bytes_remaining);
    write_size_ += bytes_remaining;
    current_buffer_offset_ += bytes_remaining;
    add_current_buffer_to_io_vector();
    return false;
  }
  std::memset(&jewels::at(*maybe_current_buffer_, static_cast<ssize_t>(current_buffer_offset_)), 0, aligned_remainder);
  write_size_ += aligned_remainder;
  current_buffer_offset_ += aligned_remainder;
  if (current_buffer_offset_ == maybe_current_buffer_->size())
  {
    add_current_buffer_to_io_vector();
  }
  return true;
}

template <typename Policy>
[[nodiscard]] size_t
AsyncWriteRequest<Policy>::copy_data(jewels::time::SteadyTime timestamp, std::span<const std::byte> data)
{
  if (!maybe_current_buffer_ || is_full())
  {
    return 0U;
  }
  maybe_oldest_data_timestamp_ = std::min(maybe_oldest_data_timestamp_.value_or(timestamp), timestamp);
  const auto bytes_to_copy =
    std::min({data.size(), maybe_current_buffer_->size() - current_buffer_offset_, max_write_size - write_size_});
  std::memcpy(
    &jewels::at(*maybe_current_buffer_, static_cast<ssize_t>(current_buffer_offset_)), data.data(), bytes_to_copy);
  current_buffer_offset_ += bytes_to_copy;
  write_size_ += bytes_to_copy;
  if (current_buffer_offset_ == maybe_current_buffer_->size())
  {
    add_current_buffer_to_io_vector();
  }
  return bytes_to_copy;
}

template <typename Policy>
[[nodiscard]] size_t AsyncWriteRequest<Policy>::zero_copy_data(
  jewels::time::SteadyTime timestamp, std::span<const std::byte> data, MessageHandleType message_handle)
{
  if (
    (maybe_current_buffer_ && !AlignerType::is_aligned(current_buffer_offset_)) ||
    !AlignerType::ptr_is_aligned(data.data()) || !AlignerType::is_aligned(data.size()))
  {
    // TODO(OI-3032): Replace with better observability/contract mechanism when one is available
    jewels::log_cerr_error("Contract violation on alignment for zero copy");
    std::terminate();
  }
  std::optional<std::span<std::byte>> maybe_partial_buffer;
  if (maybe_current_buffer_ && current_buffer_offset_ != 0U)
  {
    maybe_partial_buffer = std::span{
      &jewels::at(*maybe_current_buffer_, static_cast<ssize_t>(current_buffer_offset_)),
      maybe_current_buffer_->size() - current_buffer_offset_};
  }
  add_current_buffer_to_io_vector();
  if (is_full())
  {
    return 0;
  }
  maybe_oldest_data_timestamp_ = std::min(maybe_oldest_data_timestamp_.value_or(timestamp), timestamp);
  const auto bytes_to_zero_copy = std::min(data.size(), max_write_size - write_size_);
  message_handles_.emplace_back(std::move(message_handle));
  // iovec is a system-defined C struct lacking const qualifiers (typical for C), but in fact this data
  // is used as const by the system call, so the const_cast is safe.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) Required by C syscall interface
  io_vectors_.emplace_back(const_cast<std::byte*>(data.data()), bytes_to_zero_copy);
  write_size_ += bytes_to_zero_copy;
  maybe_current_buffer_ = maybe_partial_buffer;
  current_buffer_offset_ = 0U;
  return bytes_to_zero_copy;
}

template <typename Policy>
[[nodiscard]] bool AsyncWriteRequest<Policy>::is_full() const
{
  return (write_size_ >= max_write_size) || (message_handles_.size() >= max_message_handles) ||
         (!maybe_current_buffer_ && (buffer_references_.size() >= max_buffers)) ||
         (io_vectors_.size() >= max_io_vectors);
}

template <typename Policy>
[[nodiscard]] size_t AsyncWriteRequest<Policy>::get_write_size() const
{
  return write_size_;
}

template <typename Policy>
[[nodiscard]] std::optional<jewels::time::SteadyTime> AsyncWriteRequest<Policy>::try_get_oldest_data_timestamp() const
{
  return maybe_oldest_data_timestamp_;
}

template <typename Policy>
[[nodiscard]] const AsyncWriteRequest<Policy>::MessageHandlesType& AsyncWriteRequest<Policy>::message_handles() const
{
  return message_handles_;
}

template <typename Policy>
void AsyncWriteRequest<Policy>::add_current_buffer_to_io_vector()
{
  if (!maybe_current_buffer_)
  {
    return;
  }
  if (current_buffer_offset_ != 0U)
  {
    io_vectors_.emplace_back(maybe_current_buffer_->data(), current_buffer_offset_);
  }
  maybe_current_buffer_ = std::nullopt;
  current_buffer_offset_ = 0U;
}

} // namespace clockwork_logging::onboard
