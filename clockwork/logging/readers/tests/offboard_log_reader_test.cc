// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/readers/offboard_log_reader.hh"
#include "clockwork/logging/readers/tests/support/test_offboard_log_writer.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging::tests
{
namespace
{

TEST_CASE("Offboard reader topics")
{
  const auto decompress_option = GENERATE(DecompressOption::decompress, DecompressOption::dont_decompress);
  CAPTURE(decompress_option);

  constexpr auto test_log_name = "test_log";
  constexpr size_t message_count = 100U;

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_log_path = test_dir.get_path() / test_log_name;
  const TestOffboardLogWriter test_writer;
  REQUIRE(test_writer.write_clockwork_test_log(test_log_path.string(), message_count));

  OffboardLogReader reader(test_log_path.string(), {}, {}, decompress_option);
  REQUIRE(reader.type() == "offboard");

  auto expected = std::vector<TopicMetadata>({
    {
      .name = std::string{TestOffboardLogWriter::clockwork_metadata1.channel_name},
      .type = std::string{TestOffboardLogWriter::clockwork_metadata1.schema_name},
      .message_encoding = TestOffboardLogWriter::clockwork_metadata1.message_encoding,
      .channel_type = TestOffboardLogWriter::clockwork_metadata1.channel_type,
      .schema_encoding = TestOffboardLogWriter::clockwork_metadata1.schema_encoding,
      .schema_definition = std::string{TestOffboardLogWriter::clockwork_metadata1.schema_definition},
    },
    {
      .name = std::string{TestOffboardLogWriter::clockwork_metadata2.channel_name},
      .type = std::string{TestOffboardLogWriter::clockwork_metadata2.schema_name},
      .message_encoding = TestOffboardLogWriter::clockwork_metadata2.message_encoding,
      .channel_type = TestOffboardLogWriter::clockwork_metadata2.channel_type,
      .schema_encoding = TestOffboardLogWriter::clockwork_metadata2.schema_encoding,
      .schema_definition = std::string{TestOffboardLogWriter::clockwork_metadata2.schema_definition},
    },
    {
      .name = std::string{TestOffboardLogWriter::clockwork_metadata3.channel_name},
      .type = std::string{TestOffboardLogWriter::clockwork_metadata3.schema_name},
      .message_encoding = TestOffboardLogWriter::clockwork_metadata3.message_encoding,
      .channel_type = TestOffboardLogWriter::clockwork_metadata3.channel_type,
      .schema_encoding = TestOffboardLogWriter::clockwork_metadata3.schema_encoding,
      .schema_definition = std::string{TestOffboardLogWriter::clockwork_metadata3.schema_definition},
    },
  });

  auto actual = reader.get_metadata();
  REQUIRE(expected == actual);

  const auto channel1_metadata = reader.get_channel_metadata(TestOffboardLogWriter::clockwork_metadata1.channel_name);
  REQUIRE(channel1_metadata);
  REQUIRE(channel1_metadata == expected.front());

  const auto channel3_metadata = reader.get_channel_metadata(TestOffboardLogWriter::clockwork_metadata3.channel_name);
  REQUIRE(channel3_metadata);
  REQUIRE(channel3_metadata == expected.back());

  REQUIRE(
    reader.get_channels() == std::vector<std::string>{
                               TestOffboardLogWriter::channel_name1,
                               TestOffboardLogWriter::channel_name2,
                               TestOffboardLogWriter::channel_name3});
}

TEST_CASE("Offboard reader metrics")
{
  const auto decompress_option = GENERATE(DecompressOption::decompress, DecompressOption::dont_decompress);
  CAPTURE(decompress_option);

  constexpr auto test_log_name = "test_log";
  constexpr size_t message_count = 100U;

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_log_path = test_dir.get_path() / test_log_name;
  const TestOffboardLogWriter test_writer;
  REQUIRE(test_writer.write_clockwork_test_log(test_log_path.string(), message_count));

  OffboardLogReader reader(test_log_path.string(), {}, {}, decompress_option);

  const auto metrics_result = reader.get_metrics();
  REQUIRE(metrics_result);
  const auto& metrics = metrics_result.value();
  REQUIRE(
    metrics.transmit_time_interval ==
    LogInterval{
      TestOffboardLogWriter::start_time,
      TestOffboardLogWriter::start_time +
        TestOffboardLogWriter::message_interval * ((static_cast<size_t>(message_count) * 3) - 1)});
  REQUIRE(metrics.message_count == static_cast<size_t>(message_count) * 3U);
  REQUIRE(
    metrics.byte_count ==
    static_cast<size_t>(message_count) *
      (test_writer.header1().size() + test_writer.data1().size() + test_writer.header2().size() +
       test_writer.compressed_data2().size() + test_writer.header3().size() + test_writer.data3().size()));
  REQUIRE(metrics.topic_metrics.size() == 3U);
  REQUIRE(metrics.topic_metrics.at(0U).topic == TestOffboardLogWriter::channel_name1);
  REQUIRE(
    metrics.topic_metrics.at(0U).transmit_time_interval ==
    LogInterval{
      TestOffboardLogWriter::start_time,
      TestOffboardLogWriter::start_time +
        TestOffboardLogWriter::message_interval * ((static_cast<size_t>(message_count) * 3) - 3)});
  REQUIRE(metrics.topic_metrics.at(0U).message_count == static_cast<size_t>(message_count));
  REQUIRE(
    metrics.topic_metrics.at(0U).byte_count ==
    static_cast<size_t>(message_count) * (test_writer.header1().size() + test_writer.data1().size()));
  REQUIRE(metrics.topic_metrics.at(1U).topic == TestOffboardLogWriter::channel_name2);
  REQUIRE(
    metrics.topic_metrics.at(1U).transmit_time_interval ==
    LogInterval{
      TestOffboardLogWriter::start_time + TestOffboardLogWriter::message_interval,
      TestOffboardLogWriter::start_time +
        TestOffboardLogWriter::message_interval * ((static_cast<size_t>(message_count) * 3) - 2)});
  REQUIRE(metrics.topic_metrics.at(1U).message_count == static_cast<size_t>(message_count));
  REQUIRE(
    metrics.topic_metrics.at(1U).byte_count ==
    static_cast<size_t>(message_count) * (test_writer.header2().size() + test_writer.compressed_data2().size()));
  REQUIRE(metrics.topic_metrics.at(2U).topic == TestOffboardLogWriter::channel_name3);
  REQUIRE(
    metrics.topic_metrics.at(2U).transmit_time_interval ==
    LogInterval{
      TestOffboardLogWriter::start_time + TestOffboardLogWriter::message_interval * 2,
      TestOffboardLogWriter::start_time +
        TestOffboardLogWriter::message_interval * ((static_cast<size_t>(message_count) * 3) - 1)});
  REQUIRE(metrics.topic_metrics.at(2U).message_count == static_cast<size_t>(message_count));
  REQUIRE(
    metrics.topic_metrics.at(2U).byte_count ==
    static_cast<size_t>(message_count) * (test_writer.header3().size() + test_writer.data3().size()));
}

TEST_CASE("Offboard reader read all messages")
{
  const auto decompress_option = GENERATE(DecompressOption::decompress, DecompressOption::dont_decompress);
  CAPTURE(decompress_option);

  constexpr auto test_log_name = "test_log";
  constexpr size_t message_count = 100U;

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_log_path = test_dir.get_path() / test_log_name;
  const TestOffboardLogWriter test_writer;
  REQUIRE(test_writer.write_clockwork_test_log(test_log_path.string(), message_count));

  OffboardLogReader reader(test_log_path.string(), {}, {}, decompress_option);
  REQUIRE(reader.open({}));
  REQUIRE(reader.start_time() == LogTimestamp{TestOffboardLogWriter::start_time});
  REQUIRE(
    reader.end_time() == LogTimestamp{
                           TestOffboardLogWriter::start_time +
                           TestOffboardLogWriter::message_interval * ((static_cast<size_t>(message_count) * 3) - 1)});

  auto expected_time = LogTimestamp{TestOffboardLogWriter::start_time};
  for (size_t i = 0U; i < message_count; ++i)
  {
    CAPTURE(i);

    auto maybe_msg = reader.next_message();
    REQUIRE(maybe_msg);
    REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name1);
    REQUIRE(maybe_msg->sequence_number == static_cast<uint32_t>(i * 4U));
    REQUIRE(maybe_msg->publish_time == expected_time);
    REQUIRE_FALSE(maybe_msg->is_lite_compressed);
    REQUIRE(maybe_msg->data.size() == test_writer.data1().size());
    REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data1().data(), test_writer.data1().size()) == 0);
    expected_time += TestOffboardLogWriter::message_interval;

    maybe_msg = reader.next_message();
    REQUIRE(maybe_msg);
    REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name2);
    REQUIRE(maybe_msg->sequence_number == static_cast<uint32_t>((i * 4U) + 1U));
    REQUIRE(maybe_msg->publish_time == expected_time);
    if (decompress_option == DecompressOption::decompress)
    {
      REQUIRE_FALSE(maybe_msg->is_lite_compressed);
      REQUIRE(maybe_msg->data.size() == test_writer.data2().size());
      REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data2().data(), test_writer.data2().size()) == 0);
    }
    else
    {
      REQUIRE(maybe_msg->is_lite_compressed);
      REQUIRE(maybe_msg->data.size() == test_writer.compressed_data2().size());
      REQUIRE(
        std::memcmp(
          maybe_msg->data.data(), test_writer.compressed_data2().data(), test_writer.compressed_data2().size()) == 0);
    }
    expected_time += TestOffboardLogWriter::message_interval;

    maybe_msg = reader.next_message();
    REQUIRE(maybe_msg);
    REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name3);
    REQUIRE(maybe_msg->sequence_number == static_cast<uint32_t>((i * 4U) + 2U));
    REQUIRE(maybe_msg->publish_time == expected_time);
    REQUIRE_FALSE(maybe_msg->is_lite_compressed);
    REQUIRE(maybe_msg->data.size() == test_writer.data3().size());
    REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data3().data(), test_writer.data3().size()) == 0);
    expected_time += TestOffboardLogWriter::message_interval;
  }

  REQUIRE_FALSE(reader.next_message());
  REQUIRE(reader.close());
}

TEST_CASE("Offboard reader filter topics")
{
  const auto decompress_option = GENERATE(DecompressOption::decompress, DecompressOption::dont_decompress);
  CAPTURE(decompress_option);

  constexpr auto test_log_name = "test_log";
  constexpr size_t message_count = 100U;

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_log_path = test_dir.get_path() / test_log_name;
  const TestOffboardLogWriter test_writer;
  REQUIRE(test_writer.write_clockwork_test_log(test_log_path.string(), message_count));

  OffboardLogReader reader(test_log_path.string(), {}, {}, decompress_option);
  REQUIRE(reader.open([](const auto& topic) { return topic == "channel1"; }));

  auto expected_time = LogTimestamp{TestOffboardLogWriter::start_time};
  for (size_t i = 0U; i < message_count; ++i)
  {
    CAPTURE(i);

    auto maybe_msg = reader.next_message();
    REQUIRE(maybe_msg);
    REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name1);
    REQUIRE(maybe_msg->publish_time == expected_time);
    REQUIRE_FALSE(maybe_msg->is_lite_compressed);
    REQUIRE(maybe_msg->data.size() == test_writer.data1().size());
    REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data1().data(), test_writer.data1().size()) == 0);
    expected_time += TestOffboardLogWriter::message_interval * 3;
  }

  REQUIRE_FALSE(reader.next_message());
  REQUIRE(reader.close());
}

TEST_CASE("Offboard reader interval")
{
  constexpr auto test_log_name = "test_log";
  constexpr size_t message_count = 100U;

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_log_path = test_dir.get_path() / test_log_name;
  const TestOffboardLogWriter test_writer;
  REQUIRE(test_writer.write_clockwork_test_log(test_log_path.string(), message_count));

  const auto first_message_index = GENERATE_REF(range(20U, 22U));
  CAPTURE(first_message_index);

  const LogInterval log_interval{
    TestOffboardLogWriter::start_time +
      TestOffboardLogWriter::message_interval * static_cast<int64_t>(first_message_index),
    TestOffboardLogWriter::start_time + TestOffboardLogWriter::message_interval * 40};

  OffboardLogReader reader(test_log_path.string(), log_interval, {}, DecompressOption::decompress);
  REQUIRE(reader.open({}));

  auto expected_time = LogTimestamp{TestOffboardLogWriter::start_time};
  bool read_first_persistent = false;
  for (size_t i = 0U; i < message_count; ++i)
  {
    CAPTURE(i);

    if (log_interval.contains(expected_time))
    {
      if (!read_first_persistent)
      {
        const auto maybe_msg = reader.next_message();
        REQUIRE(maybe_msg);
        REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name2);
        REQUIRE(maybe_msg->publish_time == expected_time);
        REQUIRE_FALSE(maybe_msg->is_lite_compressed);
        REQUIRE(maybe_msg->data.size() == test_writer.data2().size());
        REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data2().data(), test_writer.data2().size()) == 0);
        REQUIRE(maybe_msg->is_repeated_persistent);
        read_first_persistent = true;
      }

      const auto maybe_msg = reader.next_message();
      REQUIRE(maybe_msg);
      REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name1);
      REQUIRE(maybe_msg->publish_time == expected_time);
      REQUIRE(maybe_msg->data.size() == test_writer.data1().size());
      REQUIRE_FALSE(maybe_msg->is_lite_compressed);
      REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data1().data(), test_writer.data1().size()) == 0);
      REQUIRE_FALSE(maybe_msg->is_repeated_persistent);
    }
    expected_time += TestOffboardLogWriter::message_interval;

    if (log_interval.contains(expected_time))
    {
      const auto maybe_msg = reader.next_message();
      REQUIRE(maybe_msg);
      REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name2);
      REQUIRE(maybe_msg->publish_time == expected_time);
      REQUIRE_FALSE(maybe_msg->is_lite_compressed);
      REQUIRE(maybe_msg->data.size() == test_writer.data2().size());
      REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data2().data(), test_writer.data2().size()) == 0);
      REQUIRE_FALSE(maybe_msg->is_repeated_persistent);
      read_first_persistent = true;
    }
    expected_time += TestOffboardLogWriter::message_interval;

    if (log_interval.contains(expected_time))
    {
      if (!read_first_persistent)
      {
        const auto maybe_msg = reader.next_message();
        REQUIRE(maybe_msg);
        REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name2);
        REQUIRE(maybe_msg->publish_time == expected_time);
        REQUIRE_FALSE(maybe_msg->is_lite_compressed);
        REQUIRE(maybe_msg->data.size() == test_writer.data2().size());
        REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data2().data(), test_writer.data2().size()) == 0);
        REQUIRE(maybe_msg->is_repeated_persistent);
        read_first_persistent = true;
      }

      const auto maybe_msg = reader.next_message();
      REQUIRE(maybe_msg);
      REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name3);
      REQUIRE(maybe_msg->publish_time == expected_time);
      REQUIRE_FALSE(maybe_msg->is_lite_compressed);
      REQUIRE(maybe_msg->data.size() == test_writer.data3().size());
      REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data3().data(), test_writer.data3().size()) == 0);
      REQUIRE_FALSE(maybe_msg->is_repeated_persistent);
    }
    expected_time += TestOffboardLogWriter::message_interval;
  }

  REQUIRE_FALSE(reader.next_message());
  REQUIRE(reader.close());
}

TEST_CASE("Offboard reader relative interval")
{
  constexpr auto test_log_name = "test_log";
  constexpr size_t message_count = 100U;

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_log_path = test_dir.get_path() / test_log_name;
  const TestOffboardLogWriter test_writer;
  REQUIRE(test_writer.write_clockwork_test_log(test_log_path.string(), message_count));

  const auto first_message_index = GENERATE_REF(range(20U, 22U));
  CAPTURE(first_message_index);

  const LogInterval log_interval{
    TestOffboardLogWriter::start_time +
      TestOffboardLogWriter::message_interval * static_cast<int64_t>(first_message_index),
    TestOffboardLogWriter::start_time + TestOffboardLogWriter::message_interval * 40};

  OffboardLogReader reader(
    test_log_path.string(),
    {},
    RelativeInterval{
      .start_offset = TestOffboardLogWriter::message_interval * static_cast<int64_t>(first_message_index),
      .end_offset = TestOffboardLogWriter::message_interval * 40},
    DecompressOption::decompress);
  REQUIRE(reader.open({}));

  auto expected_time = LogTimestamp{TestOffboardLogWriter::start_time};
  bool read_first_persistent = false;
  for (size_t i = 0U; i < message_count; ++i)
  {
    CAPTURE(i);

    if (log_interval.contains(expected_time))
    {
      if (!read_first_persistent)
      {
        const auto maybe_msg = reader.next_message();
        REQUIRE(maybe_msg);
        REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name2);
        REQUIRE(maybe_msg->publish_time == expected_time);
        REQUIRE_FALSE(maybe_msg->is_lite_compressed);
        REQUIRE(maybe_msg->data.size() == test_writer.data2().size());
        REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data2().data(), test_writer.data2().size()) == 0);
        REQUIRE(maybe_msg->is_repeated_persistent);
        read_first_persistent = true;
      }

      const auto maybe_msg = reader.next_message();
      REQUIRE(maybe_msg);
      REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name1);
      REQUIRE(maybe_msg->publish_time == expected_time);
      REQUIRE_FALSE(maybe_msg->is_lite_compressed);
      REQUIRE(maybe_msg->data.size() == test_writer.data1().size());
      REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data1().data(), test_writer.data1().size()) == 0);
      REQUIRE_FALSE(maybe_msg->is_repeated_persistent);
    }
    expected_time += TestOffboardLogWriter::message_interval;

    if (log_interval.contains(expected_time))
    {
      const auto maybe_msg = reader.next_message();
      REQUIRE(maybe_msg);
      REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name2);
      REQUIRE(maybe_msg->publish_time == expected_time);
      REQUIRE_FALSE(maybe_msg->is_lite_compressed);
      REQUIRE(maybe_msg->data.size() == test_writer.data2().size());
      REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data2().data(), test_writer.data2().size()) == 0);
      REQUIRE_FALSE(maybe_msg->is_repeated_persistent);
      read_first_persistent = true;
    }
    expected_time += TestOffboardLogWriter::message_interval;

    if (log_interval.contains(expected_time))
    {
      if (!read_first_persistent)
      {
        const auto maybe_msg = reader.next_message();
        REQUIRE(maybe_msg);
        REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name2);
        REQUIRE(maybe_msg->publish_time == expected_time);
        REQUIRE_FALSE(maybe_msg->is_lite_compressed);
        REQUIRE(maybe_msg->data.size() == test_writer.data2().size());
        REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data2().data(), test_writer.data2().size()) == 0);
        REQUIRE(maybe_msg->is_repeated_persistent);
        read_first_persistent = true;
      }

      const auto maybe_msg = reader.next_message();
      REQUIRE(maybe_msg);
      REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name3);
      REQUIRE(maybe_msg->publish_time == expected_time);
      REQUIRE_FALSE(maybe_msg->is_lite_compressed);
      REQUIRE(maybe_msg->data.size() == test_writer.data3().size());
      REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data3().data(), test_writer.data3().size()) == 0);
      REQUIRE_FALSE(maybe_msg->is_repeated_persistent);
    }
    expected_time += TestOffboardLogWriter::message_interval;
  }

  REQUIRE_FALSE(reader.next_message());
  REQUIRE(reader.close());
}

TEST_CASE("Offboard reader relative interval with just start offset")
{
  constexpr auto test_log_name = "test_log";
  constexpr size_t message_count = 100U;

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_log_path = test_dir.get_path() / test_log_name;
  const TestOffboardLogWriter test_writer;
  REQUIRE(test_writer.write_clockwork_test_log(test_log_path.string(), message_count));

  const auto first_message_index = GENERATE_REF(range(20U, 22U));
  CAPTURE(first_message_index);

  const LogInterval log_interval{
    TestOffboardLogWriter::start_time +
      TestOffboardLogWriter::message_interval * static_cast<int64_t>(first_message_index),
    jewels::time::SyncTime{std::chrono::nanoseconds(std::numeric_limits<int64_t>::max())}};

  OffboardLogReader reader(
    test_log_path.string(),
    {},
    RelativeInterval{
      .start_offset = TestOffboardLogWriter::message_interval * static_cast<int64_t>(first_message_index)},
    DecompressOption::decompress);
  REQUIRE(reader.open({}));

  auto expected_time = LogTimestamp{TestOffboardLogWriter::start_time};
  bool read_first_persistent = false;
  for (size_t i = 0U; i < message_count; ++i)
  {
    CAPTURE(i);

    if (log_interval.contains(expected_time))
    {
      if (!read_first_persistent)
      {
        const auto maybe_msg = reader.next_message();
        REQUIRE(maybe_msg);
        REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name2);
        REQUIRE(maybe_msg->publish_time == expected_time);
        REQUIRE_FALSE(maybe_msg->is_lite_compressed);
        REQUIRE(maybe_msg->data.size() == test_writer.data2().size());
        REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data2().data(), test_writer.data2().size()) == 0);
        REQUIRE(maybe_msg->is_repeated_persistent);
        read_first_persistent = true;
      }

      const auto maybe_msg = reader.next_message();
      REQUIRE(maybe_msg);
      REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name1);
      REQUIRE(maybe_msg->publish_time == expected_time);
      REQUIRE_FALSE(maybe_msg->is_lite_compressed);
      REQUIRE(maybe_msg->data.size() == test_writer.data1().size());
      REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data1().data(), test_writer.data1().size()) == 0);
      REQUIRE_FALSE(maybe_msg->is_repeated_persistent);
    }
    expected_time += TestOffboardLogWriter::message_interval;

    if (log_interval.contains(expected_time))
    {
      const auto maybe_msg = reader.next_message();
      REQUIRE(maybe_msg);
      REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name2);
      REQUIRE(maybe_msg->publish_time == expected_time);
      REQUIRE_FALSE(maybe_msg->is_lite_compressed);
      REQUIRE(maybe_msg->data.size() == test_writer.data2().size());
      REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data2().data(), test_writer.data2().size()) == 0);
      REQUIRE_FALSE(maybe_msg->is_repeated_persistent);
      read_first_persistent = true;
    }
    expected_time += TestOffboardLogWriter::message_interval;

    if (log_interval.contains(expected_time))
    {
      if (!read_first_persistent)
      {
        const auto maybe_msg = reader.next_message();
        REQUIRE(maybe_msg);
        REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name2);
        REQUIRE(maybe_msg->publish_time == expected_time);
        REQUIRE_FALSE(maybe_msg->is_lite_compressed);
        REQUIRE(maybe_msg->data.size() == test_writer.data2().size());
        REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data2().data(), test_writer.data2().size()) == 0);
        REQUIRE(maybe_msg->is_repeated_persistent);
        read_first_persistent = true;
      }

      const auto maybe_msg = reader.next_message();
      REQUIRE(maybe_msg);
      REQUIRE(maybe_msg->topic == TestOffboardLogWriter::channel_name3);
      REQUIRE(maybe_msg->publish_time == expected_time);
      REQUIRE_FALSE(maybe_msg->is_lite_compressed);
      REQUIRE(maybe_msg->data.size() == test_writer.data3().size());
      REQUIRE(std::memcmp(maybe_msg->data.data(), test_writer.data3().data(), test_writer.data3().size()) == 0);
      REQUIRE_FALSE(maybe_msg->is_repeated_persistent);
    }
    expected_time += TestOffboardLogWriter::message_interval;
  }

  REQUIRE_FALSE(reader.next_message());
  REQUIRE(reader.close());
}

} // namespace
} // namespace clockwork_logging::tests
