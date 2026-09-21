// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/s3_read_streambuf.hh"

#include "clockwork/logging/nolint_helper.hh"

// IWYU pragma: no_include <bits/types/__mbstate_t.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>

namespace clockwork_logging::offboard
{

S3ReadStreambuf::S3ReadStreambuf(std::span<std::byte> buffer)
  : buffer_(buffer)
{
  if (buffer_.empty())
  {
    setp(nullptr, nullptr);
  }
  else
  {
    setp(
      nolint_helper::char_ptr_to_span_element(buffer_),
      nolint_helper::char_ptr_to_span_element(buffer_, buffer_.size()));
  }
}

[[nodiscard]] size_t S3ReadStreambuf::get_buffer_size() const noexcept
{
  return buffer_.size();
}

std::streamsize S3ReadStreambuf::xsputn(const char_type* buf, std::streamsize count)
{
  auto remainder = static_cast<std::streamsize>(this->epptr() - this->pptr());
  if (remainder == 0)
  {
    return 0;
  }
  auto bytes_to_read = std::min(count, remainder);
  std::memcpy(this->pptr(), buf, static_cast<size_t>(bytes_to_read));
  pbump(static_cast<int32_t>(bytes_to_read));
  return bytes_to_read;
}

S3ReadStreambuf::pos_type
S3ReadStreambuf::seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode mode)
{
  if (off != 0)
  {
    return pos_type{-1};
  }
  if (dir == std::ios_base::end)
  {
    return this->seekpos(static_cast<int64_t>(buffer_.size()), mode);
  }
  if (dir == std::ios_base::beg)
  {
    return this->seekpos(0, mode);
  }
  return this->pptr() - this->pbase();
}

S3ReadStreambuf::pos_type S3ReadStreambuf::seekpos(pos_type pos, std::ios_base::openmode /*mode*/)
{
  if (pos != 0 && std::cmp_not_equal(static_cast<int64_t>(pos), buffer_.size()))
  {
    return pos_type{-1};
  }
  setp(
    nolint_helper::char_ptr_to_span_element(buffer_), nolint_helper::char_ptr_to_span_element(buffer_, buffer_.size()));
  pbump(static_cast<int32_t>(pos));
  return pos;
}

} // namespace clockwork_logging::offboard
