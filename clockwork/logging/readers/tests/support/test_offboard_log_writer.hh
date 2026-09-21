// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace clockwork_logging::tests
{

/// Class to write offboard logs for unit tests
class TestOffboardLogWriter
{
public:
  /// Log start time in nanoseconds since the start of the current epoch
  static constexpr int64_t start_time_ns{1'000'000'000'000};

  /// Log start time
  static constexpr jewels::time::SyncTime start_time{std::chrono::nanoseconds(start_time_ns)};

  /// Logged message interval in nanoseconds
  static constexpr int64_t message_interval_ns{1'000};

  /// Logged message interval
  static constexpr std::chrono::nanoseconds message_interval{message_interval_ns};

  /// Channel1 name
  static constexpr auto channel_name1 = "channel1";

  /// Channel1 metadata
  static constexpr offboard::LoggedChannelMetadata clockwork_metadata1{
    .channel_name = channel_name1,
    .message_encoding = MessageEncoding::tachyon,
    .channel_type = ChannelType::regular,
    .schema_name = "schema1",
    .schema_encoding = SchemaEncoding::undefined,
    .schema_definition = "",
    .is_amended = true,
  };

  /// Channel 1 default header size
  static constexpr size_t default_header1_size = 123U;

  /// Channel 1 default data size
  static constexpr size_t default_data1_size = 1234U;

  /// Channel2 name
  static constexpr auto channel_name2 = "channel2";

  /// Channel2 metadata
  static constexpr offboard::LoggedChannelMetadata clockwork_metadata2{
    .channel_name = channel_name2,
    .message_encoding = MessageEncoding::tachyon,
    .channel_type = ChannelType::persistent,
    .schema_name = "schema2",
    .schema_encoding = SchemaEncoding::undefined,
    .schema_definition = "",
    .is_amended = false,
  };

  /// Channel 2 default header size
  static constexpr size_t default_header2_size = 234U;

  /// Channel 2 default data size
  static constexpr size_t default_data2_size = 2345U;

  /// Channel3 name
  static constexpr auto channel_name3 = "channel3";

  /// Channel3 metadata
  static constexpr offboard::LoggedChannelMetadata clockwork_metadata3{
    .channel_name = channel_name3,
    .message_encoding = MessageEncoding::tachyon,
    .channel_type = ChannelType::regular,
    .schema_name = "schema3",
    .schema_encoding = SchemaEncoding::undefined,
    .schema_definition = "",
    .is_amended = true,
  };

  /// Channel 3 default header size
  static constexpr size_t default_header3_size = 345U;

  /// Channel 3 default data size
  static constexpr size_t default_data3_size = 3456U;

  /// TapMsg channel
  static constexpr auto nested_schema_channel_name = "nested_schema";

  /// Constructor
  TestOffboardLogWriter();

  ~TestOffboardLogWriter() noexcept = default;

  TestOffboardLogWriter(const TestOffboardLogWriter&) = delete;
  TestOffboardLogWriter& operator=(const TestOffboardLogWriter&) = delete;
  TestOffboardLogWriter(TestOffboardLogWriter&&) noexcept = default;
  TestOffboardLogWriter& operator=(TestOffboardLogWriter&&) noexcept = default;

  /// Write a clockwork test log
  /// @param[in] log_dir Test log directory
  /// @param[in] message_count Number of messages per channel
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> write_clockwork_test_log(std::string_view log_dir, size_t message_count) const;

  /// Channel 1 header accessor
  [[nodiscard]] std::span<const std::byte> header1() const;

  /// Set the channel 1 header
  void set_header1(std::span<const std::byte> header);

  /// Channel 1 data accessor
  [[nodiscard]] std::span<const std::byte> data1() const;

  /// Set the channel 1 data
  void set_data1(std::span<const std::byte> data);

  /// Channel 2 header accessor
  [[nodiscard]] std::span<const std::byte> header2() const;

  /// Set the channel 2 header
  void set_header2(std::span<const std::byte> header);

  /// Channel 2 data accessor
  [[nodiscard]] std::span<const std::byte> data2() const;

  /// Channel 2 compressed data accessor
  [[nodiscard]] std::span<const std::byte> compressed_data2() const;

  /// Set the channel 2 data
  void set_data2(std::span<const std::byte> data);

  /// Channel 3 header accessor
  [[nodiscard]] std::span<const std::byte> header3() const;

  /// Set the channel 3 header
  void set_header3(std::span<const std::byte> header);

  /// Channel 3 data accessor
  [[nodiscard]] std::span<const std::byte> data3() const;

  /// Set the channel 3 data
  void set_data3(std::span<const std::byte> data);

private:
  /// Channel 1 header
  std::vector<std::byte> header1_;

  /// Channel 1 data
  std::vector<std::byte> data1_;

  /// Channel 2 header
  std::vector<std::byte> header2_;

  /// Channel 2 data
  std::vector<std::byte> data2_;

  /// Lite compressed channel 2 data
  std::vector<std::byte> compressed_data2_;

  /// Channel 3 header
  std::vector<std::byte> header3_;

  /// Channel 3 data
  std::vector<std::byte> data3_;
};

} // namespace clockwork_logging::tests
