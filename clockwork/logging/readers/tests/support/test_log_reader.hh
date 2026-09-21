// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/tests/support/test_message_clk_cc.hh"
#include "clockwork/repr_iface.hh"

#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging
{

struct TestMsgRecord
{
  using MsgType = clockwork::Tappy<tests::TestMessage>;

  LogTimestamp publish_time;
  MsgType msg;

  [[nodiscard]] friend bool operator==(const TestMsgRecord& lhs, const TestMsgRecord& rhs) noexcept = default;
};

class TestLogReader : public AbstractLogReader
{
public:
  TestLogReader(
    std::string_view log_uri,
    std::optional<LogInterval> maybe_log_interval,
    std::optional<RelativeInterval> maybe_relative_interval,
    std::map<std::string, std::vector<TestMsgRecord>> msg_records);
  ~TestLogReader() override;

  TestLogReader(const TestLogReader&) = delete;
  TestLogReader& operator=(const TestLogReader&) = delete;
  TestLogReader(TestLogReader&&) = delete;
  TestLogReader& operator=(TestLogReader&&) = delete;

  [[nodiscard]] std::string type() const override;
  [[nodiscard]] LogExpected<void> open(const std::function<bool(std::string_view)>& topic_filter) override;
  [[nodiscard]] LogExpected<void> close() override;
  [[nodiscard]] std::vector<std::string> get_channels() override;
  [[nodiscard]] std::vector<TopicMetadata> get_metadata() override;
  [[nodiscard]] LogExpected<TopicMetadata> get_channel_metadata(std::string_view channel_name) override;
  [[nodiscard]] LogExpected<LogTimestamp> start_time() override;
  [[nodiscard]] LogExpected<LogTimestamp> end_time() override;
  [[nodiscard]] std::optional<LoggedMessage> next_message_impl() override;

private:
  bool opened_ = false;
  bool closed_ = false;
  /// Original message records.
  std::map<std::string, std::vector<TestMsgRecord>> msg_records_;
  /// Vector of logged messages.
  std::vector<LoggedMessage> logged_msgs_;
  /// Current message iterator.
  std::optional<std::vector<LoggedMessage>::iterator> logged_msg_iter_;
  /// The time of the earliest message
  LogTimestamp log_start_time_{std::numeric_limits<int64_t>::max()};
  /// The time of the last message
  LogTimestamp log_end_time_{std::numeric_limits<int64_t>::min()};
};

} // namespace clockwork_logging
