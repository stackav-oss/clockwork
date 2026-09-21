// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// IWYU pragma: no_include <features.h> - confuses gazelle and isn't really important here
#include "jewels/filesystem/error_code.hh"

#include <array>
#include <cstring>

namespace jewels::filesystem
{

ErrorCode::ErrorCode(int32_t error_number) noexcept
  : error_number_(error_number)
{
}

ErrorCode::ErrorCode(std::errc error_code) noexcept
  : error_number_(static_cast<int>(error_code))
{
}

[[nodiscard]] int32_t ErrorCode::value() const noexcept
{
  return error_number_;
}

[[nodiscard]] const char* ErrorCode::message(std::span<char> buffer) const noexcept
{
  // NOLINTNEXTLINE(readability-qualified-auto) since this can be int or char* unqualified auto is simpler
  auto result = ::strerror_r(error_number_, buffer.data(), buffer.size());
#if (_POSIX_C_SOURCE >= 200112L) && !_GNU_SOURCE
  return (result == 0 ? buffer.data() : "Error formatting error string");
#else
  return result;
#endif
}

[[nodiscard]] const char* ErrorCode::message() const
{
  static constexpr size_t message_buffer_size = 256U;
  thread_local std::array<char, message_buffer_size> message_buffer{};
  return message(message_buffer);
}

[[nodiscard]] ErrorCode::operator bool() const noexcept
{
  return error_number_ != 0;
}

[[nodiscard]] ErrorCode make_error_code(int32_t error_number) noexcept
{
  return ErrorCode{error_number};
}

} // namespace jewels::filesystem
