// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/common/signal_metadata_config_clk_cc.hh"
#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/log_writer_config_clk_cc.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/readers/offboard_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/writers/deterministic_log_writer.hh"
#include "clockwork/logging/writers/persistent_log_entry.hh"
#include "clockwork/logging/writers/tests/support/test_log_writer_config.hh"
#include "clockwork/logging/writers/tests/support/test_publisher.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/runners/deterministic_channel_handler.hh"
#include "clockwork/tools/metrics_channel_metadata/metrics_channel_metadata_config_clk_cc.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>
#include <xxh3.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
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
};

/// Log directory name
constexpr auto log_directory_name = "test_log";

/// Telemetry writer name
constexpr auto writer_name = "telemetry_writer";

/// Pinion unix domain socket namespace
constexpr auto pinion_namespace = "logging_test";

using ChannelUuid = jewels::Uuid<::clockwork::common::EndpointInstanceId>;
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

// NOLINTNEXTLINE(readability-function-size) This is a test case
TEST_CASE("Log messages")
{
  setenv("VEHICLE_ID", "unknown", 1); // NOLINT(concurrency-mt-unsafe). Test case not used in parallel

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  const auto expected_log_path = (test_dir.get_path() / log_directory_name / "telemetry" / writer_name).string();
  const auto chunk_reader_factory =
    std::make_shared<clockwork_logging::offboard::ChunkReaderWriterFactory<>>(memory_resource);

  const jewels::testing::TmpDirectoryGuard shm_dir;
  const auto writer_config_ptr = get_test_log_writer_config();
  auto& writer_config = *writer_config_ptr;

  std::vector<ExpectedMessage> expected_messages;

  {
    constexpr std::chrono::milliseconds message_interval(10);
    LogTimestamp message_time{std::chrono::hours(1)};
    std::vector<tests::TestPublisher> test_publishers;
    test_publishers.reserve(writer_config.get_channels().size());
    ChannelMap channel_map;
    std::map<std::pmr::string, size_t> expected_message_counts;
    std::unordered_set<std::string_view> persistent_channel_set;

    const jewels::time::SyncTime init_time;
    for (auto& channel_config : writer_config.get_mutable_channels())
    {
      // This test needs the channel names to be unique
      channel_config.get_underlying_channel_name().set_truncate(channel_config.get_uuid().to_string());
      if (channel_config.get_channel_type() == ChannelType::persistent)
      {
        persistent_channel_set.emplace(channel_config.get_channel_name());
      }

      auto open_result = tests::TestPublisher::try_open(
        memory_resource,
        shm_dir.get_path().string(),
        pinion_namespace,
        channel_config.get_uuid().to_string(),
        channel_config.get_channel_name(),
        channel_config.get_num_slots(),
        channel_config.get_message_size());
      REQUIRE(open_result);
      test_publishers.emplace_back(std::move(open_result).value());
      auto& publisher = test_publishers.back();
      auto endpoint_uuid_string = channel_config.get_uuid().to_string();
      auto endpoint_uuid = ChannelUuid::from_string(endpoint_uuid_string);
      REQUIRE(endpoint_uuid);
      channel_map.emplace(endpoint_uuid.value(), publisher.underlying_publisher());
    }

    auto test_writer_ptr = std::make_unique<clockwork::DeterministicChannelHandler>(
      memory_resource,
      jewels::memory::make_shared<LogMessageWriter>(
        memory_resource,
        jewels::memory::make_non_null_from_ref(writer_config),
        get_test_persistent_entries(memory_resource, true, false),
        channel_map,
        expected_log_path,
        init_time),
      jewels::memory::make_non_null_from_ref(writer_config),
      channel_map);

    auto& test_writer = *test_writer_ptr;
    REQUIRE(test_writer.initialize());

    for (uint32_t i = 0U; i < writer_config.get_channels().size(); ++i)
    {
      auto& publisher = test_publishers.at(i);
      REQUIRE(publisher.on_connect_pending());
    }

    constexpr size_t messages_per_channel = 10U;
    for (uint32_t i = 0U; i <= messages_per_channel; ++i)
    {
      for (uint32_t j = 0U; j < writer_config.get_channels().size(); ++j)
      {
        auto& publisher = test_publishers.at(j);
        const auto& channel_config = writer_config.get_channels()[j];
        expected_messages.emplace_back();
        auto& expected_message = expected_messages.back();
        expected_message.channel_name = channel_config.get_channel_name();
        expected_message.data.resize(channel_config.get_message_size());
        onboard::tests::fill_with_random_bytes(expected_message.data);
        expected_message.log_time = LogTimestamp{message_time};
        expected_message.message_time = message_time;
        expected_message.sequence_number = i;
        REQUIRE(publisher.try_publish(expected_message.message_time, expected_message.data));
        ++expected_message_counts[expected_message.channel_name];
        message_time += message_interval;
      }
    }
  }

  // Check the logged messages
  OffboardLogReader reader{expected_log_path, {}, {}, DecompressOption::decompress, chunk_reader_factory};
  REQUIRE(reader.open({}));

  // Checked metrics channel metadata persistent message.
  auto maybe_metrics_channel_metadata = reader.next_message();
  REQUIRE(maybe_metrics_channel_metadata);
  const auto& logged_metrics_channel_metadata = *maybe_metrics_channel_metadata;
  REQUIRE(logged_metrics_channel_metadata.topic == metrics_channel_metadata_channel_name);
  const auto metrics_channel_metadata =
    nolint_helper::byte_span_to_value_ptr<clockwork::Tappy<clockwork::tools::MetricsChannelMetadataReport<>>>(
      logged_metrics_channel_metadata.data);
  REQUIRE(metrics_channel_metadata);
  const auto* const metrics_channel_metadata_ptr = metrics_channel_metadata.value();
  REQUIRE(metrics_channel_metadata_ptr->get_underlying_metrics_channels().size() == 2U);

  for (const auto& expected_message : expected_messages)
  {
    auto read_result = reader.next_message();
    REQUIRE(read_result);

    const auto& logged_message = *read_result;
    REQUIRE(logged_message.sequence_number == expected_message.sequence_number);
    REQUIRE(logged_message.topic == expected_message.channel_name);
    REQUIRE(logged_message.log_time >= expected_message.log_time);
    REQUIRE(logged_message.publish_time == expected_message.message_time);
    REQUIRE(logged_message.data.size() == expected_message.data.size());
    REQUIRE(std::memcmp(logged_message.data.data(), expected_message.data.data(), expected_message.data.size()) == 0);
  }
  REQUIRE(!reader.next_message());
}

TEST_CASE("Persistent entries are written before regular messages, in order")
{
  setenv("VEHICLE_ID", "unknown", 1); // NOLINT(concurrency-mt-unsafe). Test case not used in parallel

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  const auto expected_log_path = (test_dir.get_path() / log_directory_name / "telemetry" / writer_name).string();

  const jewels::testing::TmpDirectoryGuard shm_dir;
  const auto writer_config_ptr = get_test_log_writer_config();
  auto& writer_config = *writer_config_ptr;

  {
    const jewels::time::SyncTime init_time;
    std::vector<tests::TestPublisher> test_publishers;
    test_publishers.reserve(writer_config.get_channels().size());
    ChannelMap channel_map;

    for (auto& channel_config : writer_config.get_mutable_channels())
    {
      channel_config.get_underlying_channel_name().set_truncate(channel_config.get_uuid().to_string());
      auto open_result = tests::TestPublisher::try_open(
        memory_resource,
        shm_dir.get_path().string(),
        pinion_namespace,
        channel_config.get_uuid().to_string(),
        channel_config.get_channel_name(),
        channel_config.get_num_slots(),
        channel_config.get_message_size());
      REQUIRE(open_result);
      test_publishers.emplace_back(std::move(open_result).value());
      auto& publisher = test_publishers.back();
      auto endpoint_uuid = ChannelUuid::from_string(channel_config.get_uuid().to_string());
      REQUIRE(endpoint_uuid);
      channel_map.emplace(endpoint_uuid.value(), publisher.underlying_publisher());
    }

    auto test_writer_ptr = std::make_unique<clockwork::DeterministicChannelHandler>(
      memory_resource,
      jewels::memory::make_shared<LogMessageWriter>(
        memory_resource,
        jewels::memory::make_non_null_from_ref(writer_config),
        get_test_persistent_entries(memory_resource, true, true),
        channel_map,
        expected_log_path,
        init_time),
      jewels::memory::make_non_null_from_ref(writer_config),
      channel_map);

    REQUIRE(test_writer_ptr->initialize());

    for (uint32_t i = 0U; i < writer_config.get_channels().size(); ++i)
    {
      REQUIRE(test_publishers.at(i).on_connect_pending());
    }

    const LogTimestamp message_time{std::chrono::hours(1)};
    for (auto& publisher : test_publishers)
    {
      std::vector<std::byte> data(writer_config.get_channels().front().get_message_size());
      onboard::tests::fill_with_random_bytes(data);
      REQUIRE(publisher.try_publish(message_time, data));
    }
  }

  const auto chunk_reader_factory =
    std::make_shared<clockwork_logging::offboard::ChunkReaderWriterFactory<>>(memory_resource);
  OffboardLogReader reader{expected_log_path, {}, {}, DecompressOption::decompress, chunk_reader_factory};
  REQUIRE(reader.open({}));

  auto maybe_metrics = reader.next_message();
  REQUIRE(maybe_metrics);
  REQUIRE(maybe_metrics->topic == metrics_channel_metadata_channel_name);
  const auto metrics_ptr =
    nolint_helper::byte_span_to_value_ptr<clockwork::Tappy<clockwork::tools::MetricsChannelMetadataReport<>>>(
      maybe_metrics->data);
  REQUIRE(metrics_ptr);
  REQUIRE(metrics_ptr.value()->get_underlying_metrics_channels().size() == 2U);

  auto maybe_signal = reader.next_message();
  REQUIRE(maybe_signal);
  REQUIRE(maybe_signal->topic == signal_metadata_channel_name);
  const auto signal_ptr =
    nolint_helper::byte_span_to_value_ptr<clockwork::Tappy<clockwork::common::SignalMetadataConfig<>>>(
      maybe_signal->data);
  REQUIRE(signal_ptr);
  REQUIRE(signal_ptr.value()->get_underlying_signals().size() == 2U);
  REQUIRE(signal_ptr.value()->get_underlying_signals()[0].get_name() == "test_signal_1");
  REQUIRE(signal_ptr.value()->get_underlying_signals()[1].get_name() == "test_signal_2");

  for (size_t i = 0U; i < writer_config.get_channels().size(); ++i)
  {
    auto msg = reader.next_message();
    REQUIRE(msg);
    REQUIRE(msg->topic != metrics_channel_metadata_channel_name);
    REQUIRE(msg->topic != signal_metadata_channel_name);
  }
  REQUIRE(!reader.next_message());
}

} // namespace
} // namespace clockwork_logging::tests
