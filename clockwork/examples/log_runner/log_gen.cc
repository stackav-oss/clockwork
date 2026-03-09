// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/examples/log_runner/test_message_clk_cc.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/span.hh"
#include "jewels/time/conversions.hh"
#include "jewels/time/sync_time.hh"

#include <fmt/format.h>

#include <chrono>
#include <cstdlib>
#include <memory_resource>
#include <span>
#include <string>
#include <tuple>

int main(int /*argc*/, const char** /*argv*/)
{
  using namespace std::chrono_literals;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  constexpr auto log_directory_name = "test_log";
  const auto out_path = jewels::filesystem::Path("/tmp", memory_resource);

  const auto expected_log_path = (out_path / log_directory_name / "log_runner" / "publisher").string();
  clockwork_logging::offboard::Writer writer(memory_resource);

  const auto start_time = jewels::time::SyncTime(std::chrono::hours(1));
  if (!writer.open(expected_log_path))
  {
    jewels::log_cerr_error("error opening logger");
  }
  auto channel_status = writer.create_channel<clockwork::Tappy<clockwork::logging::test::TestMessage>>("test_channel");

  if (!channel_status)
  {
    jewels::log_cerr_error("Error adding channel");
    return EXIT_FAILURE;
  }

  auto channel_status2 =
    writer.create_channel<clockwork::Tappy<clockwork::logging::test::TestMessage>>("test_channel2");

  if (!channel_status2)
  {
    jewels::log_cerr_error("Error adding channel");
    return EXIT_FAILURE;
  }

  const int num_log_messages = 100;
  for (int i = 0; i < num_log_messages; ++i)
  {
    clockwork::Tappy<clockwork::logging::test::TestMessage> test_message{};
    test_message.get_underlying_message_string().set_truncate(fmt::format("A test message index {}", i));

    const auto log_data = as_bytes(jewels::as_single_item_span(test_message));

    auto time =
      jewels::time::get_ns(start_time) + (std::chrono::duration_cast<std::chrono::nanoseconds>(10ms).count() * i);
    auto logged_message = clockwork_logging::offboard::LoggedMessage{
      .channel_name = "test_channel",
      .log_time = clockwork_logging::LogTimestamp{time},
      .transmit_time = clockwork_logging::LogTimestamp{time},
      .data = log_data};
    auto logged_message2 = clockwork_logging::offboard::LoggedMessage{
      .channel_name = "test_channel2",
      .log_time = clockwork_logging::LogTimestamp{time},
      .transmit_time = clockwork_logging::LogTimestamp{time},
      .data = log_data};

    auto writer_status = writer.write(logged_message);
    if (!writer_status)
    {
      jewels::log_cerr_error("Error writing to log");
      return EXIT_FAILURE;
    }
    auto writer_status2 = writer.write(logged_message2);
    if (!writer_status2)
    {
      jewels::log_cerr_error("Error writing to log");
      return EXIT_FAILURE;
    }

    jewels::log_cerr_info("log written success");
  }
  std::ignore = writer.close();
  return EXIT_SUCCESS;
}
