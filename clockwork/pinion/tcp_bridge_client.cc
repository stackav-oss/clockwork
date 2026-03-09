// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/tcp_bridge_client.hh"

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/detail/socket_common.hh"
#include "clockwork/pinion/detail/tcp_socket.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/time/sync_time.hh"

#include <poll.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <compare>
#include <cstdint>
#include <optional>
#include <ranges>
#include <ratio>
#include <span>
#include <sys/socket.h>
#include <sys/time.h>
#include <utility>

namespace clockwork::pinion
{

namespace
{

/// TCP bridge client connect interval
constexpr auto bridge_client_connect_interval = std::chrono::seconds(1);

/// TCP bridge connect poll timeout interval
constexpr auto bridge_client_connect_timeout = std::chrono::seconds(1);

/// TCP bridge receive timeout interval
constexpr auto bridge_client_recv_timeout = std::chrono::seconds(1);

} // namespace

TcpBridgeClient::TcpBridgeClient(TcpBridgeClientParams& params)
  : channel_name_(params.channel_name, params.memres),
    is_bulk_data_(params.is_bulk_data),
    publisher_(std::move(params.publisher)),
    subscriber_(std::move(params.subscriber)),
    server_address_(params.server_address),
    diagnostics_state_(std::move(params.diagnostics_state)),
    lite_compressor_(params.memres),
    recv_buffer_(params.memres),
    min_sequence_number_(params.min_sequence_number)
{
  worker_thread_ = std::thread([this]() { worker_thread_main(); });
}

TcpBridgeClient::~TcpBridgeClient()
{
  request_stop();
  if (worker_thread_.joinable())
  {
    worker_thread_.join();
  }
}

void TcpBridgeClient::request_stop()
{
  stop_requested_.store(true, std::memory_order_release);
}

std::shared_ptr<TcpBridgeClient> TcpBridgeClient::make(
  const jewels::memory::MemoryResource& memres,
  const Tappy<TcpBridgeClientConfig>& config,
  PublisherHandle publisher,
  SubscriberHandle subscriber,
  std::shared_ptr<TcpBridgeDiagnosticsState> diagnostics_state)
{
  auto server_addr =
    jewels::networking::SocketAddress::create(std::string{config.get_server_address()}, config.get_server_port());
  if (!server_addr)
  {
    jewels::log_cerr_error(
      "cannot connect to address {}:{}: {}",
      config.get_server_address(),
      config.get_server_port(),
      server_addr.error());
    return {};
  }

  uint64_t min_sequence_number = 0U;
  const auto current_messages = subscriber.available();
  if (!current_messages.empty())
  {
    min_sequence_number = current_messages.back().header()->sequence_number + 1U;
    // This should never happen because the publisher and subscriber should both be owned the the TCPBridgeClient, but
    // check for the error just in case.
    if (!current_messages.begin().is_valid())
    {
      jewels::log_cerr_error(
        "Detected overrun while obtaining the sequence number for channel: {}",
        config.get_publisher_endpoint().get_channel_name());
      diagnostics_state->increment_failed_recvs();
    }
  }

  TcpBridgeClientParams client_params{
    .memres = memres,
    .channel_name = config.get_publisher_endpoint().get_channel_name(),
    .is_bulk_data = config.get_publisher_endpoint().get_is_bulk_data(),
    .publisher = std::move(publisher),
    .subscriber = std::move(subscriber),
    .server_address = *server_addr,
    .min_sequence_number = min_sequence_number,
    .diagnostics_state = std::move(diagnostics_state),
  };

  auto bridge_client = jewels::memory::make_pmr_shared<TcpBridgeClient>(memres, client_params);

  return bridge_client;
}

int TcpBridgeClient::socket_fd() const
{
  if (!socket_)
  {
    return -1;
  }
  return socket_->descriptor();
}

[[nodiscard]] std::string_view TcpBridgeClient::channel_name() const
{
  return channel_name_;
}

[[nodiscard]] TcpBridgeClientServerCounters TcpBridgeClient::get_and_reset_counters()
{
  std::lock_guard guard{mutex_};
  TcpBridgeClientServerCounters result = client_counters_;
  client_counters_ = {};
  return result;
}

void TcpBridgeClient::worker_thread_main()
{
  while (!stop_requested_.load(std::memory_order_acquire))
  {
    switch (state_)
    {
    case State::idle:
      payload_ = std::as_writable_bytes(jewels::as_single_item_span(header_));
      state_ = State::receiving_header;
      [[fallthrough]];
    case State::receiving_header:
      receive_header();
      break;
    case State::receiving_message:
      receive_message();
      break;
    case State::disconnected:
      start_connect();
      break;
    case State::connecting:
      complete_connect();
      break;
    }
  }
}

void TcpBridgeClient::send_acknowledgement()
{
  if (!socket_)
  {
    return;
  }

  // Set TCP nodelay to force the ack to go out right away
  if (const auto result =
        jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(socket_->descriptor(), 1);
      !result)
  {
    jewels::log_cerr_error("Failed to set tcp nodelay for channel {}: {}", channel_name_, result.error());
    diagnostics_state_->increment_failed_sends();
    close_socket();
    return;
  }

  auto ack_byte = static_cast<uint8_t>(last_sequence_number_);
  auto bytes_sent = ::send(socket_->descriptor(), &ack_byte, 1U, MSG_NOSIGNAL | MSG_DONTWAIT);
  if (std::cmp_equal(bytes_sent, 1U))
  {
    return;
  }
  if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
  {
    // Nothing to do here, we will try to send the acknowedgement again when we receive a null header
    return;
  }
  // All other errors are difficult to recover from. Just close the socket
  // and reconnect.
  diagnostics_state_->increment_failed_sends();
  jewels::log_cerr_error(
    "Failed to send acknowledement for channel {}. Closing socket: {}",
    channel_name_,
    jewels::filesystem::ErrorCode(errno));
  close_socket();
}

void TcpBridgeClient::start_connect()
{
  if (!initial_connect_)
  {
    std::this_thread::sleep_for(bridge_client_connect_interval);
  }
  else
  {
    initial_connect_ = false;
  }

  const auto local_address = jewels::networking::SocketAddress::create_any_address(0);
  auto connect_result = TcpSocket::create_connect_async(server_address_, local_address);
  if (connect_result)
  {
    socket_ = *std::move(connect_result);
    state_ = State::connecting;
  }
  else
  {
    jewels::log_cerr_error("Failed to connect to server for {}", channel_name_);
  }
}

void TcpBridgeClient::complete_connect()
{
  if (!socket_)
  {
    return;
  }
  struct pollfd pfd{};
  pfd.fd = socket_->descriptor();
  pfd.events = POLLOUT;
  pfd.revents = 0;
  const auto poll_rc =
    ::poll(&pfd, 1, std::chrono::duration_cast<std::chrono::milliseconds>(bridge_client_connect_timeout).count());
  if (poll_rc < 0)
  {
    jewels::log_cerr_error(
      "Failed to poll for connection to server for {}: {}", channel_name_, jewels::filesystem::ErrorCode(errno));
    close_socket();
    return;
  }
  if (poll_rc == 0)
  {
    return;
  }
  int error{};
  socklen_t error_size = sizeof(error);
  if (const auto sockopt_rc = ::getsockopt(socket_->descriptor(), SOL_SOCKET, SO_ERROR, &error, &error_size);
      sockopt_rc != 0 || error != 0)
  {
    if (sockopt_rc != 0)
    {
      jewels::log_cerr_error(
        "Failed to get socket error for connect of {}: {}", channel_name_, jewels::filesystem::ErrorCode(errno));
    }
    else
    {
      jewels::log_cerr_error(
        "Failed to connect to server for {}: {}", channel_name_, jewels::filesystem::ErrorCode(error));
    }
    close_socket();
    return;
  }
  jewels::log_cerr_info("Connected to server for channel {}", channel_name_);
  const auto current_messages = subscriber_.available();
  if (!current_messages.empty())
  {
    min_sequence_number_ = current_messages.back().header()->sequence_number + 1U;
    // This should never happen because the publisher and subscriber should both be owned the the TCPBridgeClient, but
    // check for the error just in case.
    if (!current_messages.begin().is_valid())
    {
      jewels::log_cerr_error("Detected overrun while obtaining the sequence number for channel: {}", channel_name_);
      diagnostics_state_->increment_failed_recvs();
    }
  }
  else
  {
    min_sequence_number_ = 0U;
  }
  if (!set_nonblocking(socket_->descriptor(), false))
  {
    jewels::log_cerr_error("Failed to clear nonblocking for channel {}", channel_name_);
    close_socket();
    return;
  }
  const auto recv_timeout_sec = std::chrono::duration_cast<std::chrono::seconds>(bridge_client_recv_timeout);
  const auto recv_timeout_usec =
    std::chrono::duration_cast<std::chrono::microseconds>(bridge_client_recv_timeout - recv_timeout_sec);
  ::timeval recv_timeout{.tv_sec = recv_timeout_sec.count(), .tv_usec = recv_timeout_usec.count()};
  if (::setsockopt(socket_->descriptor(), SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(::timeval)) == -1)
  {
    jewels::log_cerr_error(
      "Failed to set recv timeout for channel {}: {}", channel_name_, jewels::filesystem::ErrorCode(errno));
    close_socket();
    return;
  }
  last_receive_time_ = jewels::time::SyncClock::now();
  state_ = State::idle;
}

void TcpBridgeClient::close_socket()
{
  diagnostics_state_->increment_closed_socket_count();
  payload_ = {};
  socket_.reset();
  state_ = State::disconnected;
}

void TcpBridgeClient::receive_from_socket()
{
  if (!socket_)
  {
    return;
  }

  auto bytes_received = ::recv(socket_->descriptor(), payload_.data(), payload_.size(), 0);
  if (bytes_received > 0)
  {
    // Advance the cursor so we can receive the rest later.
    payload_ = payload_.subspan(static_cast<size_t>(bytes_received));
    last_receive_time_ = jewels::time::SyncClock::now();
    return;
  }
  if (bytes_received == 0)
  {
    // The sender closed the socket for some reason. Close our end.
    jewels::log_cerr_error("Lost connection to server for channel {}", channel_name_);
    close_socket();
    return;
  }
  if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
  {
    if (jewels::time::SyncClock::now() - last_receive_time_ > tcp_bridge_reconnect_interval)
    {
      jewels::log_cerr_error("Stopped receiving from server for channel {}, starting reconnect", channel_name_);
      close_socket();
    }
    return;
  }

  // All other errors are difficult to recover from. Close the socket.
  diagnostics_state_->increment_failed_recvs();
  jewels::log_cerr_error(
    "Failed to receive for channel {}. Closing socket: {}", channel_name_, jewels::filesystem::ErrorCode(errno));
  close_socket();
}


[[nodiscard]] TcpBridgeClient::ValidateHeaderResult TcpBridgeClient::validate_header()
{
  if (const auto header_checksum =
        clockwork_logging::compute_xxh3_checksum(std::as_bytes(std::span{&header_.body, 1U}));
      header_checksum != header_.checksum)
  {
    jewels::log_cerr_error(
      "Received malformed message for channel {}: got checksum {} expected checksum {}",
      channel_name_,
      header_.checksum,
      header_checksum);
    return ValidateHeaderResult::bad_checksum;
  }

  if (header_.body.magic_number != tcp_message_header_magic_number)
  {
    jewels::log_cerr_error(
      "Received malformed message for channel {}: got invalid magic number with valid checksum.", channel_name_);
    return ValidateHeaderResult::invalid;
  }

  if (header_.body.payload_type == PayloadType::null_header || header_.body.payload_type == PayloadType::keep_alive)
  {
    return ValidateHeaderResult::null_header;
  }

  if (header_.body.payload_type != PayloadType::message)
  {
    jewels::log_cerr_error(
      "Received malformed message for channel {}: got payload type number ({}) with valid checksum.",
      static_cast<uint32_t>(header_.body.payload_type),
      channel_name_);
    return ValidateHeaderResult::invalid;
  }

  if (
    header_.body.message_length >
    publisher_.layout().message_size + clockwork_logging::LiteCompressor::max_compression_overhead_bytes)
  {
    // If the message size is wrong then the header is corrupted. Drop the packet and close the socket.
    jewels::log_cerr_error(
      "Received malformed message for channel {}: got {} bytes expected at most {} ({} + {})",
      channel_name_,
      header_.body.message_length,
      publisher_.layout().message_size + clockwork_logging::LiteCompressor::max_compression_overhead_bytes,
      publisher_.layout().message_size,
      clockwork_logging::LiteCompressor::max_compression_overhead_bytes);
    return ValidateHeaderResult::invalid;
  }

  return ValidateHeaderResult::valid;
}

void TcpBridgeClient::receive_header()
{
  receive_from_socket();
  if (state_ != State::receiving_header || !payload_.empty())
  {
    return;
  }

  const auto validate_header_result = validate_header();
  switch (validate_header_result)
  {
  case ValidateHeaderResult::valid:
    recv_buffer_.resize(header_.body.message_length + sizeof(TcpMessageTail));
    payload_ = std::span{recv_buffer_};
    current_receive_time_ = jewels::time::SyncClock::now();
    state_ = State::receiving_message;
    return;
  case ValidateHeaderResult::null_header:
    state_ = State::idle;
    send_acknowledgement();
    return;
  case ValidateHeaderResult::bad_checksum:
    [[fallthrough]];
  case ValidateHeaderResult::invalid:
    diagnostics_state_->increment_malformed_messages();
    close_socket();
    return;
  }
}

void TcpBridgeClient::receive_message()
{
  receive_from_socket();
  if (state_ != State::receiving_message || !payload_.empty())
  {
    return;
  }

  TcpMessageTail tail{};
  const auto copy_span = std::span{recv_buffer_}.last(sizeof(TcpMessageTail));
  std::ranges::copy(
    copy_span.begin(), copy_span.end(), std::as_writable_bytes(jewels::as_single_item_span(tail)).begin());

  // We received the entire payload. Decompress and publish it to the buffer if the sequence
  // number is above the minimum we were told to accept
  if (header_.body.sequence_number >= min_sequence_number_)
  {
    auto reservation = publisher_.reserve();
    if (!reservation)
    {
      // This should only happen if the channel is full. Don't bother draining
      // the socket until we have somewhere to put the payload.
      jewels::log_cerr_error("Failed to get reservation for channel {}", channel_name_);
      diagnostics_state_->increment_failed_reservations();
      last_sequence_number_ = header_.body.sequence_number;
      state_ = State::idle;
      send_acknowledgement();
      return;
    }

    auto slot = reservation->slot();
    const auto receive_end_time = jewels::time::SyncClock::now();
    if (const auto decompress_outcome = lite_compressor_.decompress(
          tail.counts_checksum,
          tail.data_checksum,
          std::span{recv_buffer_}.first(header_.body.message_length),
          slot.message());
        !decompress_outcome.ok())
    {
      // If decompression fails then the message is corrupted. Drop the packet.
      jewels::log_cerr_error("Received malformed message for channel {}: {}", channel_name_, decompress_outcome.get());
      diagnostics_state_->increment_malformed_messages();
      if (const auto result = reservation->discard(); !result)
      {
        jewels::log_cerr_error("Failed to discard message for channel {}: {}", channel_name_, result.error());
        diagnostics_state_->increment_failed_discards();
      }
      last_sequence_number_ = header_.body.sequence_number;
      state_ = State::idle;
      send_acknowledgement();
      return;
    }
    const auto decompress_end_time = jewels::time::SyncClock::now();
    const auto message_size = slot.message().size();

    const jewels::time::SyncTime publish_stamp{std::chrono::nanoseconds{header_.body.publish_timestamp}};
    const jewels::time::SyncTime source_commit_stamp{std::chrono::nanoseconds{header_.body.source_commit_timestamp}};
    if (const auto result = reservation->commit(publish_stamp, header_.body.sequence_number, source_commit_stamp);
        !result)
    {
      jewels::log_cerr_error("Failed to commit message for channel {}: {}", channel_name_, result.error());
      diagnostics_state_->increment_failed_commits();
      last_sequence_number_ = header_.body.sequence_number;
      state_ = State::idle;
      send_acknowledgement();
      return;
    }
    const auto publish_end_time = jewels::time::SyncClock::now();

    const auto receive_latency = current_receive_time_ - source_commit_stamp;
    const auto transfer_time = receive_end_time - current_receive_time_;
    const auto compression_time = decompress_end_time - receive_end_time;
    const auto bridge_latency = publish_end_time - source_commit_stamp;
    if (is_bulk_data_)
    {
      diagnostics_state_->update_max_bridge_bulk_data_latency(bridge_latency, channel_name_);
    }
    else
    {
      diagnostics_state_->update_max_bridge_latency(bridge_latency, channel_name_);
    }
    std::lock_guard guard{mutex_};
    update_client_server_counters(
      message_size,
      recv_buffer_.size(),
      receive_latency,
      transfer_time,
      compression_time,
      bridge_latency,
      client_counters_);
  }
  last_sequence_number_ = header_.body.sequence_number;
  state_ = State::idle;
  send_acknowledgement();
}

} // namespace clockwork::pinion
