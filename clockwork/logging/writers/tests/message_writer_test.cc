// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/log_writer_config.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/onboard/buffered_reader.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/reader.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "clockwork/logging/writers/logger_config.hh"
#include "clockwork/logging/writers/message_writer.hh"
#include "clockwork/logging/writers/tests/support/test_log_writer_config.hh"
#include "clockwork/logging/writers/tests/support/test_publisher.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/shared_buffer_pool.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <gsl/util>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace clockwork_logging::tests
{
namespace
{

struct TestReaderPolicy
{
  /// Filesystem library type
  using FilesystemType = jewels::filesystem::Filesystem;

  /// Read buffer size
  static constexpr size_t read_buffer_size = 2U * jewels::math::constants::bytes_per_mib<size_t>;

  /// Maximum read size
  static constexpr size_t max_read_size = onboard::max_log_record_size;

  /// Minimum size of reads when recovering from I/O error
  static constexpr size_t min_io_error_recover_read_size = 512U;
};

/// Expected test message
struct ExpectedMessage
{
  /// Channel name
  std::pmr::string channel_name;
  /// Message data
  std::vector<std::byte> data;
  /// Log time
  LogTimestamp log_time;
  /// Transmit time
  LogTimestamp message_time;
  /// Sequence number
  uint32_t sequence_number{};
};

TEST_CASE("Calculate buffer pool size")
{
  const auto max_write_mib_sec = GENERATE(0U, 4U, 8U);
  const auto max_write_backlog = GENERATE(std::chrono::seconds(0), std::chrono::seconds(1), std::chrono::seconds(2));
  CAPTURE(max_write_mib_sec);
  CAPTURE(max_write_backlog.count());

  REQUIRE(
    MessageWriter::calculate_write_buffer_pool_size(max_write_mib_sec, max_write_backlog) ==
    onboard::Writer<MessageWriter::OnboardWriterPolicy>::calculate_write_buffer_pool_size(
      max_write_mib_sec, max_write_backlog));
}

TEST_CASE("Log messages")
{
  constexpr std::chrono::milliseconds run_interval(1);

  const jewels::testing::TmpDirectoryGuard shm_dir;
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  const auto log_writer_config_ptr = get_test_log_writer_config();
  const auto logger_config_ptr = get_test_logger_config(test_dir.get_path().string(), shm_dir.get_path().string());
  const auto& channel_rates_config_ptr = get_test_channel_message_rates_config();

  std::vector<ExpectedMessage> expected_messages;
  ExpectedMessage expected_persistent_message;

  constexpr auto log_directory_name = "test_log";

  {
    constexpr std::chrono::milliseconds message_interval(1);
    LogTimestamp message_time{std::chrono::hours(1)};
    std::vector<clockwork_logging::tests::TestPublisher> test_publishers;
    test_publishers.reserve(log_writer_config_ptr->get_channels().size());
    std::map<std::pmr::string, size_t> expected_message_counts;
    std::unordered_set<std::string_view> persistent_channel_set;
    for (const auto& channel_config : log_writer_config_ptr->get_channels())
    {
      if (channel_config.get_channel_type() == ChannelType::persistent)
      {
        persistent_channel_set.emplace(channel_config.get_channel_name());
      }

      auto open_result = clockwork_logging::tests::TestPublisher::try_open(
        memory_resource,
        logger_config_ptr->get_pinion_shm_root(),
        logger_config_ptr->get_pinion_namespace(),
        channel_config.get_uuid().to_string(),
        channel_config.get_channel_name(),
        channel_config.get_num_slots(),
        channel_config.get_message_size());
      REQUIRE(open_result);
      test_publishers.emplace_back(std::move(open_result).value());
      auto& publisher = test_publishers.back();
      expected_message_counts[std::pmr::string(publisher.get_channel_name())] = 0U;

      ExpectedMessage expected_message;
      expected_message.channel_name = channel_config.get_channel_name();
      expected_message.data.resize(channel_config.get_message_size());
      onboard::tests::fill_with_random_bytes(expected_message.data);
      expected_message.log_time = LogTimestamp{jewels::time::SyncClock::now().time_since_epoch()};
      expected_message.message_time = message_time;
      expected_message.sequence_number = 0U;

      REQUIRE(publisher.try_publish(expected_message.message_time, expected_message.data));
      message_time += message_interval;

      if (channel_config.get_channel_type() == ChannelType::persistent)
      {
        expected_persistent_message = expected_message;
      }
    }
    REQUIRE(persistent_channel_set.size() == 1U); // This test assumes one persistent channel is logged
    expected_persistent_message.message_time = message_time;
    expected_messages.emplace_back(std::move(expected_persistent_message));

    auto test_writer_ptr = std::make_unique<MessageWriter>(
      memory_resource, *log_writer_config_ptr, *logger_config_ptr, *channel_rates_config_ptr);
    auto& test_writer = *test_writer_ptr;
    REQUIRE(test_writer.initialize());
    test_writer.run_for(run_interval);

    for (auto& publisher : test_publishers)
    {
      REQUIRE(publisher.on_connect_pending());
      REQUIRE(publisher.num_clients() == 1U);
    }

    REQUIRE(test_writer.start_logging(log_directory_name));
    test_writer.run_for(run_interval);

    constexpr size_t messages_per_channel = 10U;
    for (uint32_t i = 1U; i <= messages_per_channel; ++i)
    {
      for (uint32_t j = 0U; j < log_writer_config_ptr->get_channels().size(); ++j)
      {
        auto& publisher = test_publishers.at(j);
        const auto& channel_config = log_writer_config_ptr->get_channels()[j];
        expected_messages.emplace_back();
        auto& expected_message = expected_messages.back();
        expected_message.channel_name = channel_config.get_channel_name();
        expected_message.data.resize(channel_config.get_message_size());
        onboard::tests::fill_with_random_bytes(expected_message.data);
        expected_message.log_time = LogTimestamp{jewels::time::SyncClock::now().time_since_epoch()};
        expected_message.message_time = message_time;
        expected_message.sequence_number = i;

        REQUIRE(publisher.try_publish(expected_message.message_time, expected_message.data));
        ++expected_message_counts[expected_message.channel_name];
        message_time += message_interval;
      }

      test_writer.run_for(run_interval);
      REQUIRE(test_writer.drain_async_operations());
      const auto& message_counts = test_writer.get_message_counts();
      for (const auto& [expected_channel, expected_count] : expected_message_counts)
      {
        CAPTURE(expected_channel);
        if (expected_count == 0)
        {
          REQUIRE_FALSE(message_counts.contains(expected_channel));
        }
        else
        {
          REQUIRE(message_counts.at(expected_channel) == expected_count);
        }
      }
    }

    REQUIRE(test_writer.drain_async_operations());
    REQUIRE(test_writer.get_and_reset_drop_count() == 0U);
  }

  // Check the logged messages
  const auto test_log_path = (test_dir.get_path() / log_directory_name).string();
  onboard::Reader<onboard::BufferedReader<TestReaderPolicy>> reader{memory_resource, test_log_path};
  REQUIRE(reader.open());
  for (const auto& expected_message : expected_messages)
  {
    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    const auto& logged_message = read_result.value();
    REQUIRE(logged_message.channel_name == expected_message.channel_name);
    REQUIRE(logged_message.sequence_number == expected_message.sequence_number);
    REQUIRE(logged_message.log_time >= expected_message.log_time);
    REQUIRE(logged_message.message_time == expected_message.message_time);
    REQUIRE(logged_message.data.size() == expected_message.data.size());
    REQUIRE(std::memcmp(logged_message.data.data(), expected_message.data.data(), expected_message.data.size()) == 0);
  }
  REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));

  // Check the channel metadata
  auto metadata_result = reader.get_channel_metadata("channel1");
  REQUIRE(metadata_result);
  REQUIRE(metadata_result->compression_type == CompressionType::none);
  REQUIRE(metadata_result->message_encoding == MessageEncoding::tachyon);
  REQUIRE(metadata_result->schema_name == "TestType1");
  REQUIRE(metadata_result->schema_encoding == SchemaEncoding::clockwork_tachyon);
  REQUIRE(metadata_result->schema_definition == "Schema definition 1");

  metadata_result = reader.get_channel_metadata("channel2");
  REQUIRE(metadata_result);
  REQUIRE(metadata_result->compression_type == CompressionType::none);
  REQUIRE(metadata_result->message_encoding == MessageEncoding::tachyon);
  REQUIRE(metadata_result->schema_name == "TestType2");
  REQUIRE(metadata_result->schema_encoding == SchemaEncoding::clockwork_tachyon);
  REQUIRE(metadata_result->schema_definition == "Schema definition 2");
}

} // namespace
} // namespace clockwork_logging::tests
