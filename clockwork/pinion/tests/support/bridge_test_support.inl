// IWYU pragma: private, include "clockwork/pinion/tests/support/bridge_test_support.hh"

#pragma once

#include "clockwork/pinion/tests/support/bridge_test_support.hh"

#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/pinion/tests/support/bridge_test_message_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/span.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace clockwork::pinion::support
{

template <typename Msg>
[[nodiscard]] std::unique_ptr<Msg>
decompress_message(uint64_t counts_checksum, uint64_t data_checksum, std::span<const std::byte> compressed_data)
{
  clockwork_logging::LiteCompressor compressor{jewels::memory::MemoryResource{std::pmr::new_delete_resource()}};
  auto message = std::make_unique<Msg>();
  const auto decompress_outcome = compressor.decompress(
    counts_checksum, data_checksum, compressed_data, std::as_writable_bytes(jewels::as_single_item_span(*message)));
  if (!decompress_outcome.ok())
  {
    return nullptr;
  }
  return message;
}

template <typename Msg>
[[nodiscard]] std::optional<std::tuple<TcpMessageHeader, std::unique_ptr<Msg>, TcpMessageTail>>
// NOLINTNEXTLINE(readability-function-size) This is test-only code
recv_and_unpack(int sock, const Msg& expected_message, AckOption ack_option, std::chrono::nanoseconds recv_timeout)
{
  const auto recv_deadline = jewels::time::SyncClock::now() + recv_timeout;
  std::optional<TcpMessageHeader> maybe_header;
  while (true)
  {
    maybe_header = recv_header(sock, recv_timeout);
    if (!maybe_header)
    {
      return std::nullopt;
    }
    CHECK(maybe_header);
    CHECK(
      maybe_header->checksum ==
      clockwork_logging::compute_xxh3_checksum(std::as_bytes(jewels::as_single_item_span(maybe_header->body))));
    if (maybe_header->body.payload_type == PayloadType::message)
    {
      break;
    }
    if (jewels::time::SyncClock::now() > recv_deadline)
    {
      CHECK(false);
      return std::nullopt;
    }
    if (ack_option == AckOption::send_ack)
    {
      CHECK(send_acknowledgement(sock, maybe_header->body.sequence_number));
    }
  }
  REQUIRE(maybe_header->body.message_length != 0U);
  REQUIRE(
    maybe_header->body.message_length <=
    sizeof(expected_message) + clockwork_logging::LiteCompressor::max_compression_overhead_bytes);
  std::vector<std::byte> recv_buffer(maybe_header->body.message_length + sizeof(TcpMessageTail));
  if (!receive_buffer(sock, recv_buffer, recv_timeout))
  {
    CHECK(false);
    return std::nullopt;
  }
  TcpMessageTail tail{};
  std::memcpy(&tail, std::span{recv_buffer}.last(sizeof(TcpMessageTail)).data(), sizeof(TcpMessageTail));

  auto decompress_result = decompress_message<Msg>(
    tail.counts_checksum, tail.data_checksum, std::span{recv_buffer}.first(maybe_header->body.message_length));
  if (!decompress_result)
  {
    CHECK(decompress_result);
    return std::nullopt;
  }
  if (ack_option == AckOption::send_ack)
  {
    CHECK(send_acknowledgement(sock, maybe_header->body.sequence_number));
  }

  return std::tuple<TcpMessageHeader, std::unique_ptr<Msg>, TcpMessageTail>{
    maybe_header.value(), std::move(decompress_result), tail};
}

template <typename Msg>
[[nodiscard]] bool check_next_payload(
  int sock,
  uint64_t expected_seqno,
  const Msg& expected_message,
  int64_t expected_publish_time,
  int64_t expected_commit_time,
  AckOption ack_option,
  std::chrono::nanoseconds recv_timeout)
{
  auto payload = recv_and_unpack(sock, expected_message, ack_option, recv_timeout);
  if (!payload)
  {
    CHECK(payload);
    return false;
  }
  const auto& [header, message, tail] = *payload;
  CHECK(header.body.sequence_number == expected_seqno);
  CHECK(header.body.publish_timestamp == expected_publish_time);
  CHECK(header.body.source_commit_timestamp == expected_commit_time);
  CHECK((*message == expected_message));
  return header.body.sequence_number == expected_seqno && header.body.publish_timestamp == expected_publish_time &&
         *message == expected_message && header.body.source_commit_timestamp == expected_commit_time;
}

template <typename Msg>
[[nodiscard]] bool check_next_payload(
  int sock,
  uint64_t expected_seqno,
  const Msg& expected_message,
  AckOption ack_option,
  std::chrono::nanoseconds recv_timeout)
{
  auto payload = recv_and_unpack<Msg>(sock, expected_message, ack_option, recv_timeout);
  if (!payload)
  {
    CHECK(payload);
    return false;
  }
  const auto& [header, message, tail] = *payload;
  CHECK(header.body.sequence_number == expected_seqno);
  CHECK((*message == expected_message));
  return header.body.sequence_number == expected_seqno && *message == expected_message;
}

template <size_t message_size>
[[nodiscard]] std::unique_ptr<Tappy<BridgeTestMessage<message_size>>> make_random_message()
{
  auto msg = std::make_unique<Tappy<BridgeTestMessage<message_size>>>();
  clockwork_logging::onboard::tests::fill_with_random_bytes(msg->get_mutable_data());
  return msg;
}

} // namespace clockwork::pinion::support
