// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <ios>
#include <memory_resource>
#include <vector>

namespace clockwork_logging::offboard
{

/// Streambuf implementation for writing data to S3 from a vector of data vectors
class S3WriteStreambuf : public std::streambuf
{
public:
  using char_type = std::streambuf::char_type;
  using traits_type = std::streambuf::traits_type;
  using int_type = std::streambuf::int_type;
  using pos_type = std::streambuf::pos_type;
  using off_type = std::streambuf::off_type;

  /// Constructor
  /// @param[in] buffers Vector of data buffers to write to S3
  explicit S3WriteStreambuf(std::pmr::vector<std::pmr::vector<std::byte>> buffers);

  ~S3WriteStreambuf() override = default;

  S3WriteStreambuf(const S3WriteStreambuf& other) = delete;
  S3WriteStreambuf& operator=(const S3WriteStreambuf& other) = delete;
  S3WriteStreambuf(S3WriteStreambuf&&) noexcept = default;
  S3WriteStreambuf& operator=(S3WriteStreambuf&&) noexcept = default;

  /// Get the total size of the data buffers
  /// @return The total size of the data buffers
  [[nodiscard]] size_t total_size() const noexcept;

protected:
  /// @see std::streambuf::xsgetn()
  std::streamsize xsgetn(char_type* buf, std::streamsize count) override;

  /// @see std::streambuf::underflow()
  int_type underflow() override;

  /// @see std::streambuf::seekoff()
  pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode mode) override;

  /// @see std::streambuf::seekpos()
  pos_type seekpos(pos_type pos, std::ios_base::openmode mode) override;

private:
  /// Vector of data buffers to write to S3
  std::pmr::vector<std::pmr::vector<std::byte>> buffers_;

  /// Current buffer index
  size_t current_buffer_index_{0U};

  /// Offset to the current buffer in bytes
  size_t current_buffer_offset_{0U};

  /// Total buffer size in bytes
  size_t total_size_{0U};
};

} // namespace clockwork_logging::offboard
