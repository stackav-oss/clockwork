// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/onboard/clockwork_writer_policy.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/readers/serialization.hh"
#include "clockwork/logging/realtime_playback/initialization_converter.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/tests/support/test_message_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging::realtime_playback
{
namespace
{

using TestMessage = clockwork::Tappy<tests::TestMessage>;
using TestWriter = onboard::Writer<onboard::ClockworkWriterPolicy<>>;

constexpr std::size_t writer_buffer_size = 1024U;
constexpr std::uint32_t old_sequence_number = 1U;
constexpr std::uint32_t lower_sequence_number = 7U;
constexpr std::uint32_t winning_sequence_number = 8U;
constexpr std::int64_t old_publish_time_ns = 10;
constexpr std::int64_t winning_publish_time_ns = 20;

[[nodiscard]] std::string schema_definition()
{
  const auto& definition = clockwork::LoggingTraits<TestMessage>::schema_definition;
  return {definition.data(), definition.size()};
}

void write_message(
  TestWriter& writer, const std::uint32_t sequence_number, const std::int64_t publish_time_ns, std::string_view value)
{
  TestMessage message;
  message.get_underlying_message_string().set_truncate(value);
  constexpr std::array header{std::byte{0x12}, std::byte{0x34}};
  REQUIRE(writer.log_message_wait(
    onboard::Message{
      .channel_name = "/source",
      .sequence_number = sequence_number,
      .log_time = LogTimestamp{publish_time_ns + 1},
      .message_time = LogTimestamp{publish_time_ns},
      .header = header,
      .data = std::as_bytes(std::span{&message, 1}),
    },
    false,
    jewels::time::SteadyClock::now()));
}

void add_source_channel(TestWriter& writer, std::string_view channel_name)
{
  const auto schema = schema_definition();
  REQUIRE(writer.add_channel(
    onboard::LoggedChannelMetadata{
      .channel_name = channel_name,
      .compression_type = CompressionType::none,
      .message_encoding = MessageEncoding::tachyon,
      .channel_type = ChannelType::persistent,
      .schema_name = clockwork::LoggingTraits<TestMessage>::schema_name,
      .schema_encoding = SchemaEncoding::clockwork_tachyon,
      .schema_definition = schema,
    },
    jewels::time::SteadyClock::now()));
}

void write_source(const std::string& path)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  TestWriter writer{
    memory_resource,
    memory_resource,
    writer_buffer_size,
    std::chrono::nanoseconds{0},
    onboard::WriterEnvironment::simulation};
  REQUIRE(writer.open_log(path, "source", jewels::time::SteadyClock::now()));
  add_source_channel(writer, "/source");
  add_source_channel(writer, "/quiet");
  write_message(writer, old_sequence_number, old_publish_time_ns, "old");
  write_message(writer, lower_sequence_number, winning_publish_time_ns, "lower");
  write_message(writer, winning_sequence_number, winning_publish_time_ns, "winner");
  REQUIRE(writer.close_log(jewels::time::SteadyClock::now()));
  REQUIRE(writer.drain_async_operations());
}

TEST_CASE("Initialization selection keeps the first persistent message at the interval start")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const auto source = temp_dir.get_path() / "source";
  const std::string source_string{source.c_str()};
  write_source(source_string);
  constexpr std::array<std::string_view, 2> channels{"/source", "/quiet"};

  InitializationSelection selection;
  REQUIRE(
    jewels::ok(select_initialization_messages(
      jewels::Out{selection},
      source_string,
      LogInterval{LogTimestamp{old_publish_time_ns}, LogTimestamp{winning_publish_time_ns}},
      channels)));
  REQUIRE(selection.size() == 1U);
  const auto& selected = selection.at("/source");
  CHECK(selected.sequence_number == old_sequence_number);
  CHECK(selected.publish_time == LogTimestamp{old_publish_time_ns});
  CHECK(selected.log_time == LogTimestamp{old_publish_time_ns + 1});
  CHECK(selected.header == std::vector{std::byte{0x12}, std::byte{0x34}});
  TestMessage decoded;
  deserialize_tachyon(decoded, selected.data);
  CHECK(decoded.get_message_string() == "old");
}

TEST_CASE("Initialization selection includes a single-point interval at the start boundary")
{
  const jewels::testing::TmpDirectoryGuard temp_dir;
  const auto source = temp_dir.get_path() / "source";
  const std::string source_string{source.c_str()};
  write_source(source_string);
  constexpr std::array<std::string_view, 1> channels{"/source"};

  InitializationSelection selection;
  REQUIRE(
    jewels::ok(select_initialization_messages(
      jewels::Out{selection},
      source_string,
      LogInterval{LogTimestamp{old_publish_time_ns}, LogTimestamp{old_publish_time_ns}},
      channels)));
  REQUIRE(selection.size() == 1U);
  CHECK(selection.at("/source").sequence_number == old_sequence_number);
}

} // namespace
} // namespace clockwork_logging::realtime_playback
