// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/tcp_bridge_client.hh"

#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/detail/socket_common.hh"
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
#include "jewels/time/sync_time.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <array>
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
#include <thread>
#include <unistd.h>
#include <utility>

namespace clockwork::pinion
{

namespace
{

/// TCP bridge client connect timeout
constexpr auto bridge_client_connect_timeout = std::chrono::seconds(90);

/// TCP bridge client reconnect interval in seconds
constexpr auto bridge_client_reconnect_interval_sec = std::chrono::seconds(1);

} // namespace

TcpBridgeClient::TcpBridgeClient(
  const jewels::memory::MemoryResource& memres,
  jewels::memory::ObjectPtr<AbstractEPollManager> epoll,
  std::string_view channel_name,
  PublisherHandle&& publisher,
  SubscriberHandle subscriber,
  jewels::filesystem::FileDescriptor&& timer_fd,
  TcpSocket&& socket,
  jewels::networking::SocketAddress server_address,
  uint64_t min_sequence_number,
  std::shared_ptr<TcpBridgeDiagnosticsCounters> diagnostics_counters)
  : epoll_(std::move(epoll)),
    channel_name_(channel_name, memres),
    publisher_(std::move(publisher)),
    subscriber_(std::move(subscriber)),
    timer_fd_(std::move(timer_fd)),
    socket_(std::move(socket)),
    server_address_(server_address),
    diagnostics_counters_(std::move(diagnostics_counters)),
    lite_compressor_(memres),
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

  std::optional<TcpSocket> socket{};
  // Every node should start this bridge at about the same time and will start
  // their servers before setting up any clients. Retries beyond the very
  // first client should be rare.
  const auto deadline = jewels::time::SteadyClock::now() + bridge_client_connect_timeout;
  while (!socket && jewels::time::SteadyClock::now() < deadline)
  {
    auto socket_attempt = TcpSocket::create_connect(*server_addr, *local_addr);
    if (socket_attempt)
    {
      socket.emplace(std::move(*socket_attempt));

      if (!set_nonblocking(socket->descriptor(), true))
      {
        jewels::log_cerr_error("Failed to set socket to nonblocking");
        return {};
      }
      if (const auto result =
            jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(socket->descriptor(), 1);
          !result)
      {
        jewels::log_cerr_error("Failed to set tcp nodelay: {}", result.error());
        return {};
      }
    }
    else
    {
      const auto sleep_dur = std::chrono::seconds(5);
      jewels::log_cerr_error(
        "failed to open socket to server at {}:{}: {}. Trying again in {}s",
        config.get_server_address(),
        config.get_server_port(),
        jewels::filesystem::ErrorCode(socket_attempt.error()),
        sleep_dur.count());
      std::this_thread::sleep_for(sleep_dur);
    }
  }
  if (!socket)
  {
    jewels::log_cerr_error(
      "server at {}:{} took too long to respond", config.get_server_address(), config.get_server_port());
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
        "Detected overrun while obtaining the sequence number for channel: {}", config.get_channel_name());
      ++diagnostics_counters->failed_recvs;
    }
  }

  auto bridge_client = jewels::memory::make_pmr_shared<TcpBridgeClient>(
    memres,
    memres,
    epoll,
    config.get_channel_name(),
    std::move(publisher),
    std::move(subscriber),
    std::move(timer_fd),
    *std::move(socket),
    *server_addr,
    min_sequence_number,
    diagnostics_counters);

  if (auto result = epoll->add(bridge_client->socket_fd(), EPOLLRDHUP | EPOLLIN | EPOLLET, bridge_client); !result)
  {
    jewels::log_cerr_error("failed to add client for {} to epoll", config.get_channel_name());
    return {};
  }
  if (auto result = epoll->add(bridge_client->timer_fd(), EPOLLIN, bridge_client); !result)
  {
    jewels::log_cerr_error("failed to add timer for {} to epoll", config.get_channel_name());
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
  switch (state_)
  {
  case State::disconnected:
    // socket_ is closed and input is ready on timer_fd, start a non-blocking connect to the server
    start_reconnect();
    break;
  case State::connecting:
    // A non-blocking connect is in progress and output is ready on socket_, complete the reconnect
    complete_reconnect();
    break;
  case State::idle:
  case State::receiving_header:
  case State::receiving_message:
    /// Client is connected
    receive_messages();
    break;
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity) TODO(OI-3013): refactor to reduce complexity
TcpBridgeClient::ReceiveResult TcpBridgeClient::receive_from_socket()
{
  if (!socket_)
  {
    return ReceiveResult::input_consumed;
  }

  auto& [header_iov, message_iov, tail_iov] = iovecs_;
  auto expected_bytes = header_iov.iov_len + message_iov.iov_len + tail_iov.iov_len;
  struct msghdr msg{
    .msg_name = nullptr,
    .msg_namelen = 0,
    .msg_iov = iovecs_.data(),
    .msg_iovlen = iovecs_.size(),
    .msg_control = nullptr,
    .msg_controllen = 0,
    .msg_flags = 0,
  };
  auto bytes_received = ::recvmsg(socket_->descriptor(), &msg, 0);
  if (bytes_received == static_cast<ssize_t>(expected_bytes))
  {
    iovecs_ = {};
    return ReceiveResult::message_complete;
  }
  if (bytes_received > 0)
  {
    // Only received part of the payload. Advance the cursor so we can receive
    // the rest later.
    advance_iovecs(std::span(iovecs_), static_cast<size_t>(bytes_received));
    return ReceiveResult::input_consumed;
  }
  if (bytes_received == 0)
  {
    // The sender closed the socket for some reason. Close our end.
    epoll_->remove(socket_->descriptor());
    socket_.reset();
    jewels::log_cerr_error("Lost connection to server for channel {}", channel_name_);
    diagnostics_counters_->closed_socket_count++;
    set_reconnect_timer();
    return ReceiveResult::input_consumed;
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
  epoll_->remove(socket_->descriptor());
  socket_.reset();
  jewels::log_cerr_error(
    "Failed to receive for channel {}. Closing socket: {}", channel_name_, jewels::filesystem::ErrorCode(errno));
  diagnostics_counters_->closed_socket_count++;
  set_reconnect_timer();
  return ReceiveResult::input_consumed;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) TODO(OI-3013): refactor to reduce complexity
void TcpBridgeClient::receive_messages()
{
  /// Receive messages until receive fails with EAGAIN or EWOULDBLOCK
  while (true)
  {
    if (!socket_)
    {
      return;
    }

    auto& [header_iov, message_iov, tail_iov] = iovecs_;
    if (state_ == State::idle)
    {
      iovecs_ = {};
      header_iov.iov_base = &header_;
      header_iov.iov_len = sizeof(TcpMessageHeader);

      state_ = State::receiving_header;
    }

    if (state_ == State::receiving_header)
    {
      const auto receive_result = receive_from_socket();
      switch (receive_result)
      {
      case ReceiveResult::input_consumed:
        return;
      case ReceiveResult::keep_reading:
        continue;
      case ReceiveResult::message_complete:
        break;
      }

      if (header_.message_length == 0)
      {
        // Zero message length is a null header
        state_ = State::idle;
        send_acknowledgement();
        continue;
      }

      recv_buffer_.resize(header_.message_length);
      iovecs_ = {};
      message_iov.iov_base = recv_buffer_.data();
      message_iov.iov_len = recv_buffer_.size();
      tail_iov.iov_base = &tail_;
      tail_iov.iov_len = sizeof(TcpMessageTail);
      current_receive_time_ = jewels::time::SyncClock::now();
      state_ = State::receiving_message;
    }

    const auto receive_result = receive_from_socket();
    switch (receive_result)
    {
    case ReceiveResult::input_consumed:
      return;
    case ReceiveResult::keep_reading:
      continue;
    case ReceiveResult::message_complete:
      break;
    }

    // We received the entire payload. Decompress and publish it to the buffer if the sequence
    // number is above the minimum we were told to accept
    if (tail_.commit && header_.sequence_number >= min_sequence_number_)
    {
      auto reservation = publisher_.reserve();
      if (!reservation)
      {
        // This should only happen if the channel is full. Don't bother draining
        // the socket until we have somewhere to put the payload.
        jewels::log_cerr_error("Failed to get reservation for channel {}", channel_name_);
        diagnostics_counters_->failed_reservations++;
        state_ = State::idle;
        continue;
      }

      auto slot = reservation->slot();
      const auto receive_end_time = jewels::time::SyncClock::now();
      if (const auto decompress_result =
            lite_compressor_.decompress(std::span{recv_buffer_.data(), recv_buffer_.size()}, slot.message());
          !decompress_result || decompress_result->size() != slot.message().size())
      {
        // Something got wired wrong. Drop the packet and close the socket.
        jewels::log_cerr_error("Received malformed message for channel {}", channel_name_);
        diagnostics_counters_->malformed_messages++;
        if (const auto result = reservation->discard(); !result)
        {
          jewels::log_cerr_error("Failed to discard message for channel {}: {}", channel_name_, result.error());
          diagnostics_counters_->failed_discards++;
        }
        epoll_->remove(socket_->descriptor());
        socket_.reset();
        diagnostics_counters_->closed_socket_count++;
        set_reconnect_timer();
        return;
      }
      const auto decompress_end_time = jewels::time::SyncClock::now();
      const auto message_size = slot.message().size();

      const jewels::time::SyncTime publish_stamp{std::chrono::nanoseconds{header_.publish_timestamp}};
      const jewels::time::SyncTime source_commit_stamp{std::chrono::nanoseconds{header_.source_commit_timestamp}};
      if (const auto result = reservation->commit(publish_stamp, header_.sequence_number, source_commit_stamp); !result)
      {
        jewels::log_cerr_error("Failed to commit message for channel {}: {}", channel_name_, result.error());
        diagnostics_counters_->failed_commits++;
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
    last_sequence_number_ = header_.sequence_number;
    state_ = State::idle;
    send_acknowledgement();
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
    diagnostics_counters_->failed_sends++;
    epoll_->remove(socket_->descriptor());
    socket_.reset();
    diagnostics_counters_->closed_socket_count++;
    set_reconnect_timer();
    return;
  }

  auto ack_byte = static_cast<uint8_t>(last_sequence_number_);
  auto bytes_sent = ::send(socket_->descriptor(), &ack_byte, 1U, MSG_NOSIGNAL);
  if (static_cast<size_t>(bytes_sent) == 1U)
  {
    return;
  }
  // We weren't able to send anything. Update counters depending on the
  // specific error.
  if (errno == EBADF || errno == ECONNRESET || errno == EPIPE)
  {
    // If we get any of these errors, the socket has been closed for some
    // reason. Close the socket so we don't waste time during the interval
    // between now and when the client reconnects.
    diagnostics_counters_->failed_sends++;
    epoll_->remove(socket_->descriptor());
    socket_.reset();
    jewels::log_cerr_error(
      "Failed to send acknowledement for channel {}. Closing socket: {}",
      channel_name_,
      jewels::filesystem::ErrorCode(errno));
    diagnostics_counters_->closed_socket_count++;
    set_reconnect_timer();
    return;
  }
  if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
  {
    // Nothing to do here, we will try to send the acknowedgement again when we receive a null header
    return;
  }
  // All other errors are difficult to recover from. Just close the socket
  // and reconnect.
  diagnostics_counters_->failed_sends++;
  epoll_->remove(socket_->descriptor());
  socket_.reset();
  jewels::log_cerr_error(
    "Failed to send acknowledement for channel {}. Closing socket: {}",
    channel_name_,
    jewels::filesystem::ErrorCode(errno));
  diagnostics_counters_->closed_socket_count++;
  set_reconnect_timer();
}

void TcpBridgeClient::set_reconnect_timer()
{
  state_ = State::disconnected;
  auto spec = itimerspec{
    .it_interval = {.tv_sec = 0, .tv_nsec = 0},
    .it_value = {.tv_sec = bridge_client_reconnect_interval_sec.count(), .tv_nsec = 0},
  };
  if (timerfd_settime(*timer_fd_, 0, &spec, nullptr) == -1)
  {
    throw std::runtime_error("internal error: could not set timer specification");
  }
}

void TcpBridgeClient::start_reconnect()
{
  uint64_t timer_count{};
  if (const auto ret = ::read(*timer_fd_, &timer_count, sizeof(timer_count)); ret == -1)
  {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      return;
    }
    jewels::log_cerr_error(
      "Internal error: failed to read from reconnect timer for {}: {}",
      channel_name_,
      jewels::filesystem::ErrorCode(errno));
    set_reconnect_timer();
    return;
  }

  const auto local_address = jewels::networking::SocketAddress::create("0.0.0.0", 0);
  if (!local_address)
  {
    jewels::log_cerr_error("cannot bind to local address 0.0.0.0/0:  {}", local_address.error());
    set_reconnect_timer();
    return;
  }
  auto connect_result = TcpSocket::create_connect_async(server_address_, *local_address);
  if (connect_result)
  {
    if (const auto result = epoll_->add(connect_result->descriptor(), EPOLLOUT, shared_from_this()); !result)
    {
      jewels::log_cerr_error("failed to add client for {} to epoll", channel_name_);
      set_reconnect_timer();
      return;
    }
    socket_ = *std::move(connect_result);
    state_ = State::connecting;
  }
  else
  {
    jewels::log_cerr_error("Failed to reconnect to server for {}", channel_name_);
    set_reconnect_timer();
  }
}

void TcpBridgeClient::complete_reconnect()
{
  if (!socket_)
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
        "Failed to get socket error for reconnect of {}: {}", channel_name_, jewels::filesystem::ErrorCode(errno));
    }
    else
    {
      jewels::log_cerr_error(
        "Failed to reconnect to server for {}: {}", channel_name_, jewels::filesystem::ErrorCode(error));
    }
    socket_.reset();
    diagnostics_counters_->closed_socket_count++;
    set_reconnect_timer();
  }
  else
  {
    epoll_->remove(socket_->descriptor());
    if (const auto result = epoll_->add(socket_->descriptor(), EPOLLRDHUP | EPOLLIN | EPOLLET, shared_from_this());
        !result)
    {
      jewels::log_cerr_error("failed to add client for {} to epoll", channel_name_);
      socket_.reset();
      diagnostics_counters_->closed_socket_count++;
      set_reconnect_timer();
      return;
    }
    jewels::log_cerr_info("Reconnected to server for channel {}", channel_name_);
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
  }
}

} // namespace clockwork::pinion
