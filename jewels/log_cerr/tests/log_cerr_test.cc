// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/log_cerr/log_cerr.hh"

#include <catch2/catch_test_macros.hpp>
#include <fmt10/format.h>

#include <array>
#include <experimental/source_location>
#include <string>
#include <string_view>
#include <thread> // IWYU pragma: keep

namespace jewels
{

struct MockTimePolicy
{
  static impl::EpochTime now()
  {
    return {.seconds = 12345, .nanoseconds = 67890};
  }
};

TEST_CASE("log_message_to_buffer")
{
  std::array<char, impl::log_message_buffer_size> buffer{};
  const auto location = std::experimental::source_location::current();
  const std::string_view timestamp = "12345.000067890";

  std::string expected_str =
    fmt::format("FATAL {}:{} [{}] Really long fatal message ", location.file_name(), location.line(), timestamp);
  const auto header_len = expected_str.size();
  expected_str += std::string(impl::log_message_buffer_size - header_len - 4, 'x');
  expected_str += "...";

  REQUIRE(
    impl::log_message_to_buffer<MockTimePolicy>(
      buffer,
      LogLevel::fatal,
      location,
      "Really long fatal message "
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxx",
      fmt::make_format_args()) == expected_str);
  expected_str = fmt::format("ERROR {}:{} [{}] Error message 1", location.file_name(), location.line(), timestamp);
  REQUIRE(
    impl::log_message_to_buffer<MockTimePolicy>(
      buffer, LogLevel::error, location, "Error message 1", fmt::make_format_args()) == expected_str);
  expected_str = fmt::format("INFO {}:{} [{}] Info message 2", location.file_name(), location.line(), timestamp);
  auto two = 2;
  REQUIRE(
    impl::log_message_to_buffer<MockTimePolicy>(
      buffer, LogLevel::info, location, "Info message {}", fmt::make_format_args(two)) == expected_str);
  expected_str = fmt::format("WARN {}:{} [{}] Warning message 3.1", location.file_name(), location.line(), timestamp);
  auto three = 3;
  auto one = 1;
  REQUIRE(
    impl::log_message_to_buffer<MockTimePolicy>(
      buffer, LogLevel::warn, location, "Warning message {}.{}", fmt::make_format_args(three, one)) == expected_str);
  expected_str = fmt::format("DEBUG {}:{} [{}] Debug message 3.1.0", location.file_name(), location.line(), timestamp);
  const auto* string_zero = "0";
  REQUIRE(
    impl::log_message_to_buffer<MockTimePolicy>(
      buffer, LogLevel::debug, location, "Debug message {}.{}.{}", fmt::make_format_args(three, one, string_zero)) ==
    expected_str);
}

TEST_CASE("Smoke test")
{
  LogCerrThrottle throttle({});
  log_cerr_fatal(
    "Really long fatal message "
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
  log_cerr_error("{}", "Error message 1");
  log_cerr_error_throttled(throttle, "{}", "Error message 1");
  log_cerr_info("Info message {}", 2);
  log_cerr_info_throttled(throttle, "Info message {}", 2);
  log_cerr_warn("Warning message {}.{}", 3, 1);
  log_cerr_warn_throttled(throttle, "Warning message {}.{}", 3, 1);
  log_cerr_debug("Debug message {}.{}.{}", 3, 1, "0");
  log_cerr_debug_throttled(throttle, "Debug message {}.{}.{}", 3, 1, "0");
}

TEST_CASE("Throttle")
{
  LogCerrThrottle throttle(std::chrono::seconds(1));
  REQUIRE(throttle.should_log());
  REQUIRE_FALSE(throttle.should_log());
  std::this_thread::sleep_for(std::chrono::seconds(1));
  REQUIRE(throttle.should_log());
}

} // namespace jewels
