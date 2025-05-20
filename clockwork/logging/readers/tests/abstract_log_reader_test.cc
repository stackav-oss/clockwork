// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging
{
namespace
{

TEST_CASE("Abstract log reader")
{
  class Reader : public AbstractLogReader
  {
  public:
    Reader(
      std::string_view log_uri,
      std::optional<LogInterval> maybe_log_interval,
      std::optional<RelativeInterval> maybe_relative_interval)
      : AbstractLogReader(log_uri, maybe_log_interval, maybe_relative_interval)
    {
    }
    [[nodiscard]] std::string type() const override
    {
      return "reader";
    }
    [[nodiscard]] LogExpected<void> open(const std::function<bool(std::string_view)>& /*topic_filter*/) override
    {
      return {};
    }
    [[nodiscard]] LogExpected<void> close() override
    {
      return {};
    }
    [[nodiscard]] std::vector<std::string> get_channels() override
    {
      return {};
    }
    [[nodiscard]] std::vector<TopicMetadata> get_metadata() override
    {
      return {};
    }
    [[nodiscard]] LogExpected<TopicMetadata> get_channel_metadata(std::string_view /*channel_name*/) override
    {
      return jewels::unexpected{LogError::unknown_channel};
    }
    [[nodiscard]] LogExpected<LogTimestamp> start_time() override
    {
      return {LogTimestamp{}};
    }
    [[nodiscard]] LogExpected<LogTimestamp> end_time() override
    {
      return {LogTimestamp{}};
    }
    [[nodiscard]] std::optional<LoggedMessage> next_message() override
    {
      return {};
    }
  };

  const std::string_view log_uri{"test"};
  const LogInterval log_interval{LogTimestamp(10)};
  const RelativeInterval relative_interval{
    .start_offset = std::chrono::nanoseconds(10), .end_offset = std::chrono::nanoseconds(11)};
  auto reader = Reader(log_uri, log_interval, relative_interval);

  REQUIRE(reader.log_uri() == log_uri);
  REQUIRE(reader.maybe_log_interval() == log_interval);
  REQUIRE(reader.maybe_relative_interval() == relative_interval);
}
} // namespace
} // namespace clockwork_logging
