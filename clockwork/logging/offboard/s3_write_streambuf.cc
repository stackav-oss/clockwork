// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/s3_write_streambuf.hh"

#include <span>

// IWYU pragma: no_include <bits/types/__mbstate_t.h>

#include "clockwork/logging/nolint_helper.hh"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <utility>

namespace clockwork_logging::offboard
{

S3WriteStreambuf::S3WriteStreambuf(std::pmr::vector<std::pmr::vector<std::byte>> buffers)
  : buffers_(std::move(buffers))
{
  for (const auto& buffer : buffers_)
  {
    total_size_ += buffer.size();
  }
  if (total_size_ == 0U)
  {
    setg(nullptr, nullptr, nullptr);
  }
  else
  {
    setg(
      nolint_helper::char_ptr_to_span_element(buffers_.front()),
      nolint_helper::char_ptr_to_span_element(buffers_.front()),
      nolint_helper::char_ptr_to_span_element(buffers_.front(), buffers_.front().size()));
  }
}

[[nodiscard]] size_t S3WriteStreambuf::total_size() const noexcept
{
  return total_size_;
}

std::streamsize S3WriteStreambuf::xsgetn(char_type* buf, std::streamsize count)
{
  auto bytes_read = std::streamsize{0};
  const std::span buf_span{buf, static_cast<size_t>(count)};
  while (bytes_read < count)
  {
    auto remainder = static_cast<std::streamsize>(this->egptr() - this->gptr());
    if (remainder == 0)
    {
      if (this->underflow() == traits_type::eof())
      {
        break;
      }
      continue;
    }
    auto bytes_to_read = std::min(count - bytes_read, remainder);
    std::memcpy(&buf_span[static_cast<size_t>(bytes_read)], _M_in_cur, static_cast<size_t>(bytes_to_read));
    _M_in_cur = nolint_helper::increment_char_ptr(_M_in_cur, static_cast<size_t>(bytes_to_read));
    bytes_read += bytes_to_read;
  }
  return bytes_read;
}

S3WriteStreambuf::int_type S3WriteStreambuf::underflow()
{
  auto remainder = static_cast<std::streamsize>(this->egptr() - this->gptr());
  if (remainder > 0)
  {
    return *(this->gptr());
  }
  while (current_buffer_index_ < buffers_.size())
  {
    current_buffer_offset_ += buffers_.at(current_buffer_index_).size();
    ++current_buffer_index_;
    if (current_buffer_index_ < buffers_.size() && !buffers_.at(current_buffer_index_).empty())
    {
      setg(
        nolint_helper::char_ptr_to_span_element(buffers_.at(current_buffer_index_)),
        nolint_helper::char_ptr_to_span_element(buffers_.at(current_buffer_index_)),
        nolint_helper::char_ptr_to_span_element(
          buffers_.at(current_buffer_index_), buffers_.at(current_buffer_index_).size()));
      return static_cast<int_type>(buffers_.at(current_buffer_index_).front());
    }
  }
  setg(nullptr, nullptr, nullptr);
  return traits_type::eof();
}

S3WriteStreambuf::pos_type
S3WriteStreambuf::seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode mode)
{
  if (off != 0)
  {
    return pos_type{-1};
  }
  if (dir == std::ios_base::end)
  {
    return this->seekpos(static_cast<int64_t>(total_size_), mode);
  }
  if (dir == std::ios_base::beg)
  {
    return this->seekpos(0, mode);
  }
  return static_cast<int64_t>(current_buffer_offset_) + (this->gptr() - this->eback());
}

S3WriteStreambuf::pos_type S3WriteStreambuf::seekpos(pos_type pos, std::ios_base::openmode /*mode*/)
{
  if (pos != 0 && std::cmp_not_equal(static_cast<int64_t>(pos), static_cast<int64_t>(total_size_)))
  {
    return pos_type{-1};
  }
  if (std::cmp_equal(static_cast<int64_t>(pos), static_cast<int64_t>(total_size_)))
  {
    current_buffer_offset_ = total_size_;
    current_buffer_index_ = buffers_.size();
    setg(nullptr, nullptr, nullptr);
    return static_cast<int64_t>(total_size_);
  }
  current_buffer_offset_ = 0U;
  current_buffer_index_ = 0U;
  while (std::cmp_greater_equal(
    static_cast<int64_t>(pos),
    static_cast<int64_t>(current_buffer_offset_ + buffers_.at(current_buffer_index_).size())))
  {
    current_buffer_offset_ += buffers_.at(current_buffer_index_).size();
    ++current_buffer_index_;
  }
  const auto buffer_offset = static_cast<size_t>(pos) - current_buffer_offset_;
  setg(
    nolint_helper::char_ptr_to_span_element(buffers_.at(current_buffer_index_)),
    nolint_helper::char_ptr_to_span_element(buffers_.at(current_buffer_index_), buffer_offset),
    nolint_helper::char_ptr_to_span_element(
      buffers_.at(current_buffer_index_), buffers_.at(current_buffer_index_).size()));
  return pos;
}

} // namespace clockwork_logging::offboard
