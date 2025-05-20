// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

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
#include "clockwork/logging/schema_encoding.hh"
#include "clockwork/logging/writers/log_writer_state.hh"
#include "clockwork/logging/writers/logger.hh"
#include "clockwork/logging/writers/logger_config.hh"
#include "clockwork/logging/writers/logger_status.hh"
#include "clockwork/logging/writers/tests/support/test_log_writer_config.hh"
#include "clockwork/logging/writers/tests/support/test_publisher.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/fix_catch2_cerr_nonthreadsafe_redirect.hh" // IWYU pragma: keep
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <numeric>
#include <ratio>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
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

/// Time to sleep when waiting for the writer thread to catch up
constexpr auto test_sleep_interval = Logger::writer_run_interval * 2;

TEST_CASE("Log messages")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());

  const jewels::testing::TmpDirectoryGuard shm_dir;
  const auto log_writer_config_ptr = clockwork_logging::tests::get_test_log_writer_config();
  const auto logger_config_ptr =
    clockwork_logging::tests::get_test_logger_config(test_dir.get_path().string(), shm_dir.get_path().string());
  const auto channel_rates_config_ptr = clockwork_logging::tests::get_test_channel_message_rates_config();

  std::string log_directory_name;

  size_t expected_message_count{0U};

  {
    std::vector<clockwork_logging::tests::TestPublisher> test_publishers;
    test_publishers.reserve(static_cast<size_t>(log_writer_config_ptr->get_channels().size()));
    for (const auto& channel_config : log_writer_config_ptr->get_channels())
    {
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
    }
    std::vector<std::size_t> publisher_indexes(test_publishers.size());
    std::iota(publisher_indexes.begin(), publisher_indexes.end(), 0U);

    auto test_logger_ptr =
      std::make_unique<Logger>(memory_resource, *log_writer_config_ptr, *logger_config_ptr, *channel_rates_config_ptr);

    log_directory_name = test_logger_ptr->get_log_directory_name();

    std::map<std::pmr::string, size_t> expected_message_counts;
    std::set<size_t> pending_publishers(publisher_indexes.begin(), publisher_indexes.end());
    std::set<std::pmr::string> expected_channels;
    while (!pending_publishers.empty())
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      for (auto index_iter = pending_publishers.begin(); index_iter != pending_publishers.end();)
      {
        auto& publisher = test_publishers.at(*index_iter);
        REQUIRE(publisher.on_connect_pending());
        if (publisher.num_clients() == 1U)
        {
          expected_message_counts[std::pmr::string(publisher.get_channel_name())] = 0U;
          expected_channels.emplace(publisher.get_channel_name());
          index_iter = pending_publishers.erase(index_iter);
        }
        else
        {
          ++index_iter;
        }
      }
    }

    LoggerStatusTap test_logger_status;
    test_logger_ptr->get_logger_status_message(test_logger_status);
    REQUIRE(test_logger_status.get_state() == LogWriterState::logging);
    REQUIRE(test_logger_status.get_drop_count() == 0U);
    REQUIRE(test_logger_status.get_status_string().empty());

    constexpr size_t messages_per_channel = 10U;
    constexpr std::chrono::milliseconds message_interval(1);
    LogTimestamp message_time{std::chrono::hours(1)};
    for (uint32_t i = 0U; i < messages_per_channel; ++i)
    {
      for (uint32_t j = 0U; j < log_writer_config_ptr->get_channels().size(); ++j)
      {
        ++expected_message_count;
        auto& publisher = test_publishers.at(j);
        const auto& channel_config = log_writer_config_ptr->get_channels()[j];
        std::pmr::vector<std::byte> message_data(channel_config.get_message_size());
        onboard::tests::fill_with_random_bytes(message_data);

        REQUIRE(publisher.try_publish(message_time, message_data));
        ++expected_message_counts[std::pmr::string{channel_config.get_channel_name(), memory_resource}];
        message_time += message_interval;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    while (!expected_channels.empty())
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      const auto& message_counts = test_logger_ptr->get_message_counts();
      for (auto channel_iter = expected_channels.begin(); channel_iter != expected_channels.end();)
      {
        const auto expected_channel = *channel_iter;
        const auto expected_count = expected_message_counts.at(expected_channel);
        CAPTURE(expected_channel);
        if (expected_count == 0)
        {
          REQUIRE_FALSE(message_counts.contains(expected_channel));
          channel_iter = expected_channels.erase(channel_iter);
          continue;
        }
        REQUIRE(message_counts.at(expected_channel) <= expected_count);
        if (message_counts.at(expected_channel) == expected_count)
        {
          channel_iter = expected_channels.erase(channel_iter);
        }
        else
        {
          ++channel_iter;
        }
      }
    }
    std::this_thread::sleep_for(test_sleep_interval);

    test_logger_ptr->get_logger_status_message(test_logger_status);
    REQUIRE(test_logger_status.get_state() == LogWriterState::logging);
    REQUIRE(test_logger_status.get_drop_count() == 0U);
    REQUIRE(test_logger_status.get_status_string().empty());
  }

  const auto expected_log_path = (test_dir.get_path() / log_directory_name).string();

  // Check the logged messages
  onboard::Reader<onboard::BufferedReader<TestReaderPolicy>> reader{memory_resource, expected_log_path};
  REQUIRE(reader.open());
  for (size_t i = 0U; i < expected_message_count; ++i)
  {
    const auto read_result = reader.read_next();
    REQUIRE(read_result);
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
