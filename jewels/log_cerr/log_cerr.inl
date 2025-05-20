// IWYU pragma: private, include "jewels/log_cerr/log_cerr.hh"
#pragma once

#include "jewels/log_cerr/log_cerr.hh"

#include "jewels/log_cerr/log_level.hh"
#include "jewels/time/sync_time.hh"

#include <fmt10/base.h>
#include <fmt10/format.h> // IWYU pragma: keep (needed for format_as to work)

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <experimental/source_location>
#include <iostream>
#include <string_view>

namespace jewels
{

namespace impl
{

/// Unspecified log level name
static constexpr std::string_view unspecified_level_name = "UNSPECIFIED";

/// Debug log level name
static constexpr std::string_view debug_level_name = "DEBUG";

/// Info log level name
static constexpr std::string_view info_level_name = "INFO";

/// Warn log level name
static constexpr std::string_view warn_level_name = "WARN";

/// Error log level name
static constexpr std::string_view error_level_name = "ERROR";

/// Fatal log level name
static constexpr std::string_view fatal_level_name = "FATAL";

/// String appended when a message has been truncated
static constexpr std::array truncated_message_suffix = {'.', '.', '.'};
static_assert(log_message_buffer_size > truncated_message_suffix.size());

/// Convert a log level to a string view for message formatting (converts to upper case)
constexpr std::string_view log_level_name(const LogLevel log_level) noexcept
{
  switch (log_level)
  {
  case LogLevel::debug:
    return debug_level_name;
  case LogLevel::info:
    return info_level_name;
  case LogLevel::warn:
    return warn_level_name;
  case LogLevel::error:
    return error_level_name;
  case LogLevel::fatal:
    return fatal_level_name;
  }
  return unspecified_level_name;
}

// Color codes
static constexpr std::string_view bright_cyan_color_code = "\033[1;36m";
static constexpr std::string_view bright_white_color_code = "\033[1m";
static constexpr std::string_view bright_yellow_color_code = "\033[1;33m";
static constexpr std::string_view bright_red_color_code = "\033[1;31m";
static constexpr std::string_view bright_white_on_red_color_code = "\033[1;37;41m";
static constexpr std::string_view restore_default_terminal_color_code = "\033[0m"; // reset to default

// Map log levels to color codes
static constexpr std::string_view unspecified_level_color_code{};
static constexpr std::string_view debug_level_color_code = bright_cyan_color_code;
static constexpr std::string_view info_level_color_code = bright_white_color_code;
static constexpr std::string_view warn_level_color_code = bright_yellow_color_code;
static constexpr std::string_view error_level_color_code = bright_red_color_code;
static constexpr std::string_view fatal_level_color_code = bright_white_on_red_color_code;

/// Lookup the color code for a given log level
constexpr std::string_view log_level_color_code(const LogLevel log_level) noexcept
{
  switch (log_level)
  {
  case LogLevel::debug:
    return debug_level_color_code;
  case LogLevel::info:
    return info_level_color_code;
  case LogLevel::warn:
    return warn_level_color_code;
  case LogLevel::error:
    return error_level_color_code;
  case LogLevel::fatal:
    return fatal_level_color_code;
  }
  return unspecified_level_color_code;
}

struct EpochTime
{
  int64_t seconds;
  int64_t nanoseconds;
};

struct SyncTimePolicy
{
  static EpochTime now()
  {
    const int64_t now_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(jewels::time::SyncClock::now().time_since_epoch()).count();
    const int64_t seconds = now_ns / 1'000'000'000;
    const int64_t nanoseconds = now_ns % 1'000'000'000;
    return {.seconds = seconds, .nanoseconds = nanoseconds};
  }
};

template <typename TimePolicy>
[[nodiscard]] std::string_view log_message_to_buffer(
  std::array<char, log_message_buffer_size>& buffer,
  LogLevel log_level,
  const std::experimental::source_location& location,
  fmt::string_view msg_fmt,
  fmt::format_args args)
{
  static_assert(log_message_buffer_size > truncated_message_suffix.size());

  const auto [seconds, nanoseconds] = TimePolicy::now();

  auto untruncated_size = fmt::format_to_n(
                            buffer.data(),
                            buffer.size(),
                            "{} {}:{} [{}.{:09}] ",
                            log_level_name(log_level),
                            location.file_name(),
                            location.line(),
                            seconds,
                            nanoseconds)
                            .size;

  if (untruncated_size < buffer.size())
  {
    untruncated_size +=
      fmt::vformat_to_n(&buffer.at(untruncated_size), buffer.size() - untruncated_size, msg_fmt, args).size;
  }
  if (untruncated_size < buffer.size())
  {
    buffer.at(untruncated_size) = '\0';
  }
  else
  {
    untruncated_size = buffer.size() - 1U;
    std::memcpy(
      &buffer.at(buffer.size() - truncated_message_suffix.size() - 1U),
      truncated_message_suffix.data(),
      truncated_message_suffix.size());
    buffer.back() = '\0';
  }
  return {buffer.data(), untruncated_size};
}

void log_message_to_cerr(
  LogLevel log_level,
  const std::experimental::source_location& location,
  fmt::string_view msg_fmt,
  fmt::format_args args)
{
  // ensure that the log threshold is met
  if (log_level < get_log_threshold())
  {
    return;
  }

  std::array<char, log_message_buffer_size> buffer{};
  const auto message = log_message_to_buffer<SyncTimePolicy>(buffer, log_level, location, msg_fmt, args);

  if (should_print_in_color())
  {
    std::cerr << log_level_color_code(log_level) << message << restore_default_terminal_color_code << "\n"
              << std::flush;
  }
  else
  {
    std::cerr << message << "\n" << std::flush;
  }
}

} // namespace impl

template <typename... Args>
log_cerr_fatal<Args...>::log_cerr_fatal(
  fmt::format_string<Args...> msg_fmt, Args&&... args, const std::experimental::source_location location)
{
  impl::log_message_to_cerr(LogLevel::fatal, location, msg_fmt, fmt::make_format_args(args...));
}

template <typename... Args>
log_cerr_error<Args...>::log_cerr_error(
  fmt::format_string<Args...> msg_fmt, Args&&... args, const std::experimental::source_location location)
{
  impl::log_message_to_cerr(LogLevel::error, location, msg_fmt, fmt::make_format_args(args...));
}

template <typename... Args>
log_cerr_error_throttled<Args...>::log_cerr_error_throttled(
  LogCerrThrottle& throttle,
  fmt::format_string<Args...> msg_fmt,
  Args&&... args,
  const std::experimental::source_location location)
{
  if (throttle.should_log())
  {
    impl::log_message_to_cerr(LogLevel::error, location, msg_fmt, fmt::make_format_args(args...));
  }
}

template <typename... Args>
log_cerr_info<Args...>::log_cerr_info(
  fmt::format_string<Args...> msg_fmt, Args&&... args, const std::experimental::source_location location)
{
  impl::log_message_to_cerr(LogLevel::info, location, msg_fmt, fmt::make_format_args(args...));
}

template <typename... Args>
log_cerr_info_throttled<Args...>::log_cerr_info_throttled(
  LogCerrThrottle& throttle,
  fmt::format_string<Args...> msg_fmt,
  Args&&... args,
  const std::experimental::source_location location)
{
  if (throttle.should_log())
  {
    impl::log_message_to_cerr(LogLevel::info, location, msg_fmt, fmt::make_format_args(args...));
  }
}

template <typename... Args>
log_cerr_warn<Args...>::log_cerr_warn(
  fmt::format_string<Args...> msg_fmt, Args&&... args, const std::experimental::source_location location)
{
  impl::log_message_to_cerr(LogLevel::warn, location, msg_fmt, fmt::make_format_args(args...));
}

template <typename... Args>
log_cerr_warn_throttled<Args...>::log_cerr_warn_throttled(
  LogCerrThrottle& throttle,
  fmt::format_string<Args...> msg_fmt,
  Args&&... args,
  const std::experimental::source_location location)
{
  if (throttle.should_log())
  {
    impl::log_message_to_cerr(LogLevel::warn, location, msg_fmt, fmt::make_format_args(args...));
  }
}

template <typename... Args>
log_cerr_debug<Args...>::log_cerr_debug(
  fmt::format_string<Args...> msg_fmt, Args&&... args, const std::experimental::source_location location)
{
  impl::log_message_to_cerr(LogLevel::debug, location, msg_fmt, fmt::make_format_args(args...));
}

template <typename... Args>
log_cerr_debug_throttled<Args...>::log_cerr_debug_throttled(
  LogCerrThrottle& throttle,
  fmt::format_string<Args...> msg_fmt,
  Args&&... args,
  const std::experimental::source_location location)
{
  if (throttle.should_log())
  {
    impl::log_message_to_cerr(LogLevel::debug, location, msg_fmt, fmt::make_format_args(args...));
  }
}

} // namespace jewels
