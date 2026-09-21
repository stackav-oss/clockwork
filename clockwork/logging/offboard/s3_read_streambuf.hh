// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <ios>
#include <span>

namespace clockwork_logging::offboard
{

/// Streambuf implementation for reading data from S3 into a data vector
class S3ReadStreambuf : public std::streambuf
{
public:
  using char_type = std::streambuf::char_type;
  using traits_type = std::streambuf::traits_type;
  using int_type = std::streambuf::int_type;
  using pos_type = std::streambuf::pos_type;
  using off_type = std::streambuf::off_type;

  /// Constructor
  /// @param[in] buffers Vector of data buffers to read to S3
  explicit S3ReadStreambuf(std::span<std::byte> buffer);

  ~S3ReadStreambuf() override = default;

  S3ReadStreambuf(const S3ReadStreambuf& other) = delete;
  S3ReadStreambuf& operator=(const S3ReadStreambuf& other) = delete;
  S3ReadStreambuf(S3ReadStreambuf&&) noexcept = default;
  S3ReadStreambuf& operator=(S3ReadStreambuf&&) noexcept = default;

  /// Get the size of the data buffer
  /// @return The size of the data buffer
  [[nodiscard]] size_t get_buffer_size() const noexcept;

protected:
  /// @see std::streambuf::xsputn()
  std::streamsize xsputn(const char_type* buf, std::streamsize count) override;

  /// @see std::streambuf::seekoff()
  pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode mode) override;

  /// @see std::streambuf::seekpos()
  pos_type seekpos(pos_type pos, std::ios_base::openmode mode) override;

private:
  /// Data span to read from S3
  std::span<std::byte> buffer_;
};

} // namespace clockwork_logging::offboard
