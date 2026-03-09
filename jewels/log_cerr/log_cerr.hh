// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/log_cerr/log_level.hh"
#include "jewels/log_cerr/log_time.hh"
#include "jewels/time/sync_time.hh"

#include <fmt/base.h>
#include <fmt/core.h> // IWYU pragma: export

#include <array>
#include <chrono>
#include <cstddef>
#include <experimental/source_location>
#include <string_view>

namespace jewels
{

namespace impl
{

/// Log message buffer size in bytes
static constexpr size_t log_message_buffer_size = 1024U;

bool should_print_in_color();

LogLevel get_log_threshold();

EpochTime get_log_time();

/// Configure the log message to use an external clock object for timestamps.
void set_log_time_clock(const ::jewels::LogClockPtr& log_clock);

/// Convert a log level to a string view for message formatting (converts to upper case)
constexpr std::string_view log_level_name(LogLevel log_level) noexcept;

/// Format a log message into a buffer
/// @param[in] buffer Message buffer
/// @param[in] log_level Log level
/// @param[in] location Source file location
/// @param[in] msg_fmt Message format
/// @param[in] args Message arguments
/// @tparam TimePolicy Time policy
/// @return string view for the formatted message (string is null terminated)
[[nodiscard]] std::string_view log_message_to_buffer(
  std::array<char, log_message_buffer_size>& buffer,
  LogLevel log_level,
  const std::experimental::source_location& location,
  fmt::string_view msg_fmt,
  fmt::format_args args);

/// Print a log message to standard error
/// @param[in] log_level Log level
/// @param[in] location Source file location
/// @param[in] msg_fmt Message format
/// @param[in] args Message arguments
/// @tparam Args Message argument types
inline void log_message_to_cerr(
  LogLevel log_level,
  const std::experimental::source_location& location,
  fmt::string_view msg_fmt,
  fmt::format_args args);

} // namespace impl

/// Throttle for limiting debug logging rates
///
/// This class is not thread safe
class LogCerrThrottle
{
public:
  /// Constructor
  /// @param[in] min_interval Minimum interval between log messages
  explicit LogCerrThrottle(std::chrono::nanoseconds min_interval);

  ~LogCerrThrottle() noexcept = default;

  LogCerrThrottle(const LogCerrThrottle&) = delete;
  LogCerrThrottle& operator=(const LogCerrThrottle&) = delete;
  LogCerrThrottle(LogCerrThrottle&&) noexcept = default;
  LogCerrThrottle& operator=(LogCerrThrottle&&) noexcept = default;

  /// Check whether a log message should be printed.
  ///
  /// Sets the last message time to the current time if the return value is true
  ///
  /// @return True iff a message should be printed
  [[nodiscard]] bool should_log();

private:
  /// Minimum interval between log messages
  std::chrono::nanoseconds min_interval_;

  /// Last time a log message was printed
  time::SteadyTime last_log_time_;
};

/// Structure for argument deduction
/// @tparam Args Message argument types
template <typename... Args>
// This is named like a function, even though it's a struct, because it's used like a function.
// NOLINTNEXTLINE(readability-identifier-naming)
struct log_cerr_fatal
{
  /// Print a fatal message to standard error
  /// @param[in] msg_fmt Message format
  /// @param[in] args Message arguments
  /// @param[in] location Source file location
  explicit log_cerr_fatal(
    fmt::format_string<Args...> msg_fmt,
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) - this is forwarded in the implementation.
    Args&&... args,
    std::experimental::source_location location = std::experimental::source_location::current());
};

template <typename... Args>
log_cerr_fatal(fmt::format_string<Args...>, Args&&...) -> log_cerr_fatal<Args...>;

/// Structure for argument deduction
/// @tparam Args Message argument types
template <typename... Args>
// This is named like a function, even though it's a struct, because it's used like a function.
// NOLINTNEXTLINE(readability-identifier-naming)
struct log_cerr_error
{
  /// Print an error message to standard error
  /// @param[in] msg_fmt Message format
  /// @param[in] args Message arguments
  /// @param[in] location Source file location
  explicit log_cerr_error(
    fmt::format_string<Args...> msg_fmt,
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) - this is forwarded in the implementation.
    Args&&... args,
    std::experimental::source_location location = std::experimental::source_location::current());
};

template <typename... Args>
log_cerr_error(fmt::format_string<Args...>, Args&&...) -> log_cerr_error<Args...>;

/// Structure for argument deduction
/// @tparam Args Message argument types
template <typename... Args>
// This is named like a function, even though it's a struct, because it's used like a function.
// NOLINTNEXTLINE(readability-identifier-naming)
struct log_cerr_error_throttled
{
  /// Print an error message to standard error with throttling
  /// @param[in,out] throttle Logging rate throttle
  /// @param[in] msg_fmt Message format
  /// @param[in] args Message arguments
  /// @param[in] location Source file location
  explicit log_cerr_error_throttled(
    LogCerrThrottle& throttle,
    fmt::format_string<Args...> msg_fmt,
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) - this is forwarded in the implementation.
    Args&&... args,
    std::experimental::source_location location = std::experimental::source_location::current());
};

template <typename... Args>
log_cerr_error_throttled(LogCerrThrottle&, fmt::format_string<Args...>, Args&&...) -> log_cerr_error_throttled<Args...>;

/// Structure for argument deduction
/// @tparam Args Message argument types
template <typename... Args>
// This is named like a function, even though it's a struct, because it's used like a function.
// NOLINTNEXTLINE(readability-identifier-naming)
struct log_cerr_info
{
  /// Print an informational message to standard error
  /// @param[in] msg_fmt Message format
  /// @param[in] args Message arguments
  /// @param[in] location Source file location
  explicit log_cerr_info(
    fmt::format_string<Args...> msg_fmt,
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) - this is forwarded in the implementation.
    Args&&... args,
    std::experimental::source_location location = std::experimental::source_location::current());
};

template <typename... Args>
log_cerr_info(fmt::format_string<Args...>, Args&&...) -> log_cerr_info<Args...>;

/// Structure for argument deduction
/// @tparam Args Message argument types
template <typename... Args>
// This is named like a function, even though it's a struct, because it's used like a function.
// NOLINTNEXTLINE(readability-identifier-naming)
struct log_cerr_info_throttled
{
  /// Print an informational message to standard error with throttling
  /// @param[in,out] throttle Logging rate throttle
  /// @param[in] msg_fmt Message format
  /// @param[in] args Message arguments
  /// @param[in] location Source file location
  explicit log_cerr_info_throttled(
    LogCerrThrottle& throttle,
    fmt::format_string<Args...> msg_fmt,
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) - this is forwarded in the implementation.
    Args&&... args,
    std::experimental::source_location location = std::experimental::source_location::current());
};

template <typename... Args>
log_cerr_info_throttled(LogCerrThrottle&, fmt::format_string<Args...>, Args&&...) -> log_cerr_info_throttled<Args...>;

/// Structure for argument deduction
/// @tparam Args Message argument types
template <typename... Args>
// This is named like a function, even though it's a struct, because it's used like a function.
// NOLINTNEXTLINE(readability-identifier-naming)
struct log_cerr_warn
{
  /// Print a warning message to standard error
  /// @param[in] msg_fmt Message format
  /// @param[in] args Message arguments
  /// @param[in] location Source file location
  explicit log_cerr_warn(
    fmt::format_string<Args...> msg_fmt,
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) - this is forwarded in the implementation.
    Args&&... args,
    std::experimental::source_location location = std::experimental::source_location::current());
};

template <typename... Args>
log_cerr_warn(fmt::format_string<Args...>, Args&&...) -> log_cerr_warn<Args...>;

/// Structure for argument deduction
/// @tparam Args Message argument types
template <typename... Args>
// This is named like a function, even though it's a struct, because it's used like a function.
// NOLINTNEXTLINE(readability-identifier-naming)
struct log_cerr_warn_throttled
{
  /// Print a warning message to standard error with throttling
  /// @param[in,out] throttle Logging rate throttle
  /// @param[in] msg_fmt Message format
  /// @param[in] args Message arguments
  /// @param[in] location Source file location
  explicit log_cerr_warn_throttled(
    LogCerrThrottle& throttle,
    fmt::format_string<Args...> msg_fmt,
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) - this is forwarded in the implementation.
    Args&&... args,
    std::experimental::source_location location = std::experimental::source_location::current());
};

template <typename... Args>
log_cerr_warn_throttled(LogCerrThrottle&, fmt::format_string<Args...>, Args&&...) -> log_cerr_warn_throttled<Args...>;

/// Structure for argument deduction
/// @tparam Args Message argument types
template <typename... Args>
// This is named like a function, even though it's a struct, because it's used like a function.
// NOLINTNEXTLINE(readability-identifier-naming)
struct log_cerr_debug
{
  /// Print a debug message to standard error
  /// @param[in] msg_fmt Message format
  /// @param[in] args Message arguments
  /// @param[in] location Source file location
  explicit log_cerr_debug(
    fmt::format_string<Args...> msg_fmt,
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) - this is forwarded in the implementation.
    Args&&... args,
    std::experimental::source_location location = std::experimental::source_location::current());
};

template <typename... Args>
log_cerr_debug(fmt::format_string<Args...>, Args&&...) -> log_cerr_debug<Args...>;

/// Structure for argument deduction
/// @tparam Args Message argument types
template <typename... Args>
// This is named like a function, even though it's a struct, because it's used like a function.
// NOLINTNEXTLINE(readability-identifier-naming)
struct log_cerr_debug_throttled
{
  /// Print a debug message to standard error with throttling
  /// @param[in,out] throttle Logging rate throttle
  /// @param[in] msg_fmt Message format
  /// @param[in] args Message arguments
  /// @param[in] location Source file location
  explicit log_cerr_debug_throttled(
    LogCerrThrottle& throttle,
    fmt::format_string<Args...> msg_fmt,
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) - this is forwarded in the implementation.
    Args&&... args,
    std::experimental::source_location location = std::experimental::source_location::current());
};

template <typename... Args>
log_cerr_debug_throttled(LogCerrThrottle&, fmt::format_string<Args...>, Args&&...) -> log_cerr_debug_throttled<Args...>;

} // namespace jewels

#include "jewels/log_cerr/log_cerr.inl"
