// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/onboard/async_write_request.hh"
#include "clockwork/logging/onboard/async_writer.hh"
#include "clockwork/logging/onboard/tests/support/test_message_handle.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer_state.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/default_memory_resource.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"
#include "jewels/shared_pool/shared_buffer_pool.hh"
#include "jewels/shared_pool/shared_object_pool.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <chrono>
#include <compare>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace clockwork_logging::onboard
{
namespace
{

/// Message buffer size
constexpr size_t message_buffer_size = 4096U;

/// Message buffer alignment
constexpr size_t message_alignment = 4096U;

/// Message buffer type
using MessageHandle =
  TestMessageHandle<jewels::SharedBufferPool<message_buffer_size, message_alignment>::SharedReference>;

struct TestAsyncWriterPolicy
{
  /// Data buffer size
  static constexpr size_t buffer_size = 4096U;

  /// Data buffer alignment
  static constexpr size_t alignment = 4096U;

  /// Maximum number of message handles per write request
  static constexpr size_t max_message_handles = 64U;

  /// Maximum number of buffers per write request
  static constexpr size_t max_buffers = 512U;

  /// Maximum write size in bytes
  static constexpr size_t max_write_size = 2 * jewels::math::constants::bytes_per_mib<size_t>;

  /// I/O ring size
  static constexpr uint32_t io_ring_size = 4U;

  /// Maximum outstanding async operations
  static constexpr size_t max_async_requests = 4U;

  /// Message handle type
  using MessageHandleType = MessageHandle;

  /// Buffer pool type
  using BufferPoolType = jewels::SharedBufferPool<buffer_size, alignment>;

  /// Buffer handle type
  using BufferReferenceType = BufferPoolType::SharedReference;

  /// Async write request handle type
  using AsyncWriteRequestHandleType =
    jewels::SharedObjectPool<AsyncWriteRequest<TestAsyncWriterPolicy>>::SharedReference;
};

using AsyncWriteRequestType = AsyncWriteRequest<TestAsyncWriterPolicy>;

TEST_CASE("Write file asynchronously")
{
  constexpr size_t write_buffer_count = 2U;
  constexpr size_t message_buffer_count = 4U;
  constexpr size_t async_write_request_count = 1U;
  constexpr auto log_file_prefix = "log_file_";

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto log_dir = test_dir.get_path() / log_file_prefix;

  const auto memory_resource = jewels::memory::get_default_memory_resource();
  jewels::SharedBufferPool<TestAsyncWriterPolicy::buffer_size, TestAsyncWriterPolicy::alignment> write_buffer_pool{
    memory_resource, write_buffer_count};
  jewels::SharedBufferPool<message_buffer_size, message_alignment> message_buffer_pool{
    memory_resource, message_buffer_count};
  jewels::SharedObjectPool<AsyncWriteRequest<TestAsyncWriterPolicy>> async_write_request_pool{
    memory_resource, async_write_request_count};

  constexpr jewels::time::SteadyTime time10{std::chrono::seconds(10)};
  AsyncWriter<TestAsyncWriterPolicy> async_writer{memory_resource, memory_resource, WriterEnvironment::normal};
  REQUIRE(async_writer.get_status_string().empty());
  REQUIRE(async_writer.get_state() == WriterState::closed);
  REQUIRE(async_writer.open_log(log_dir.string(), log_file_prefix));
  REQUIRE(async_writer.get_write_backlog(time10).value() == std::chrono::nanoseconds(0));
  REQUIRE(async_writer.get_log_file_offset() == 0U);

  auto write_request_result = async_write_request_pool.make_shared_object();
  REQUIRE(write_request_result);
  auto write_request = std::move(write_request_result).value();

  const jewels::time::SteadyTime time1{std::chrono::seconds(1)};

  auto write_buf_result = write_buffer_pool.get_shared_buffer();
  REQUIRE(write_buf_result);
  REQUIRE(write_request->add_buffer(write_buf_result.value()));

  std::vector<char> message1(message_buffer_size, 'A');
  REQUIRE(
    write_request->copy_data(
      time1, std::as_bytes(std::span{message1.data(), message1.size()}), AsyncWriteRequestType::DataType::message) ==
    message1.size());

  constexpr jewels::time::SteadyTime time2{std::chrono::seconds(2)};
  auto message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message2 = std::move(message_result).value();
  std::memset(message2->data(), 'B', message2->size());
  REQUIRE(
    write_request->zero_copy_data(
      time2, std::as_bytes(std::span{*message2}), AsyncWriteRequestType::DataType::message, MessageHandle{message2}) ==
    message2->size());

  REQUIRE(async_writer.write_async(std::move(write_request)));
  REQUIRE(async_writer.get_write_backlog(time10).value() <= std::chrono::seconds(9));
  REQUIRE(async_writer.get_log_file_offset() == message1.size() + message2->size());
  REQUIRE(async_writer.get_pending_request_count().value() <= 1U);
  REQUIRE(async_writer.get_state() == WriterState::logging);

  while (async_writer.get_pending_request_count().value() != 0U)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    async_writer.periodic_callback();
  }

  REQUIRE(async_writer.pause_logging());
  REQUIRE(async_writer.resume_logging());

  write_request_result = async_write_request_pool.make_shared_object();
  REQUIRE(write_request_result);
  write_request = std::move(write_request_result).value();

  constexpr jewels::time::SteadyTime time3{std::chrono::seconds(3)};
  message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message3 = std::move(message_result).value();
  std::memset(message3->data(), 'C', message3->size());
  REQUIRE(
    write_request->zero_copy_data(
      time3, std::as_bytes(std::span{*message3}), AsyncWriteRequestType::DataType::message, MessageHandle{message3}) ==
    message3->size());

  REQUIRE(async_writer.write_async(std::move(write_request)));
  REQUIRE(async_writer.get_write_backlog(time10).value() <= std::chrono::seconds(7));
  REQUIRE(async_writer.get_log_file_offset() == message3->size());
  REQUIRE(async_writer.get_pending_request_count().value() <= 2U);
  REQUIRE(async_writer.get_state() == WriterState::logging);

  REQUIRE(async_writer.close_log());
  REQUIRE(async_writer.get_state() == WriterState::closed);
  REQUIRE(async_writer.get_pending_request_count().value() <= 3U);

  REQUIRE(async_writer.drain_async_operations());

  SECTION("Reopen existing log and keep writing")
  {
    AsyncWriter<TestAsyncWriterPolicy> async_writer2{memory_resource, memory_resource, WriterEnvironment::normal};
    REQUIRE(async_writer2.get_status_string().empty());
    REQUIRE(async_writer2.get_state() == WriterState::closed);
    REQUIRE(async_writer2.open_log(log_dir.string(), log_file_prefix));
    REQUIRE(async_writer2.get_write_backlog(time10).value() == std::chrono::nanoseconds(0));
    REQUIRE(async_writer2.get_log_file_offset() == 0U);

    write_request_result = async_write_request_pool.make_shared_object();
    REQUIRE(write_request_result);
    write_request = std::move(write_request_result).value();

    constexpr jewels::time::SteadyTime time4{std::chrono::seconds(4)};

    write_buf_result = write_buffer_pool.get_shared_buffer();
    REQUIRE(write_buf_result);
    REQUIRE(write_request->add_buffer(write_buf_result.value()));

    std::vector<char> message4(message_buffer_size, 'D');
    REQUIRE(
      write_request->copy_data(
        time4, std::as_bytes(std::span{message4.data(), message4.size()}), AsyncWriteRequestType::DataType::message) ==
      message4.size());

    constexpr jewels::time::SteadyTime time5{std::chrono::seconds(5)};
    message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message5 = std::move(message_result).value();
    std::memset(message5->data(), 'E', message5->size());
    REQUIRE(
      write_request->zero_copy_data(
        time5,
        std::as_bytes(std::span{*message5}),
        AsyncWriteRequestType::DataType::message,
        MessageHandle{message5}) == message5->size());

    REQUIRE(async_writer2.write_async(std::move(write_request)));
    REQUIRE(async_writer2.get_write_backlog(time10).value() <= std::chrono::seconds(6));
    REQUIRE(async_writer2.get_log_file_offset() == message4.size() + message5->size());
    REQUIRE(async_writer2.get_pending_request_count().value() <= 1U);
    REQUIRE(async_writer2.get_state() == WriterState::logging);

    while (async_writer2.get_pending_request_count().value() != 0U)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      async_writer2.periodic_callback();
    }

    REQUIRE(async_writer2.pause_logging());
    REQUIRE(async_writer2.resume_logging());

    write_request_result = async_write_request_pool.make_shared_object();
    REQUIRE(write_request_result);
    write_request = std::move(write_request_result).value();

    constexpr jewels::time::SteadyTime time6{std::chrono::seconds(6)};
    message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message6 = std::move(message_result).value();
    std::memset(message6->data(), 'F', message6->size());
    REQUIRE(
      write_request->zero_copy_data(
        time6,
        std::as_bytes(std::span{*message6}),
        AsyncWriteRequestType::DataType::message,
        MessageHandle{message6}) == message6->size());

    REQUIRE(async_writer2.write_async(std::move(write_request)));
    REQUIRE(async_writer2.get_write_backlog(time10).value() <= std::chrono::seconds(7));
    REQUIRE(async_writer2.get_log_file_offset() == message6->size());
    REQUIRE(async_writer2.get_pending_request_count().value() <= 2U);
    REQUIRE(async_writer2.get_state() == WriterState::logging);

    REQUIRE(async_writer2.close_log());
    REQUIRE(async_writer2.get_state() == WriterState::closed);
    REQUIRE(async_writer2.get_pending_request_count().value() <= 3U);

    REQUIRE(async_writer2.drain_async_operations());

    auto maybe_file_data =
      tests::try_read_file((test_dir.get_path() / log_file_prefix / "log_file_000002.olog").string());
    REQUIRE(maybe_file_data);
    REQUIRE(maybe_file_data->size() == message1.size() + message2->size());
    REQUIRE(std::memcmp(maybe_file_data->data(), message4.data(), message4.size()) == 0);
    REQUIRE(std::memcmp(&maybe_file_data->at(message4.size()), message5->data(), message5->size()) == 0);

    maybe_file_data = tests::try_read_file((test_dir.get_path() / log_file_prefix / "log_file_000003.olog").string());
    REQUIRE(maybe_file_data);
    REQUIRE(maybe_file_data->size() == message6->size());
    REQUIRE(std::memcmp(maybe_file_data->data(), message6->data(), message6->size()) == 0);
  }

  auto maybe_file_data =
    tests::try_read_file((test_dir.get_path() / log_file_prefix / "log_file_000000.olog").string());
  REQUIRE(maybe_file_data);
  REQUIRE(maybe_file_data->size() == message1.size() + message2->size());
  REQUIRE(std::memcmp(maybe_file_data->data(), message1.data(), message1.size()) == 0);
  REQUIRE(std::memcmp(&maybe_file_data->at(message1.size()), message2->data(), message2->size()) == 0);

  maybe_file_data = tests::try_read_file((test_dir.get_path() / log_file_prefix / "log_file_000001.olog").string());
  REQUIRE(maybe_file_data);
  REQUIRE(maybe_file_data->size() == message3->size());
  REQUIRE(std::memcmp(maybe_file_data->data(), message3->data(), message3->size()) == 0);
}

TEST_CASE("Overrun detected")
{
  constexpr size_t write_buffer_count = 2U;
  constexpr size_t message_buffer_count = 4U;
  constexpr size_t async_write_request_count = 1U;
  constexpr auto log_file_prefix = "log_file_";

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto log_dir = test_dir.get_path() / log_file_prefix;

  const auto memory_resource = jewels::memory::get_default_memory_resource();
  jewels::SharedBufferPool<TestAsyncWriterPolicy::buffer_size, TestAsyncWriterPolicy::alignment> write_buffer_pool{
    memory_resource, write_buffer_count};
  jewels::SharedBufferPool<message_buffer_size, message_alignment> message_buffer_pool{
    memory_resource, message_buffer_count};
  jewels::SharedObjectPool<AsyncWriteRequest<TestAsyncWriterPolicy>> async_write_request_pool{
    memory_resource, async_write_request_count};

  constexpr jewels::time::SteadyTime time10{std::chrono::seconds(10)};
  AsyncWriter<TestAsyncWriterPolicy> async_writer{memory_resource, memory_resource, WriterEnvironment::normal};
  REQUIRE(async_writer.get_status_string().empty());
  REQUIRE(async_writer.get_state() == WriterState::closed);
  REQUIRE(async_writer.open_log(log_dir.string(), log_file_prefix));
  REQUIRE(async_writer.get_write_backlog(time10).value() == std::chrono::nanoseconds(0));
  REQUIRE(async_writer.get_log_file_offset() == 0U);

  auto write_request_result = async_write_request_pool.make_shared_object();
  REQUIRE(write_request_result);
  auto write_request = std::move(write_request_result).value();

  const jewels::time::SteadyTime time1{std::chrono::seconds(1)};

  auto write_buf_result = write_buffer_pool.get_shared_buffer();
  REQUIRE(write_buf_result);
  REQUIRE(write_request->add_buffer(write_buf_result.value()));

  std::vector<char> message1(message_buffer_size, 'A');
  REQUIRE(
    write_request->copy_data(
      time1, std::as_bytes(std::span{message1.data(), message1.size()}), AsyncWriteRequestType::DataType::message) ==
    message1.size());

  constexpr jewels::time::SteadyTime time2{std::chrono::seconds(2)};
  auto message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message2 = std::move(message_result).value();
  std::memset(message2->data(), 'B', message2->size());
  MessageHandle message_handle2{message2};
  message_handle2.set_is_valid(false);
  REQUIRE(
    write_request->zero_copy_data(
      time2,
      std::as_bytes(std::span{*message2}),
      AsyncWriteRequestType::DataType::message,
      std::move(message_handle2)) == message2->size());

  REQUIRE(async_writer.write_async(std::move(write_request)));
  REQUIRE(async_writer.get_write_backlog(time10).value() <= std::chrono::seconds(9));
  REQUIRE(async_writer.get_log_file_offset() == message1.size() + message2->size());
  REQUIRE(async_writer.get_pending_request_count().value() <= 1U);
  REQUIRE(async_writer.get_state() == WriterState::logging);

  while (async_writer.get_pending_request_count().value() != 0U)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    async_writer.periodic_callback();
  }

  REQUIRE(async_writer.close_log());
  REQUIRE(async_writer.get_state() == WriterState::closed);
  REQUIRE(async_writer.get_pending_request_count().value() <= 3U);

  REQUIRE(async_writer.drain_async_operations());

  REQUIRE(async_writer.get_and_reset_drop_count() == 1U);

  auto maybe_file_data =
    tests::try_read_file((test_dir.get_path() / log_file_prefix / "log_file_000000.olog").string());
  REQUIRE(maybe_file_data);
  REQUIRE(maybe_file_data->size() == message1.size() + message2->size());
  REQUIRE(std::memcmp(maybe_file_data->data(), message1.data(), message1.size()) == 0);
  REQUIRE(std::memcmp(&maybe_file_data->at(message1.size()), message2->data(), message2->size()) == 0);
}

TEST_CASE("Pause/Resume")
{
  constexpr size_t write_buffer_count = 2U;
  constexpr size_t message_buffer_count = 4U;
  constexpr size_t async_write_request_count = 1U;
  constexpr auto log_file_prefix = "log_file_";

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto log_dir = test_dir.get_path() / log_file_prefix;

  const auto memory_resource = jewels::memory::get_default_memory_resource();
  jewels::SharedBufferPool<TestAsyncWriterPolicy::buffer_size, TestAsyncWriterPolicy::alignment> write_buffer_pool{
    memory_resource, write_buffer_count};
  jewels::SharedBufferPool<message_buffer_size, message_alignment> message_buffer_pool{
    memory_resource, message_buffer_count};
  jewels::SharedObjectPool<AsyncWriteRequest<TestAsyncWriterPolicy>> async_write_request_pool{
    memory_resource, async_write_request_count};

  constexpr jewels::time::SteadyTime time10{std::chrono::seconds(10)};
  AsyncWriter<TestAsyncWriterPolicy> async_writer{memory_resource, memory_resource, WriterEnvironment::normal};
  REQUIRE(async_writer.get_status_string().empty());
  REQUIRE(async_writer.get_state() == WriterState::closed);
  REQUIRE(async_writer.open_log_paused(log_dir.string(), log_file_prefix));
  REQUIRE(async_writer.get_state() == WriterState::paused);
  REQUIRE(async_writer.resume_logging());
  REQUIRE(async_writer.get_state() == WriterState::logging);
  REQUIRE(async_writer.get_write_backlog(time10).value() == std::chrono::nanoseconds(0));
  REQUIRE(async_writer.get_log_file_offset() == 0U);

  auto write_request_result = async_write_request_pool.make_shared_object();
  REQUIRE(write_request_result);
  auto write_request = std::move(write_request_result).value();

  const jewels::time::SteadyTime time1{std::chrono::seconds(1)};

  auto write_buf_result = write_buffer_pool.get_shared_buffer();
  REQUIRE(write_buf_result);
  REQUIRE(write_request->add_buffer(write_buf_result.value()));

  std::vector<char> message1(message_buffer_size, 'A');
  REQUIRE(
    write_request->copy_data(
      time1, std::as_bytes(std::span{message1.data(), message1.size()}), AsyncWriteRequestType::DataType::message) ==
    message1.size());

  constexpr jewels::time::SteadyTime time2{std::chrono::seconds(2)};
  auto message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message2 = std::move(message_result).value();
  std::memset(message2->data(), 'B', message2->size());
  REQUIRE(
    write_request->zero_copy_data(
      time2, std::as_bytes(std::span{*message2}), AsyncWriteRequestType::DataType::message, MessageHandle{message2}) ==
    message2->size());

  REQUIRE(async_writer.write_async(std::move(write_request)));
  REQUIRE(async_writer.get_write_backlog(time10).value() <= std::chrono::seconds(9));
  REQUIRE(async_writer.get_log_file_offset() == message1.size() + message2->size());
  REQUIRE(async_writer.get_pending_request_count().value() <= 1U);
  REQUIRE(async_writer.get_state() == WriterState::logging);

  while (async_writer.get_pending_request_count().value() != 0U)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    async_writer.periodic_callback();
  }

  REQUIRE(async_writer.pause_logging());
  REQUIRE(async_writer.resume_logging());

  write_request_result = async_write_request_pool.make_shared_object();
  REQUIRE(write_request_result);
  write_request = std::move(write_request_result).value();

  constexpr jewels::time::SteadyTime time3{std::chrono::seconds(3)};
  message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message3 = std::move(message_result).value();
  std::memset(message3->data(), 'C', message3->size());
  REQUIRE(
    write_request->zero_copy_data(
      time3, std::as_bytes(std::span{*message3}), AsyncWriteRequestType::DataType::message, MessageHandle{message3}) ==
    message3->size());

  REQUIRE(async_writer.write_async(std::move(write_request)));
  REQUIRE(async_writer.get_write_backlog(time10).value() <= std::chrono::seconds(7));
  REQUIRE(async_writer.get_log_file_offset() == message3->size());
  REQUIRE(async_writer.get_pending_request_count().value() <= 2U);
  REQUIRE(async_writer.get_state() == WriterState::logging);

  REQUIRE(async_writer.pause_logging());
  REQUIRE(async_writer.get_state() == WriterState::paused);
  REQUIRE(async_writer.get_pending_request_count().value() <= 3U);
  REQUIRE(async_writer.resume_logging());
  REQUIRE(async_writer.get_state() == WriterState::logging);

  REQUIRE(async_writer.drain_async_operations());

  write_request_result = async_write_request_pool.make_shared_object();
  REQUIRE(write_request_result);
  write_request = std::move(write_request_result).value();

  constexpr jewels::time::SteadyTime time4{std::chrono::seconds(4)};

  write_buf_result = write_buffer_pool.get_shared_buffer();
  REQUIRE(write_buf_result);
  REQUIRE(write_request->add_buffer(write_buf_result.value()));

  std::vector<char> message4(message_buffer_size, 'D');
  REQUIRE(
    write_request->copy_data(
      time4, std::as_bytes(std::span{message4.data(), message4.size()}), AsyncWriteRequestType::DataType::message) ==
    message4.size());

  constexpr jewels::time::SteadyTime time5{std::chrono::seconds(5)};
  message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message5 = std::move(message_result).value();
  std::memset(message5->data(), 'E', message5->size());
  REQUIRE(
    write_request->zero_copy_data(
      time5, std::as_bytes(std::span{*message5}), AsyncWriteRequestType::DataType::message, MessageHandle{message5}) ==
    message5->size());

  REQUIRE(async_writer.write_async(std::move(write_request)));
  REQUIRE(async_writer.get_write_backlog(time10).value() <= std::chrono::seconds(6));
  REQUIRE(async_writer.get_log_file_offset() == message4.size() + message5->size());
  REQUIRE(async_writer.get_pending_request_count().value() <= 1U);
  REQUIRE(async_writer.get_state() == WriterState::logging);

  while (async_writer.get_pending_request_count().value() != 0U)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    async_writer.periodic_callback();
  }

  REQUIRE(async_writer.pause_logging());
  REQUIRE(async_writer.resume_logging());

  write_request_result = async_write_request_pool.make_shared_object();
  REQUIRE(write_request_result);
  write_request = std::move(write_request_result).value();

  constexpr jewels::time::SteadyTime time6{std::chrono::seconds(6)};
  message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message6 = std::move(message_result).value();
  std::memset(message6->data(), 'F', message6->size());
  REQUIRE(
    write_request->zero_copy_data(
      time6, std::as_bytes(std::span{*message6}), AsyncWriteRequestType::DataType::message, MessageHandle{message6}) ==
    message6->size());

  REQUIRE(async_writer.write_async(std::move(write_request)));
  REQUIRE(async_writer.get_write_backlog(time10).value() <= std::chrono::seconds(7));
  REQUIRE(async_writer.get_log_file_offset() == message6->size());
  REQUIRE(async_writer.get_pending_request_count().value() <= 2U);
  REQUIRE(async_writer.get_state() == WriterState::logging);

  REQUIRE(async_writer.pause_logging());
  REQUIRE(async_writer.get_state() == WriterState::paused);
  REQUIRE(async_writer.close_log());
  REQUIRE(async_writer.get_state() == WriterState::closed);
  REQUIRE(async_writer.get_pending_request_count().value() <= 3U);

  REQUIRE(async_writer.drain_async_operations());

  auto maybe_file_data =
    tests::try_read_file((test_dir.get_path() / log_file_prefix / "log_file_000000.olog").string());
  REQUIRE(maybe_file_data);
  REQUIRE(maybe_file_data->size() == message1.size() + message2->size());
  REQUIRE(std::memcmp(maybe_file_data->data(), message1.data(), message1.size()) == 0);
  REQUIRE(std::memcmp(&maybe_file_data->at(message1.size()), message2->data(), message2->size()) == 0);

  maybe_file_data = tests::try_read_file((test_dir.get_path() / log_file_prefix / "log_file_000001.olog").string());
  REQUIRE(maybe_file_data);
  REQUIRE(maybe_file_data->size() == message3->size());
  REQUIRE(std::memcmp(maybe_file_data->data(), message3->data(), message3->size()) == 0);

  maybe_file_data = tests::try_read_file((test_dir.get_path() / log_file_prefix / "log_file_000002.olog").string());
  REQUIRE(maybe_file_data);
  REQUIRE(maybe_file_data->size() == message1.size() + message2->size());
  REQUIRE(std::memcmp(maybe_file_data->data(), message4.data(), message4.size()) == 0);
  REQUIRE(std::memcmp(&maybe_file_data->at(message4.size()), message5->data(), message5->size()) == 0);

  maybe_file_data = tests::try_read_file((test_dir.get_path() / log_file_prefix / "log_file_000003.olog").string());
  REQUIRE(maybe_file_data);
  REQUIRE(maybe_file_data->size() == message6->size());
  REQUIRE(std::memcmp(maybe_file_data->data(), message6->data(), message6->size()) == 0);
}

TEST_CASE("Error handling")
{
  constexpr size_t message_buffer_count = 1U;
  constexpr size_t async_write_request_count = 1U;
  constexpr auto log_file_prefix = "log_file_";
  constexpr jewels::time::SteadyTime time1{std::chrono::seconds(1)};

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto log_dir = test_dir.get_path() / log_file_prefix;

  const auto memory_resource = jewels::memory::get_default_memory_resource();
  jewels::SharedBufferPool<message_buffer_size, message_alignment> message_buffer_pool{
    memory_resource, message_buffer_count};
  jewels::SharedObjectPool<AsyncWriteRequest<TestAsyncWriterPolicy>> async_write_request_pool{
    memory_resource, async_write_request_count};

  SECTION("Log not open")
  {
    AsyncWriter<TestAsyncWriterPolicy> async_writer{memory_resource, memory_resource, WriterEnvironment::normal};
    REQUIRE(async_writer.close_log() == jewels::unexpected(LogError::not_open));
    REQUIRE(async_writer.pause_logging() == jewels::unexpected(LogError::not_open));
    REQUIRE(async_writer.resume_logging() == jewels::unexpected(LogError::not_paused));
    auto write_request_result = async_write_request_pool.make_shared_object();
    REQUIRE(write_request_result);
    REQUIRE(
      async_writer.write_async(std::move(write_request_result).value()) == jewels::unexpected(LogError::not_open));
    REQUIRE(async_writer.get_write_backlog(time1) == jewels::unexpected(LogError::not_open));
    REQUIRE(async_writer.get_log_file_offset() == jewels::unexpected(LogError::not_open));
  }

  SECTION("Log file creation fails")
  {
    AsyncWriter<TestAsyncWriterPolicy> async_writer{memory_resource, memory_resource, WriterEnvironment::normal};
    REQUIRE(async_writer.open_log(log_dir.string(), log_file_prefix));
    const jewels::filesystem::Filesystem vfs{memory_resource};
    REQUIRE(vfs.rename(log_dir, test_dir.get_path() / "XXXX"));
    REQUIRE(async_writer.pause_logging());
    REQUIRE(async_writer.resume_logging() == jewels::unexpected(LogError::failed_to_open_log_file));
    REQUIRE(async_writer.get_state() == WriterState::failed);
    REQUIRE_THAT(
      std::string{async_writer.get_status_string()}, Catch::Matchers::StartsWith("Failed to open log file "));
    REQUIRE(async_writer.drain_async_operations() == jewels::unexpected(LogError::failed_to_open_log_file));
  }

  SECTION("Already open")
  {
    AsyncWriter<TestAsyncWriterPolicy> async_writer{memory_resource, memory_resource, WriterEnvironment::normal};
    REQUIRE(async_writer.open_log(log_dir.string(), log_file_prefix));
    REQUIRE(async_writer.open_log(log_dir.string(), log_file_prefix) == jewels::unexpected(LogError::already_open));
  }

  SECTION("Fail to get handle for async close")
  {
    AsyncWriter<TestAsyncWriterPolicy> async_writer{memory_resource, memory_resource, WriterEnvironment::normal};
    REQUIRE(async_writer.open_log(log_dir.string(), log_file_prefix));
    for (size_t i = 0U; i < TestAsyncWriterPolicy::max_async_requests; ++i)
    {
      REQUIRE(async_writer.pause_logging());
      REQUIRE(async_writer.resume_logging());
    }
    REQUIRE(async_writer.pause_logging() == jewels::unexpected(LogError::failed_to_allocate_async_request));
    REQUIRE(async_writer.get_state() == WriterState::failed);
    REQUIRE(async_writer.get_status_string() == "Failed to get async request from pool for async close");
    REQUIRE(async_writer.drain_async_operations() == jewels::unexpected(LogError::failed_to_allocate_async_request));
  }

  SECTION("Fail to queue async write")
  {
    AsyncWriter<TestAsyncWriterPolicy> async_writer{memory_resource, memory_resource, WriterEnvironment::normal};
    REQUIRE(async_writer.open_log(log_dir.string(), log_file_prefix));

    auto write_request_result = async_write_request_pool.make_shared_object();
    REQUIRE(write_request_result);
    auto write_request = std::move(write_request_result).value();

    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message = std::move(message_result).value();
    std::memset(message->data(), 'A', message->size());
    REQUIRE(
      write_request->zero_copy_data(
        time1, std::as_bytes(std::span{*message}), AsyncWriteRequestType::DataType::message, MessageHandle{message}) ==
      message->size());
    REQUIRE(async_writer.write_async(write_request));
    REQUIRE(async_writer.write_async(write_request));
    REQUIRE(async_writer.write_async(write_request));
    REQUIRE(async_writer.write_async(write_request));
    REQUIRE(async_writer.write_async(write_request) == jewels::unexpected(LogError::failed_to_queue_async_write));
    REQUIRE(async_writer.get_state() == WriterState::failed);
    REQUIRE(async_writer.get_status_string() == "Failed to add async write to list of pending writes");
    REQUIRE(async_writer.drain_async_operations() == jewels::unexpected(LogError::failed_to_queue_async_write));
  }
}

} // namespace
} // namespace clockwork_logging::onboard
