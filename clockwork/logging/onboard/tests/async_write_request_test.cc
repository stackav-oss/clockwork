// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/onboard/async_write_request.hh"
#include "clockwork/logging/onboard/tests/support/test_message_handle.hh"
#include "jewels/aligner/aligner.hh"
#include "jewels/container/at.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/shared_buffer_pool.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <optional>
#include <span>
#include <sys/uio.h>
#include <utility>
#include <vector>

namespace clockwork_logging::onboard
{
namespace
{

/// Message buffer size
constexpr size_t message_buffer_size = 384U;

/// Message buffer alignment
constexpr size_t message_alignment = 16U;

/// Message handle type
using MessageHandle =
  TestMessageHandle<jewels::SharedBufferPool<message_buffer_size, message_alignment>::SharedReference>;

struct TestAsyncWriterPolicy
{
  /// Data buffer size
  static constexpr size_t buffer_size = 128U;

  /// Data buffer alignment
  static constexpr size_t alignment = 16U;

  /// Maximum number of message handles per write request
  static constexpr size_t max_message_handles = 4U;

  /// Maximum number of buffers per write request
  static constexpr size_t max_buffers = 4U;

  /// Maximum write size in bytes
  static constexpr size_t max_write_size = 992U;

  /// Message handle type
  using MessageHandleType = MessageHandle;

  /// Buffer pool type
  using BufferPoolType = jewels::SharedBufferPool<buffer_size, alignment>;

  /// Buffer handle type
  using BufferReferenceType = BufferPoolType::SharedReference;
};

using AsyncWriteRequestType = AsyncWriteRequest<TestAsyncWriterPolicy>;

TEST_CASE("Smoke test")
{
  constexpr size_t write_buffer_count = 16U;
  constexpr size_t message_buffer_count = 16U;

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  jewels::SharedBufferPool<TestAsyncWriterPolicy::buffer_size, TestAsyncWriterPolicy::alignment> write_buffer_pool{
    memory_resource, write_buffer_count};
  jewels::SharedBufferPool<message_buffer_size, message_alignment> message_buffer_pool{
    memory_resource, message_buffer_count};

  const jewels::time::SteadyTime time10{std::chrono::seconds(10)};

  AsyncWriteRequestType write_request;
  REQUIRE_FALSE(write_request.is_full());
  REQUIRE(write_request.get_write_size() == 0U);
  REQUIRE(write_request.get_io_vector().empty());
  REQUIRE_FALSE(write_request.try_get_oldest_data_timestamp());

  SECTION("Copy data into write request")
  {
    constexpr size_t message_size = 77U;

    const jewels::time::SteadyTime time2{std::chrono::seconds(2)};
    const std::vector<char> message1(message_size, 'A');
    REQUIRE(
      write_request.copy_data(time2, std::as_bytes(std::span{message1}), AsyncWriteRequestType::DataType::message) ==
      0U);
    REQUIRE(write_request.get_message_data_size() == 0U);

    const auto write_buf1_result = write_buffer_pool.get_shared_buffer();
    REQUIRE(write_buf1_result);
    REQUIRE(write_request.add_buffer(write_buf1_result.value()));
    REQUIRE(write_buf1_result.value().get_reference_count() == 2U);
    std::vector<std::byte> expected_buf1(TestAsyncWriterPolicy::buffer_size);

    REQUIRE(
      write_request.copy_data(time2, std::as_bytes(std::span{message1}), AsyncWriteRequestType::DataType::message) ==
      message1.size());
    REQUIRE(write_request.try_get_oldest_data_timestamp() == time2);
    REQUIRE(write_request.get_message_data_size() == message_size);
    std::memcpy(expected_buf1.data(), message1.data(), message1.size());

    const jewels::time::SteadyTime time1{std::chrono::seconds(1)};
    const std::vector<char> message2(message_size, 'B');
    const size_t msg2_split_offset = 51U;
    REQUIRE(
      write_request.copy_data(time1, std::as_bytes(std::span{message2}), AsyncWriteRequestType::DataType::message) ==
      msg2_split_offset);
    REQUIRE(write_request.try_get_oldest_data_timestamp() == time1);
    REQUIRE(write_request.get_message_data_size() == message_size + msg2_split_offset);
    std::memcpy(&expected_buf1.at(message1.size()), message2.data(), msg2_split_offset);

    const auto write_buf2_result = write_buffer_pool.get_shared_buffer();
    REQUIRE(write_buf2_result);
    REQUIRE(write_request.add_buffer(write_buf2_result.value()));
    REQUIRE(write_buf2_result.value().get_reference_count() == 2U);
    const auto buf2_pad_size = jewels::Aligner<TestAsyncWriterPolicy::alignment>::aligned_remainder(message_size * 2U);
    std::vector<std::byte> expected_buf2(message_size - msg2_split_offset + buf2_pad_size);

    REQUIRE(
      write_request.copy_data(
        time1,
        std::as_bytes(std::span{message2}.subspan(msg2_split_offset)),
        AsyncWriteRequestType::DataType::message) == message2.size() - msg2_split_offset);
    std::memcpy(expected_buf2.data(), &message2.at(msg2_split_offset), message2.size() - msg2_split_offset);
    REQUIRE(write_request.get_write_size() == message1.size() + message2.size());
    REQUIRE(write_request.get_message_data_size() == message_size * 2U);

    const auto io_vector = write_request.get_io_vector();
    std::memset(&expected_buf2.at(message2.size() - msg2_split_offset), 0, buf2_pad_size);
    REQUIRE(write_request.get_write_size() == message1.size() + message2.size() + buf2_pad_size);
    REQUIRE(io_vector.size() == 2U);

    REQUIRE(jewels::at(io_vector, 0).iov_len == expected_buf1.size());
    REQUIRE(std::memcmp(expected_buf1.data(), jewels::at(io_vector, 0).iov_base, expected_buf1.size()) == 0);
    REQUIRE(jewels::at(io_vector, 1).iov_len == expected_buf2.size());
    REQUIRE(std::memcmp(expected_buf2.data(), jewels::at(io_vector, 1).iov_base, expected_buf2.size()) == 0);
  }

  SECTION("Copy metadata into write request")
  {
    constexpr size_t message_size = 77U;

    const jewels::time::SteadyTime time2{std::chrono::seconds(2)};
    const std::vector<char> message1(message_size, 'A');
    REQUIRE(
      write_request.copy_data(time2, std::as_bytes(std::span{message1}), AsyncWriteRequestType::DataType::metadata) ==
      0U);
    REQUIRE(write_request.get_message_data_size() == 0U);

    const auto write_buf1_result = write_buffer_pool.get_shared_buffer();
    REQUIRE(write_buf1_result);
    REQUIRE(write_request.add_buffer(write_buf1_result.value()));
    REQUIRE(write_buf1_result.value().get_reference_count() == 2U);
    std::vector<std::byte> expected_buf1(TestAsyncWriterPolicy::buffer_size);

    REQUIRE(
      write_request.copy_data(time2, std::as_bytes(std::span{message1}), AsyncWriteRequestType::DataType::metadata) ==
      message1.size());
    REQUIRE(write_request.try_get_oldest_data_timestamp() == time2);
    REQUIRE(write_request.get_message_data_size() == 0U);
    std::memcpy(expected_buf1.data(), message1.data(), message1.size());

    const jewels::time::SteadyTime time1{std::chrono::seconds(1)};
    const std::vector<char> message2(message_size, 'B');
    const size_t msg2_split_offset = 51U;
    REQUIRE(
      write_request.copy_data(time1, std::as_bytes(std::span{message2}), AsyncWriteRequestType::DataType::metadata) ==
      msg2_split_offset);
    REQUIRE(write_request.try_get_oldest_data_timestamp() == time1);
    REQUIRE(write_request.get_message_data_size() == 0U);
    std::memcpy(&expected_buf1.at(message1.size()), message2.data(), msg2_split_offset);

    const auto write_buf2_result = write_buffer_pool.get_shared_buffer();
    REQUIRE(write_buf2_result);
    REQUIRE(write_request.add_buffer(write_buf2_result.value()));
    REQUIRE(write_buf2_result.value().get_reference_count() == 2U);
    const auto buf2_pad_size = jewels::Aligner<TestAsyncWriterPolicy::alignment>::aligned_remainder(message_size * 2U);
    std::vector<std::byte> expected_buf2(message_size - msg2_split_offset + buf2_pad_size);

    REQUIRE(
      write_request.copy_data(
        time1,
        std::as_bytes(std::span{message2}.subspan(msg2_split_offset)),
        AsyncWriteRequestType::DataType::metadata) == message2.size() - msg2_split_offset);
    std::memcpy(expected_buf2.data(), &message2.at(msg2_split_offset), message2.size() - msg2_split_offset);
    REQUIRE(write_request.get_write_size() == message1.size() + message2.size());
    REQUIRE(write_request.get_message_data_size() == 0U);

    const auto io_vector = write_request.get_io_vector();
    std::memset(&expected_buf2.at(message2.size() - msg2_split_offset), 0, buf2_pad_size);
    REQUIRE(write_request.get_write_size() == message1.size() + message2.size() + buf2_pad_size);
    REQUIRE(io_vector.size() == 2U);

    REQUIRE(jewels::at(io_vector, 0).iov_len == expected_buf1.size());
    REQUIRE(std::memcmp(expected_buf1.data(), jewels::at(io_vector, 0).iov_base, expected_buf1.size()) == 0);
    REQUIRE(jewels::at(io_vector, 1).iov_len == expected_buf2.size());
    REQUIRE(std::memcmp(expected_buf2.data(), jewels::at(io_vector, 1).iov_base, expected_buf2.size()) == 0);
  }

  SECTION("Copy data into write request until it is full")
  {
    constexpr size_t message_size = 177U;

    const jewels::time::SteadyTime time2{std::chrono::seconds(2)};
    const std::vector<char> message1(message_size, 'A');
    REQUIRE(
      write_request.copy_data(time2, std::as_bytes(std::span{message1}), AsyncWriteRequestType::DataType::message) ==
      0U);

    auto write_buf_result = write_buffer_pool.get_shared_buffer();
    REQUIRE(write_buf_result);
    REQUIRE(write_request.add_buffer(std::move(write_buf_result).value()));
    std::vector<std::byte> expected_buf1(TestAsyncWriterPolicy::buffer_size);

    const auto msg1_split_offset = TestAsyncWriterPolicy::buffer_size;
    const auto msg1_remainder = message_size - msg1_split_offset;
    REQUIRE(
      write_request.copy_data(time2, std::as_bytes(std::span{message1}), AsyncWriteRequestType::DataType::message) ==
      msg1_split_offset);
    REQUIRE(write_request.try_get_oldest_data_timestamp() == time2);
    std::memcpy(expected_buf1.data(), message1.data(), msg1_split_offset);

    write_buf_result = write_buffer_pool.get_shared_buffer();
    REQUIRE(write_buf_result);
    REQUIRE(write_request.add_buffer(std::move(write_buf_result).value()));
    std::vector<std::byte> expected_buf2(TestAsyncWriterPolicy::buffer_size);

    REQUIRE(
      write_request.copy_data(
        time2,
        std::as_bytes(std::span{message1}.subspan(msg1_split_offset)),
        AsyncWriteRequestType::DataType::message) == msg1_remainder);
    std::memcpy(expected_buf2.data(), &message1.at(msg1_split_offset), msg1_remainder);

    const jewels::time::SteadyTime time1{std::chrono::seconds(1)};
    const std::vector<char> message2(message_size, 'B');
    const size_t msg2_split_offset = 79U;
    const size_t msg2_remainder = message_size - msg2_split_offset;
    REQUIRE(
      write_request.copy_data(
        time1, std::as_bytes(std::span{message2.data(), message2.size()}), AsyncWriteRequestType::DataType::message) ==
      msg2_split_offset);
    REQUIRE(write_request.try_get_oldest_data_timestamp() == time1);
    std::memcpy(&expected_buf2.at(msg1_remainder), message2.data(), msg2_split_offset);

    write_buf_result = write_buffer_pool.get_shared_buffer();
    REQUIRE(write_buf_result);
    REQUIRE(write_request.add_buffer(std::move(write_buf_result).value()));
    std::vector<std::byte> expected_buf3(TestAsyncWriterPolicy::buffer_size);

    REQUIRE(
      write_request.copy_data(
        time1,
        std::as_bytes(std::span{&message2.at(msg2_split_offset), msg2_remainder}),
        AsyncWriteRequestType::DataType::message) == msg2_remainder);
    std::memcpy(expected_buf3.data(), &message2.at(msg2_split_offset), msg2_remainder);

    const std::vector<char> message3(message_size, 'C');
    const size_t msg3_split_offset = 30U;
    const size_t msg3_remainder = message_size - msg3_split_offset;
    REQUIRE(
      write_request.copy_data(
        time1, std::as_bytes(std::span{message3.data(), message3.size()}), AsyncWriteRequestType::DataType::message) ==
      msg3_split_offset);
    std::memcpy(&expected_buf3.at(msg2_remainder), message3.data(), msg3_split_offset);

    write_buf_result = write_buffer_pool.get_shared_buffer();
    REQUIRE(write_buf_result);
    REQUIRE(write_request.add_buffer(std::move(write_buf_result).value()));
    std::vector<std::byte> expected_buf4(TestAsyncWriterPolicy::buffer_size);

    REQUIRE(
      write_request.copy_data(
        time1,
        std::as_bytes(std::span{&message3.at(msg3_split_offset), msg3_remainder}),
        AsyncWriteRequestType::DataType::message) == TestAsyncWriterPolicy::buffer_size);
    std::memcpy(expected_buf4.data(), &message3.at(msg3_split_offset), TestAsyncWriterPolicy::buffer_size);

    REQUIRE(write_request.is_full());
    REQUIRE(
      write_request.copy_data(
        time1,
        std::as_bytes(std::span{&message3.at(msg3_split_offset), msg3_remainder}),
        AsyncWriteRequestType::DataType::message) == 0U);
    REQUIRE(
      write_request.get_write_size() ==
      message1.size() + message2.size() + msg3_split_offset + TestAsyncWriterPolicy::buffer_size);

    const auto io_vector = write_request.get_io_vector();
    REQUIRE(io_vector.size() == 4U);
    REQUIRE(jewels::at(io_vector, 0).iov_len == expected_buf1.size());
    REQUIRE(std::memcmp(expected_buf1.data(), jewels::at(io_vector, 0).iov_base, expected_buf1.size()) == 0);
    REQUIRE(jewels::at(io_vector, 1).iov_len == expected_buf2.size());
    REQUIRE(std::memcmp(expected_buf2.data(), jewels::at(io_vector, 1).iov_base, expected_buf2.size()) == 0);
    REQUIRE(jewels::at(io_vector, 2).iov_len == expected_buf3.size());
    REQUIRE(std::memcmp(expected_buf3.data(), jewels::at(io_vector, 2).iov_base, expected_buf3.size()) == 0);
    REQUIRE(jewels::at(io_vector, 3).iov_len == expected_buf4.size());
    REQUIRE(std::memcmp(expected_buf4.data(), jewels::at(io_vector, 3).iov_base, expected_buf4.size()) == 0);
  }

  SECTION("Zero Copy data into write request")
  {
    constexpr size_t message1_size = 7U;
    constexpr size_t pre_align2_size = 11U;
    constexpr size_t align2_pad_size =
      jewels::Aligner<TestAsyncWriterPolicy::alignment>::aligned_remainder(message1_size + pre_align2_size);

    auto write_buf_result = write_buffer_pool.get_shared_buffer();
    REQUIRE(write_buf_result);
    REQUIRE(write_request.add_buffer(std::move(write_buf_result).value()));
    std::vector<std::byte> expected_buf1(message1_size + pre_align2_size + align2_pad_size);

    const jewels::time::SteadyTime time2{std::chrono::seconds(2)};
    const std::vector<char> message1(message1_size, 'A');
    REQUIRE(
      write_request.copy_data(
        time2, std::as_bytes(std::span{message1.data(), message1.size()}), AsyncWriteRequestType::DataType::message) ==
      message1.size());
    REQUIRE(write_request.try_get_oldest_data_timestamp() == time2);
    std::memcpy(expected_buf1.data(), message1.data(), message1.size());

    const std::vector<char> pre_aligned2(pre_align2_size, 'B');
    REQUIRE(write_request.pad_for_alignment(pre_align2_size));
    std::memset(&expected_buf1.at(message1_size), 0, align2_pad_size);

    const jewels::time::SteadyTime time1{std::chrono::seconds(1)};
    REQUIRE(
      write_request.copy_data(
        time1,
        std::as_bytes(std::span{pre_aligned2.data(), pre_align2_size}),
        AsyncWriteRequestType::DataType::message) == pre_align2_size);
    REQUIRE(write_request.try_get_oldest_data_timestamp() == time1);
    std::memcpy(&expected_buf1.at(message1_size + align2_pad_size), pre_aligned2.data(), pre_align2_size);

    auto msg2_aligned_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(msg2_aligned_result);
    auto msg2_aligned = std::move(msg2_aligned_result).value();
    std::memset(msg2_aligned->data(), 'C', msg2_aligned->size());
    REQUIRE(
      write_request.zero_copy_data(
        time1,
        std::as_bytes(std::span{*msg2_aligned}),
        AsyncWriteRequestType::DataType::message,
        MessageHandle{msg2_aligned}) == msg2_aligned->size());
    REQUIRE(msg2_aligned.get_reference_count() == 2U);
    REQUIRE(write_request.get_write_size() == expected_buf1.size() + msg2_aligned->size());

    std::vector<std::byte> expected_buf2(TestAsyncWriterPolicy::buffer_size - expected_buf1.size());
    constexpr size_t post_align2_size = 11U;
    const std::vector<char> post_aligned2(post_align2_size, 'D');
    REQUIRE(
      write_request.copy_data(
        time1,
        std::as_bytes(std::span{post_aligned2.data(), post_aligned2.size()}),
        AsyncWriteRequestType::DataType::message) == 11U);
    std::memcpy(expected_buf2.data(), post_aligned2.data(), post_align2_size);

    constexpr size_t pre_align3_size = 166U;
    constexpr auto pad3_size =
      jewels::Aligner<TestAsyncWriterPolicy::alignment>::aligned_remainder(post_align2_size + pre_align3_size);
    const std::vector<char> pre_aligned3(pre_align3_size, 'E');
    REQUIRE(write_request.pad_for_alignment(pre_align3_size));
    std::memset(&expected_buf2.at(post_align2_size), 0, pad3_size);

    const auto pre_align3_split = expected_buf2.size() - post_align2_size - pad3_size;
    REQUIRE(
      write_request.copy_data(
        time1,
        std::as_bytes(std::span{pre_aligned3.data(), pre_aligned3.size()}),
        AsyncWriteRequestType::DataType::message) == pre_align3_split);
    std::memcpy(&expected_buf2.at(post_align2_size + pad3_size), pre_aligned3.data(), pre_align3_split);

    write_buf_result = write_buffer_pool.get_shared_buffer();
    REQUIRE(write_buf_result);
    REQUIRE(write_request.add_buffer(std::move(write_buf_result).value()));

    REQUIRE(
      write_request.copy_data(
        time1,
        std::as_bytes(std::span{&pre_aligned3.at(pre_align3_split), pre_aligned3.size() - pre_align3_split}),
        AsyncWriteRequestType::DataType::message) == pre_aligned3.size() - pre_align3_split);
    std::vector<std::byte> expected_buf3(pre_aligned3.size() - pre_align3_split);
    std::memcpy(expected_buf3.data(), &pre_aligned3.at(pre_align3_split), pre_aligned3.size() - pre_align3_split);

    auto msg3_aligned_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(msg3_aligned_result);
    auto msg3_aligned = std::move(msg3_aligned_result).value();
    std::memset(msg3_aligned->data(), 'D', msg3_aligned->size());
    REQUIRE(
      write_request.zero_copy_data(
        time1,
        std::as_bytes(std::span{*msg3_aligned}),
        AsyncWriteRequestType::DataType::message,
        MessageHandle{msg3_aligned}) == msg3_aligned->size());

    std::vector<std::byte> expected_buf4(message_buffer_size);
    std::memcpy(expected_buf4.data(), msg3_aligned->data(), message_buffer_size);

    REQUIRE(write_request.get_write_size() == TestAsyncWriterPolicy::max_write_size);
    REQUIRE(write_request.is_full());

    SECTION("Adding buffer fails, request is full")
    {
      write_buf_result = write_buffer_pool.get_shared_buffer();
      REQUIRE(write_buf_result);
      REQUIRE_FALSE(write_request.add_buffer(std::move(write_buf_result).value()));
      REQUIRE(write_request.is_full());
    }

    SECTION("Zero copy fails, request is full")
    {
      auto msg4_aligned_result = message_buffer_pool.get_shared_buffer();
      REQUIRE(msg4_aligned_result);
      auto msg4_aligned = std::move(msg4_aligned_result).value();
      REQUIRE(
        write_request.zero_copy_data(
          time1,
          std::as_bytes(std::span{*msg4_aligned}),
          AsyncWriteRequestType::DataType::message,
          MessageHandle{msg4_aligned}) == 0U);
      REQUIRE(write_request.is_full());
    }

    const auto io_vector = write_request.get_io_vector();
    REQUIRE(io_vector.size() == 5U);
    REQUIRE(jewels::at(io_vector, 0).iov_len == expected_buf1.size());
    REQUIRE(std::memcmp(expected_buf1.data(), jewels::at(io_vector, 0).iov_base, expected_buf1.size()) == 0);
    REQUIRE(jewels::at(io_vector, 1).iov_len == msg2_aligned->size());
    REQUIRE(std::memcmp(msg2_aligned->data(), jewels::at(io_vector, 1).iov_base, msg2_aligned->size()) == 0);
    REQUIRE(jewels::at(io_vector, 2).iov_len == expected_buf2.size());
    REQUIRE(std::memcmp(expected_buf2.data(), jewels::at(io_vector, 2).iov_base, expected_buf2.size()) == 0);
    REQUIRE(jewels::at(io_vector, 3).iov_len == expected_buf3.size());
    REQUIRE(std::memcmp(expected_buf3.data(), jewels::at(io_vector, 3).iov_base, expected_buf3.size()) == 0);
    REQUIRE(jewels::at(io_vector, 4).iov_len == expected_buf4.size());
    REQUIRE(std::memcmp(expected_buf4.data(), jewels::at(io_vector, 4).iov_base, expected_buf4.size()) == 0);
  }

  SECTION("Zero copy data until the request is full")
  {
    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message1 = std::move(message_result).value();
    std::memset(message1->data(), 'A', message1->size());
    REQUIRE(
      write_request.zero_copy_data(
        time10,
        std::as_bytes(std::span{*message1}),
        AsyncWriteRequestType::DataType::message,
        MessageHandle{message1}) == message1->size());

    message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message2 = std::move(message_result).value();
    std::memset(message2->data(), 'B', message2->size());
    REQUIRE(
      write_request.zero_copy_data(
        time10,
        std::as_bytes(std::span{*message2}),
        AsyncWriteRequestType::DataType::message,
        MessageHandle{message2}) == message2->size());

    message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message3 = std::move(message_result).value();
    const size_t message3_split_offset = TestAsyncWriterPolicy::max_write_size - message1->size() - message2->size();
    std::memset(message3->data(), 'C', message3->size());
    REQUIRE(
      write_request.zero_copy_data(
        time10,
        std::as_bytes(std::span{*message3}),
        AsyncWriteRequestType::DataType::message,
        MessageHandle{message3}) == message3_split_offset);

    REQUIRE(write_request.is_full());

    const auto io_vector = write_request.get_io_vector();
    REQUIRE(io_vector.size() == 3U);
    REQUIRE(jewels::at(io_vector, 0).iov_len == message1->size());
    REQUIRE(std::memcmp(message1->data(), jewels::at(io_vector, 0).iov_base, message1->size()) == 0);
    REQUIRE(jewels::at(io_vector, 1).iov_len == message2->size());
    REQUIRE(std::memcmp(message2->data(), jewels::at(io_vector, 1).iov_base, message2->size()) == 0);
    REQUIRE(jewels::at(io_vector, 2).iov_len == message3_split_offset);
    REQUIRE(std::memcmp(message3->data(), jewels::at(io_vector, 2).iov_base, message3_split_offset) == 0);
  }
}

} // namespace
} // namespace clockwork_logging::onboard
