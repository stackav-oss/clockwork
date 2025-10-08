// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/tcp_bridge_client.hh"

#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/detail/tcp_socket.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/time/sync_time.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <compare>
#include <cstdint>
#include <ctime>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <utility>

namespace clockwork::pinion
{

namespace
{

/// Initial bridge client connect interval
constexpr auto initial_bridge_client_connect_interval = std::chrono::milliseconds(1);

/// TCP bridge client reconnect interval
constexpr auto bridge_client_reconnect_interval = std::chrono::seconds(1);

/// Arm a timer file descriptor
/// @param[in] timer_fd Timer file descriptor
/// @param[in] value Timer value
void arm_timer(int32_t timer_fd, std::chrono::nanoseconds value)
{
  constexpr auto nanoseconds_per_second = 1'000'000'000;
  const auto value_sec = value.count() / nanoseconds_per_second;
  const auto value_nsec = value.count() % nanoseconds_per_second;
  auto spec = itimerspec{
    .it_interval = {.tv_sec = 0, .tv_nsec = 0},
    .it_value = {.tv_sec = value_sec, .tv_nsec = value_nsec},
  };
  if (timerfd_settime(timer_fd, 0, &spec, nullptr) == -1)
  {
    throw std::runtime_error("internal error: could not set timer specification");
  }
}

} // namespace

// NOLINTNEXTLINE(readability-function-size) TODO(OI-3672)
TcpBridgeClient::TcpBridgeClient(
  const jewels::memory::MemoryResource& memres,
  jewels::memory::ObjectPtr<AbstractEPollManager> epoll,
  std::string_view channel_name,
  PublisherHandle&& publisher,
  SubscriberHandle subscriber,
  jewels::filesystem::FileDescriptor&& timer_fd,
  jewels::networking::SocketAddress server_address,
  uint64_t min_sequence_number,
  std::shared_ptr<TcpBridgeDiagnosticsCounters> diagnostics_counters)
  : epoll_(std::move(epoll)),
    channel_name_(channel_name, memres),
    publisher_(std::move(publisher)),
    subscriber_(std::move(subscriber)),
    timer_fd_(std::move(timer_fd)),
    server_address_(server_address),
    diagnostics_counters_(std::move(diagnostics_counters)),
    lite_compressor_(memres),
    header_search_candidate_(memres),
    recv_buffer_(memres),
    min_sequence_number_(min_sequence_number)
{
}

std::shared_ptr<TcpBridgeClient> TcpBridgeClient::make(
  const jewels::memory::MemoryResource& memres,
  const TcpBridgeClientConfigTap& config,
  PublisherHandle&& publisher,
  SubscriberHandle subscriber,
  jewels::memory::ObjectPtr<AbstractEPollManager> epoll,
  const std::shared_ptr<TcpBridgeDiagnosticsCounters>& diagnostics_counters)
{
  auto timer_fd = jewels::filesystem::FileDescriptor{timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)};
  if (!timer_fd)
  {
    jewels::log_cerr_error("Failed to create timer descriptor: {}", jewels::filesystem::ErrorCode(errno));
    return {};
  }

  const auto local_addr = jewels::networking::SocketAddress::create("0.0.0.0", 0);
  if (!local_addr)
  {
    jewels::log_cerr_error("cannot bind to local address 0.0.0.0/0:  {}", local_addr.error());
    return {};
  }

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
    if (!subscriber.still_available(current_messages.begin()))
    {
      jewels::log_cerr_error(
        "Detected overrun while obtaining the sequence number for channel: {}",
        config.get_publisher_endpoint().get_channel_name());
      ++diagnostics_counters->failed_recvs;
    }
  }

  auto bridge_client = jewels::memory::make_pmr_shared<TcpBridgeClient>(
    memres,
    memres,
    epoll,
    config.get_publisher_endpoint().get_channel_name(),
    std::move(publisher),
    std::move(subscriber),
    std::move(timer_fd),
    *server_addr,
    min_sequence_number,
    diagnostics_counters);

  arm_timer(bridge_client->timer_fd(), initial_bridge_client_connect_interval);

  if (auto result = epoll->add(bridge_client->timer_fd(), EPOLLIN, bridge_client); !result)
  {
    jewels::log_cerr_error("failed to add timer for {} to epoll", config.get_publisher_endpoint().get_channel_name());
    return {};
  }

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

int TcpBridgeClient::timer_fd() const
{
  return *timer_fd_;
}

void TcpBridgeClient::notify(AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t /*events*/)
{
  while (true)
  {
    ReceiveResult receive_result{};
    switch (state_)
    {
    case State::idle:
      payload_ = std::as_writable_bytes(jewels::as_single_item_span(header_));
      state_ = State::receiving_header;
      [[fallthrough]];
    case State::receiving_header:
      receive_result = receive_header();
      break;
    case State::searching_for_next_header:
      receive_result = search_for_next_header();
      break;
    case State::receiving_message:
      receive_result = receive_message();
      break;
    case State::disconnected:
      receive_result = start_reconnect();
      break;
    case State::connecting:
      receive_result = complete_reconnect();
      break;
    }

    switch (receive_result)
    {
    case ReceiveResult::input_consumed:
      return;
    case ReceiveResult::keep_reading:
      [[fallthrough]];
    case ReceiveResult::payload_complete:
      continue;
    }
  }
}

[[nodiscard]] std::string_view TcpBridgeClient::channel_name() const
{
  return channel_name_;
}

[[nodiscard]] TcpBridgeClientServerCounters TcpBridgeClient::get_and_reset_counters()
{
  TcpBridgeClientServerCounters result = client_counters_;
  client_counters_ = {};
  return result;
}

TcpBridgeClient::ReceiveResult TcpBridgeClient::receive_from_socket()
{
  if (!socket_)
  {
    return ReceiveResult::input_consumed;
  }

  if (!header_search_remainder_.empty())
  {
    const auto bytes_to_copy = std::min(header_search_remainder_.size(), payload_.size());
    const auto copy_span = header_search_remainder_.first(bytes_to_copy);
    std::ranges::copy(copy_span.begin(), copy_span.end(), payload_.begin());
    header_search_remainder_ = header_search_remainder_.subspan(bytes_to_copy);
    payload_ = payload_.subspan(bytes_to_copy);
    if (payload_.empty())
    {
      return ReceiveResult::payload_complete;
    }
  }

  auto bytes_received = ::recv(socket_->descriptor(), payload_.data(), payload_.size(), 0);
  if (bytes_received > 0)
  {
    // Only received part of the payload. Advance the cursor so we can receive
    // the rest later.
    payload_ = payload_.subspan(static_cast<size_t>(bytes_received));
    return payload_.empty() ? ReceiveResult::payload_complete : ReceiveResult::input_consumed;
  }
  if (bytes_received == 0)
  {
    // The sender closed the socket for some reason. Close our end.
    jewels::log_cerr_error("Lost connection to server for channel {}", channel_name_);
    return close_socket_and_start_reconnect_timer();
  }
  if (errno == EWOULDBLOCK || errno == EAGAIN)
  {
    // Nothing to do in this case. Wait to be woken up again.
    return ReceiveResult::input_consumed;
  }
  if (errno == EINTR)
  {
    // Try again
    return ReceiveResult::keep_reading;
  }

  // All other errors are difficult to recover from. Just close the socket
  // and reconnect.
  diagnostics_counters_->failed_recvs++;
  jewels::log_cerr_error(
    "Failed to receive for channel {}. Closing socket: {}", channel_name_, jewels::filesystem::ErrorCode(errno));
  return close_socket_and_start_reconnect_timer();
}

[[nodiscard]] TcpBridgeClient::ValidateHeaderResult TcpBridgeClient::validate_header()
{
  if (const auto header_checksum =
        clockwork_logging::compute_xxh3_checksum(std::as_bytes(std::span{&header_.body, 1U}));
      header_checksum != header_.checksum)
  {
    if (state_ != State::searching_for_next_header)
    {
      jewels::log_cerr_error(
        "Received malformed message for channel {}: got checksum {} expected checksum {}",
        channel_name_,
        header_.checksum,
        header_checksum);
    }
    return ValidateHeaderResult::bad_checksum;
  }

  if (header_.body.magic_number != tcp_message_header_magic_number)
  {
    // If the magic number is wrong then the header is corrupted. Drop the packet and close the socket.
    jewels::log_cerr_error(
      "Received malformed message for channel {}: got invalid magic number with valid checksum.", channel_name_);
    return ValidateHeaderResult::invalid;
  }

  if (header_.body.message_length == 0)
  {
    return ValidateHeaderResult::null_header;
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

[[nodiscard]] TcpBridgeClient::ReceiveResult TcpBridgeClient::receive_header()
{
  const auto receive_result = receive_from_socket();
  switch (receive_result)
  {
  case ReceiveResult::input_consumed:
    [[fallthrough]];
  case ReceiveResult::keep_reading:
    return receive_result;
  case ReceiveResult::payload_complete:
    break;
  }

  const auto validate_header_result = validate_header();
  switch (validate_header_result)
  {
  case ValidateHeaderResult::valid:
    recv_buffer_.resize(header_.body.message_length + sizeof(TcpMessageTail));
    payload_ = std::span{recv_buffer_};
    current_receive_time_ = jewels::time::SyncClock::now();
    state_ = State::receiving_message;
    return ReceiveResult::keep_reading;
  case ValidateHeaderResult::null_header:
    state_ = State::idle;
    return send_acknowledgement();
  case ValidateHeaderResult::bad_checksum:
    diagnostics_counters_->malformed_messages++;
    state_ = State::searching_for_next_header;
    return ReceiveResult::keep_reading;
  case ValidateHeaderResult::invalid:
    diagnostics_counters_->malformed_messages++;
    return close_socket_and_start_reconnect_timer();
  }
}

[[nodiscard]] TcpBridgeClient::ReceiveResult TcpBridgeClient::receive_message()
{
  const auto receive_result = receive_from_socket();
  switch (receive_result)
  {
  case ReceiveResult::input_consumed:
    [[fallthrough]];
  case ReceiveResult::keep_reading:
    return receive_result;
  case ReceiveResult::payload_complete:
    break;
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
      diagnostics_counters_->failed_reservations++;
      last_sequence_number_ = header_.body.sequence_number;
      state_ = State::idle;
      return send_acknowledgement();
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
      diagnostics_counters_->malformed_messages++;
      if (const auto result = reservation->discard(); !result)
      {
        jewels::log_cerr_error("Failed to discard message for channel {}: {}", channel_name_, result.error());
        diagnostics_counters_->failed_discards++;
      }
      last_sequence_number_ = header_.body.sequence_number;
      state_ = State::idle;
      return send_acknowledgement();
    }
    const auto decompress_end_time = jewels::time::SyncClock::now();
    const auto message_size = slot.message().size();

    const jewels::time::SyncTime publish_stamp{std::chrono::nanoseconds{header_.body.publish_timestamp}};
    const jewels::time::SyncTime source_commit_stamp{std::chrono::nanoseconds{header_.body.source_commit_timestamp}};
    if (const auto result = reservation->commit(publish_stamp, header_.body.sequence_number, source_commit_stamp);
        !result)
    {
      jewels::log_cerr_error("Failed to commit message for channel {}: {}", channel_name_, result.error());
      diagnostics_counters_->failed_commits++;
      last_sequence_number_ = header_.body.sequence_number;
      state_ = State::idle;
      return send_acknowledgement();
    }
    const auto publish_end_time = jewels::time::SyncClock::now();

    const auto receive_latency = current_receive_time_ - source_commit_stamp;
    const auto transfer_time = receive_end_time - current_receive_time_;
    const auto compression_time = decompress_end_time - receive_end_time;
    const auto bridge_latency = publish_end_time - source_commit_stamp;
    if (bridge_latency > diagnostics_counters_->max_bridge_latency)
    {
      diagnostics_counters_->max_bridge_latency = bridge_latency;
      diagnostics_counters_->max_latency_channel_name = channel_name_;
    }
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
  return send_acknowledgement();
}

[[nodiscard]] TcpBridgeClient::ReceiveResult TcpBridgeClient::send_acknowledgement()
{
  if (!socket_)
  {
    return ReceiveResult::input_consumed;
  }

  // Set TCP nodelay to force the ack to go out right away
  if (const auto result =
        jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(socket_->descriptor(), 1);
      !result)
  {
    jewels::log_cerr_error("Failed to set tcp nodelay for channel {}: {}", channel_name_, result.error());
    diagnostics_counters_->failed_sends++;
    return close_socket_and_start_reconnect_timer();
  }

  auto ack_byte = static_cast<uint8_t>(last_sequence_number_);
  auto bytes_sent = ::send(socket_->descriptor(), &ack_byte, 1U, MSG_NOSIGNAL);
  if (std::cmp_equal(bytes_sent, 1U))
  {
    return ReceiveResult::keep_reading;
  }
  // We weren't able to send anything. Update counters depending on the
  // specific error.
  if (errno == EBADF || errno == ECONNRESET || errno == EPIPE)
  {
    // If we get any of these errors, the socket has been closed for some
    // reason. Close the socket so we don't waste time during the interval
    // between now and when the client reconnects.
    diagnostics_counters_->failed_sends++;
    jewels::log_cerr_error(
      "Failed to send acknowledement for channel {}. Closing socket: {}",
      channel_name_,
      jewels::filesystem::ErrorCode(errno));
    return close_socket_and_start_reconnect_timer();
  }
  if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
  {
    // Nothing to do here, we will try to send the acknowedgement again when we receive a null header
    return ReceiveResult::keep_reading;
  }
  // All other errors are difficult to recover from. Just close the socket
  // and reconnect.
  diagnostics_counters_->failed_sends++;
  jewels::log_cerr_error(
    "Failed to send acknowledement for channel {}. Closing socket: {}",
    channel_name_,
    jewels::filesystem::ErrorCode(errno));
  return close_socket_and_start_reconnect_timer();
}

void TcpBridgeClient::set_reconnect_timer()
{
  state_ = State::disconnected;
  arm_timer(*timer_fd_, bridge_client_reconnect_interval);
}

[[nodiscard]] TcpBridgeClient::ReceiveResult TcpBridgeClient::start_reconnect()
{
  uint64_t timer_count{};
  if (const auto ret = ::read(*timer_fd_, &timer_count, sizeof(timer_count)); ret == -1)
  {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      return ReceiveResult::input_consumed;
    }
    jewels::log_cerr_error(
      "Internal error: failed to read from reconnect timer for {}: {}",
      channel_name_,
      jewels::filesystem::ErrorCode(errno));
    set_reconnect_timer();
    return ReceiveResult::input_consumed;
  }

  const auto local_address = jewels::networking::SocketAddress::create("0.0.0.0", 0);
  if (!local_address)
  {
    jewels::log_cerr_error("cannot bind to local address 0.0.0.0/0:  {}", local_address.error());
    set_reconnect_timer();
    return ReceiveResult::input_consumed;
  }
  auto connect_result = TcpSocket::create_connect_async(server_address_, *local_address);
  if (connect_result)
  {
    if (const auto result = epoll_->add(connect_result->descriptor(), EPOLLOUT, shared_from_this()); !result)
    {
      jewels::log_cerr_error("failed to add client for {} to epoll", channel_name_);
      set_reconnect_timer();
      return ReceiveResult::input_consumed;
    }
    socket_ = *std::move(connect_result);
    state_ = State::connecting;
  }
  else
  {
    jewels::log_cerr_error("Failed to connect to server for {}", channel_name_);
    set_reconnect_timer();
  }
  return ReceiveResult::input_consumed;
}

[[nodiscard]] TcpBridgeClient::ReceiveResult TcpBridgeClient::complete_reconnect()
{
  if (!socket_)
  {
    return ReceiveResult::input_consumed;
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
    return close_socket_and_start_reconnect_timer();
  }
  epoll_->remove(socket_->descriptor());
  if (const auto result = epoll_->add(socket_->descriptor(), EPOLLRDHUP | EPOLLIN | EPOLLET, shared_from_this());
      !result)
  {
    jewels::log_cerr_error("failed to add client for {} to epoll", channel_name_);
    return close_socket_and_start_reconnect_timer();
  }
  jewels::log_cerr_info("Connected to server for channel {}", channel_name_);
  const auto current_messages = subscriber_.available();
  if (!current_messages.empty())
  {
    min_sequence_number_ = current_messages.back().header()->sequence_number + 1U;
    // This should never happen because the publisher and subscriber should both be owned the the TCPBridgeClient, but
    // check for the error just in case.
    if (!subscriber_.still_available(current_messages.begin()))
    {
      jewels::log_cerr_error("Detected overrun while obtaining the sequence number for channel: {}", channel_name_);
      ++diagnostics_counters_->failed_recvs;
    }
  }
  else
  {
    min_sequence_number_ = 0U;
  }
  state_ = State::idle;
  return ReceiveResult::keep_reading;
}

[[nodiscard]] TcpBridgeClient::ReceiveResult TcpBridgeClient::search_for_next_header()
{
  // Read the next chunk from the input stream
  if (header_search_remainder_.empty())
  {
    if (payload_.empty())
    {
      payload_ = std::span{header_search_buffer_};
    }
    const auto recv_result = receive_from_socket();
    if (payload_.size() == header_search_buffer_.size())
    {
      return recv_result;
    }
    header_search_remainder_ = std::span{header_search_buffer_}.first(header_search_buffer_.size() - payload_.size());
    payload_ = {};
  }

  // Locate the next magic number in the input stream and put it into the search candidate
  while (header_search_candidate_.size() < tcp_message_header_magic_number.size())
  {
    if (header_search_remainder_.empty())
    {
      return ReceiveResult::keep_reading;
    }
    if (header_search_remainder_.front() == tcp_message_header_magic_number.at(header_search_candidate_.size()))
    {
      header_search_candidate_.emplace_back(header_search_remainder_.front());
    }
    else
    {
      header_search_candidate_.clear();
      if (header_search_remainder_.front() == tcp_message_header_magic_number.front())
      {
        header_search_candidate_.emplace_back(header_search_remainder_.front());
      }
    }
    header_search_remainder_ = header_search_remainder_.subspan(1U);
  }

  // Copy the remainder of the header candidate from the search buffer
  if (header_search_remainder_.empty())
  {
    return ReceiveResult::keep_reading;
  }
  const auto bytes_to_copy =
    std::min(sizeof(TcpMessageHeader) - header_search_candidate_.size(), header_search_remainder_.size());
  const auto copy_offset = header_search_candidate_.size();
  header_search_candidate_.resize(copy_offset + bytes_to_copy);
  const auto copy_span = header_search_remainder_.first(bytes_to_copy);
  std::ranges::copy(
    copy_span.begin(), copy_span.end(), std::span{header_search_candidate_}.subspan(copy_offset).begin());
  header_search_remainder_ = header_search_remainder_.subspan(bytes_to_copy);
  if (header_search_candidate_.size() < sizeof(TcpMessageHeader))
  {
    return ReceiveResult::keep_reading;
  }

  // Try to validate the header candidate
  const auto header_span = std::span{header_search_candidate_};
  std::ranges::copy(
    header_span.begin(), header_span.end(), std::as_writable_bytes(jewels::as_single_item_span(header_)).begin());
  const auto validate_result = validate_header();
  switch (validate_result)
  {
  case ValidateHeaderResult::valid:
    header_search_candidate_.clear();
    recv_buffer_.resize(header_.body.message_length + sizeof(TcpMessageTail));
    payload_ = std::span{recv_buffer_};
    current_receive_time_ = jewels::time::SyncClock::now();
    state_ = State::receiving_message;
    return ReceiveResult::keep_reading;
  case ValidateHeaderResult::null_header:
    header_search_candidate_.clear();
    state_ = State::idle;
    return send_acknowledgement();
  case ValidateHeaderResult::bad_checksum:
    break;
  case ValidateHeaderResult::invalid:
    diagnostics_counters_->malformed_messages++;
    return close_socket_and_start_reconnect_timer();
  }

  // Header checksum was wrong, see if we have another magic number in the current header candidate
  size_t magic_number_offset = 1U;
  while (magic_number_offset < header_search_candidate_.size())
  {
    if (header_search_candidate_.at(magic_number_offset) == tcp_message_header_magic_number.front())
    {
      const auto bytes_to_check =
        std::min(tcp_message_header_magic_number.size(), header_search_candidate_.size() - magic_number_offset);
      if (std::ranges::equal(
            std::span{header_search_candidate_}.subspan(magic_number_offset, bytes_to_check),
            std::span{tcp_message_header_magic_number}.first(bytes_to_check)))
      {
        break;
      }
    }
    ++magic_number_offset;
  }
  if (magic_number_offset == header_search_candidate_.size())
  {
    header_search_candidate_.clear();
  }
  else
  {
    const auto new_candidate_size = header_search_candidate_.size() - magic_number_offset;
    const auto new_candidate_span =
      std::span{header_search_candidate_}.subspan(magic_number_offset, new_candidate_size);
    std::ranges::copy(
      new_candidate_span.begin(), new_candidate_span.end(), std::span{header_search_candidate_}.begin());
    header_search_candidate_.resize(new_candidate_size);
  }

  return ReceiveResult::keep_reading;
}

[[nodiscard]] TcpBridgeClient::ReceiveResult TcpBridgeClient::close_socket_and_start_reconnect_timer()
{
  header_search_remainder_ = {};
  header_search_candidate_.clear();
  payload_ = {};
  if (socket_)
  {
    epoll_->remove(socket_->descriptor());
    socket_.reset();
  }
  diagnostics_counters_->closed_socket_count++;
  set_reconnect_timer();
  return ReceiveResult::input_consumed;
}

} // namespace clockwork::pinion
