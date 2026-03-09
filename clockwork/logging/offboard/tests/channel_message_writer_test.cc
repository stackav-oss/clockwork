// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/async_work_queue.hh"
#include "clockwork/logging/offboard/channel_message_writer.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/file_chunk_reader.hh"
#include "clockwork/logging/offboard/file_chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/message_chunk_reader.hh"
#include "clockwork/logging/offboard/tests/support/test_support.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/testing/filesystem_wrapper.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("ChannelMessageWriter")
{
  constexpr auto test_file_name = "test_file.slog";
  constexpr auto channel_name = "test_channel";

  const auto message_chunk_index_format = GENERATE(MessageChunkIndexFormat::v1, MessageChunkIndexFormat::v2);
  CAPTURE(message_chunk_index_format);

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_file_path = test_dir.get_path() / test_file_name;
  const auto compressor_ptr =
    jewels::memory::allocate_shared<ChunkCompressor, std::pmr::polymorphic_allocator<ChunkCompressor>>(
      memory_resource, memory_resource);
  LiteCompressor lite_compressor{memory_resource};
  const auto writer_result = FileChunkWriter<>::make_shared(test_file_path.string(), memory_resource);
  REQUIRE(writer_result);
  const auto& file_writer_ptr = writer_result.value();
  const auto reader_result = FileChunkReader<jewels::filesystem::testing::FilesystemWrapper>::make_shared(
    test_file_path.string(), memory_resource);
  REQUIRE(reader_result);
  auto& file_reader = *reader_result.value();
  MessageChunkReader message_reader{memory_resource, channel_name, ChannelType::regular, {}, CompressionType::zstd};
  const auto async_work_queue_ptr =
    jewels::memory::allocate_shared<AsyncWorkQueue, std::pmr::polymorphic_allocator<AsyncWorkQueue>>(
      memory_resource, 1U);
  ChannelMessageWriter message_writer{
    memory_resource,
    message_chunk_index_format,
    CompressionType::zstd,
    compressor_ptr,
    file_writer_ptr,
    async_work_queue_ptr};

  constexpr auto header_size = 125U;
  constexpr auto data_size = 1234U;
  constexpr LogTimestamp time1{std::chrono::seconds(1)};
  constexpr LogTimestamp time2{std::chrono::seconds(2)};
  constexpr LogTimestamp time3{std::chrono::seconds(3)};

  std::vector<std::byte> message1_data(data_size);
  onboard::tests::fill_with_random_bytes(message1_data);
  tests::TestLoggedMessage message1{
    .channel_name = channel_name,
    .sequence_number = 1U,
    .log_time = time1,
    .logged_transmit_time = time1,
    .expected_transmit_time = time1,
    .header = std::vector<std::byte>(header_size),
    .data = offboard::tests::zero_copy_lite_compress(message1_data, lite_compressor),
    .is_repeated_persistent = false,
    .is_lite_compressed = true,
  };
  onboard::tests::fill_with_random_bytes(message1.header);

  std::vector<std::byte> message2_data(data_size);
  onboard::tests::fill_with_random_bytes(message2_data);
  tests::TestLoggedMessage message2{
    .channel_name = channel_name,
    .sequence_number = 2U,
    .log_time = time2,
    .logged_transmit_time = time2,
    .expected_transmit_time = time2,
    .header = std::vector<std::byte>(header_size),
    .data = offboard::tests::zero_copy_lite_compress(message2_data, lite_compressor),
    .is_repeated_persistent = true,
    .is_lite_compressed = false,
  };
  onboard::tests::fill_with_random_bytes(message2.header);

  std::vector<std::byte> message3_data(data_size);
  onboard::tests::fill_with_random_bytes(message3_data);
  tests::TestLoggedMessage message3{
    .channel_name = channel_name,
    .sequence_number = 3U,
    .log_time = time3,
    .logged_transmit_time = time3,
    .expected_transmit_time = time3,
    .header = std::vector<std::byte>(header_size),
    .data = offboard::tests::zero_copy_lite_compress(message2_data, lite_compressor),
    .is_repeated_persistent = false,
    .is_lite_compressed = true,
  };
  onboard::tests::fill_with_random_bytes(message3.header);

  REQUIRE(file_writer_ptr->open());
  REQUIRE(message_writer.add_message(
    offboard::tests::spans_size(message3.data),
    ZeroCopyLoggedMessage{
      .sequence_number = message3.sequence_number,
      .log_time = message3.log_time,
      .transmit_time = message3.logged_transmit_time,
      .header = message3.header,
      .data = message3.data,
      .is_repeated_persistent = message3.is_repeated_persistent,
      .is_lite_compressed = message3.is_lite_compressed,
    }));
  REQUIRE(message_writer.add_message(
    offboard::tests::spans_size(message2.data),
    ZeroCopyLoggedMessage{
      .sequence_number = message2.sequence_number,
      .log_time = message2.log_time,
      .transmit_time = message2.logged_transmit_time,
      .header = message2.header,
      .data = message2.data,
      .is_repeated_persistent = message2.is_repeated_persistent,
      .is_lite_compressed = message2.is_lite_compressed,
    }));
  REQUIRE(message_writer.flush_current_chunk());
  REQUIRE(message_writer.add_message(
    offboard::tests::spans_size(message2.data),
    ZeroCopyLoggedMessage{
      .sequence_number = message2.sequence_number,
      .log_time = message2.log_time,
      .transmit_time = message2.logged_transmit_time,
      .header = message2.header,
      .data = message2.data,
      .is_repeated_persistent = message2.is_repeated_persistent,
      .is_lite_compressed = message2.is_lite_compressed,
    }));
  REQUIRE(message_writer.add_message(
    offboard::tests::spans_size(message1.data),
    ZeroCopyLoggedMessage{
      .sequence_number = message1.sequence_number,
      .log_time = message1.log_time,
      .transmit_time = message1.logged_transmit_time,
      .header = message1.header,
      .data = message1.data,
      .is_repeated_persistent = message1.is_repeated_persistent,
      .is_lite_compressed = message1.is_lite_compressed,
    }));
  const auto finalize_result = message_writer.finalize();
  REQUIRE(finalize_result);
  const auto& channel_index = finalize_result.value();
  REQUIRE(channel_index.size() == 2U);
  REQUIRE(file_writer_ptr->close());

  REQUIRE(file_reader.open());
  REQUIRE(message_reader.read_chunk(channel_index.at(0U).location, *compressor_ptr, file_reader));
  REQUIRE(tests::check_read_result({message1, message2}, message_reader));
  REQUIRE(message_reader.read_chunk(channel_index.at(1U).location, *compressor_ptr, file_reader));
  REQUIRE(tests::check_read_result({message2, message3}, message_reader));
}

} // namespace
} // namespace clockwork_logging::offboard
