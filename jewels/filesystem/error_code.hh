// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/base.h>
#include <fmt/format.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>

namespace jewels::filesystem
{

/// Error codes for filesystem errors
class ErrorCode
{
public:
  /// Construct an error code from an errno value
  explicit ErrorCode(int32_t error_number) noexcept;

  /// Construct an error code from an C++ portable error code
  explicit ErrorCode(std::errc error_code) noexcept;

  constexpr ErrorCode() noexcept = default;
  ~ErrorCode() noexcept = default;

  ErrorCode(const ErrorCode&) noexcept = default;
  ErrorCode& operator=(const ErrorCode&) noexcept = default;
  ErrorCode(ErrorCode&& other) noexcept = default;
  ErrorCode& operator=(ErrorCode&& other) noexcept = default;

  /// Accessor for the the error number
  /// @return Error number
  [[nodiscard]] int32_t value() const noexcept;

  /// Get the error message for the error number, using the provided buffer as temporary storage if necessary
  /// @return Error message, which may or may not be the same as buffer.data()
  [[nodiscard]] const char* message(std::span<char> buffer) const noexcept;

  /// Get the error message for the error number
  /// @return Error message
  [[nodiscard]] const char* message() const;

  /// Bool operator
  /// @return True if error_number_ is non zero, otherwise false
  [[nodiscard]] explicit operator bool() const noexcept;

  /// Comparison operator
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return lhs == rhs
  [[nodiscard]] friend bool operator==(const ErrorCode& lhs, const ErrorCode& rhs) noexcept
  {
    return lhs.error_number_ == rhs.error_number_;
  }
  [[nodiscard]] friend bool operator==(const ErrorCode& lhs, const std::errc& rhs) noexcept
  {
    return lhs.error_number_ == static_cast<int>(rhs);
  }
  [[nodiscard]] friend bool operator==(const std::errc& lhs, const ErrorCode& rhs) noexcept
  {
    return static_cast<int>(lhs) == rhs.error_number_;
  }

  /// Comparison operator
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return lhs != rhs
  [[nodiscard]] friend bool operator!=(const ErrorCode& lhs, const ErrorCode& rhs) noexcept
  {
    return !(lhs == rhs);
  }
  [[nodiscard]] friend bool operator!=(const ErrorCode& lhs, const std::errc& rhs) noexcept
  {
    return !(lhs == rhs);
  }
  [[nodiscard]] friend bool operator!=(const std::errc& lhs, const ErrorCode& rhs) noexcept
  {
    return !(lhs == rhs);
  }

  /// Comparison operator
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return lhs < rhs
  [[nodiscard]] friend bool operator<(const ErrorCode& lhs, const ErrorCode& rhs) noexcept
  {
    return lhs.error_number_ < rhs.error_number_;
  }

private:
  /// File descriptor, valid if greater than or equal to zero
  int32_t error_number_{0};
};

/// Make an error code from an error number
/// @param[in] error_number Error number
/// @return Error code
[[nodiscard]] ErrorCode make_error_code(int32_t error_number) noexcept;

} // namespace jewels::filesystem

template <>
struct fmt::formatter<jewels::filesystem::ErrorCode> : fmt::formatter<string_view>
{
  auto format(const jewels::filesystem::ErrorCode& value, fmt::format_context& ctx) const
  {
    static constexpr size_t temp_buffer_size = 256;
    std::array<char, temp_buffer_size> buffer{};
    return fmt::formatter<string_view>::format(value.message(buffer), ctx);
  }
};
