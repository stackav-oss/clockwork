// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_playback/log_message_fetcher.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/log_writer_config.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/writers/tests/support/test_log_writer_config.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace clockwork_logging::tests
{

/// Log directory name
constexpr auto log_directory_name = "test_log";

/// Telemetry writer name
constexpr auto writer_name = "telemetry_writer";

using ChannelUuid = jewels::Uuid<::clockwork::common::EndpointInstanceId>;
TEST_CASE("Test Fetching from a log")
{

  setenv("VEHICLE_ID", "unknown", 1); // NOLINT(concurrency-mt-unsafe) this test runs single-threaded.

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  const auto expected_log_path = (test_dir.get_path() / log_directory_name / "telemetry" / writer_name).string();

  const jewels::testing::TmpDirectoryGuard shm_dir;
  auto writer_config_ptr = tests::get_test_log_writer_config();
  auto& writer_config = *writer_config_ptr;
  const LogTimestamp message_time{std::chrono::hours(1)};

  offboard::Writer writer(memory_resource);
  REQUIRE(writer.open(expected_log_path));

  for (auto& channel_config : writer_config.get_mutable_channels())
  {
    // This test wants the channel names to be unique
    channel_config.get_underlying_channel_name().set_truncate(channel_config.get_uuid().to_string());
    auto endpoint_uuid_string = channel_config.get_uuid().to_string();
    auto endpoint_uuid = ChannelUuid::from_string(endpoint_uuid_string);
    REQUIRE(endpoint_uuid);

    REQUIRE(writer.create_channel(offboard::LoggedChannelMetadata{
      .channel_name = channel_config.get_channel_name(),
      .message_encoding = MessageEncoding::tachyon,
      .channel_type = channel_config.get_channel_type(),
    }));
  }

  constexpr size_t messages_per_channel = 10U;
  for (uint32_t i = 0U; i < messages_per_channel; ++i)
  {
    for (uint32_t j = 0U; j < writer_config.get_channels().size(); ++j)
    {
      const auto& channel_config = writer_config.get_channels()[j];
      std::vector<std::byte> data(channel_config.get_message_size());
      onboard::tests::fill_with_random_bytes(data);
      auto logged_message = offboard::LoggedMessage{
        .channel_name = channel_config.get_channel_name(), .log_time = message_time, .data = data};

      REQUIRE(writer.write(logged_message));
    }
  }
  REQUIRE(writer.close());

  SECTION("Valid log file")
  {
    auto log_fetcher = LogMessageFetcher(
      expected_log_path, jewels::memory::make_non_null_from_ref(writer_config), LogInterval{}, memory_resource);

    REQUIRE(log_fetcher.initialize());
    for (uint32_t i = 0U; i < messages_per_channel; ++i)
    {
      for (uint32_t j = 0U; j < writer_config.get_channels().size(); ++j)
      {
        REQUIRE(log_fetcher.try_fetch_message());
      }
    }
    REQUIRE(!log_fetcher.try_fetch_message());
  }
  SECTION("Invalid Log")
  {
    auto log_fetcher = LogMessageFetcher(
      "junk.log", jewels::memory::make_non_null_from_ref(writer_config), LogInterval{}, memory_resource);

    REQUIRE(!log_fetcher.initialize());
  }
}
} // namespace clockwork_logging::tests
