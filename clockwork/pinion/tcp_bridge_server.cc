// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/tcp_bridge_server.hh"

#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "clockwork/pinion/detail/socket_common.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/uuid/uuid.hh"

#include <arpa/inet.h>
#include <boost/iterator/iterator_facade.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <netinet/in.h>
#include <ranges>
#include <span>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <utility>

namespace clockwork::pinion
{
// NOLINTNEXTLINE(readability-function-size) TODO(OI-3673)
TcpBridgeServer::TcpBridgeServer(
  jewels::memory::MemoryResource memres,
  jewels::memory::ObjectPtr<AbstractEPollManager> epoll,
  std::string_view channel_name,
  SubscriberHandle&& subscriber,
  TcpSocket&& listen_socket,
  uint16_t listen_port,
  size_t max_clients,
  std::shared_ptr<TcpBridgeDiagnosticsCounters> diagnostics_counters,
  TcpBridgeServerMode mode)
  : memres_(memres),
    epoll_(std::move(epoll)),
    channel_name_(channel_name, memres),
    subscriber_(std::move(subscriber)),
    listen_socket_(std::move(listen_socket)),
    listen_port_(listen_port),
    clients_(max_clients, nullptr, memres),
    diagnostics_counters_(std::move(diagnostics_counters)),
    server_counters_(jewels::memory::make_pmr_shared<TcpBridgeClientServerCounters>(memres)),
    mode_(mode)
{
}

std::shared_ptr<TcpBridgeServer> TcpBridgeServer::make(
  jewels::memory::MemoryResource memres,
  const TcpBridgeServerConfigTap& config,
  SubscriberHandle&& subscriber,
  jewels::memory::ObjectPtr<AbstractEPollManager> epoll,
  const std::shared_ptr<TcpBridgeDiagnosticsCounters>& diagnostics_counters,
  TcpBridgeServerMode mode)
{
  const auto listen_addr =
    jewels::networking::SocketAddress::create(std::string{config.get_listen_address()}, config.get_listen_port());
  if (!listen_addr)
  {
    jewels::log_cerr_error(
      "cannot bind to address {}:{}: {}", config.get_listen_address(), config.get_listen_port(), listen_addr.error());
    return {};
  }

  auto listen_socket = TcpSocket::create_listen(*listen_addr, static_cast<int>(config.get_num_clients()));
  if (!listen_socket)
  {
    jewels::log_cerr_error(
      "failed to open socket on {}:{}: {}",
      config.get_listen_address(),
      config.get_listen_port(),
      jewels::filesystem::ErrorCode(listen_socket.error()));
    return {};
  }

  ::sockaddr_in actual_addr{};
  socklen_t addr_size = sizeof(actual_addr);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) The intended way to execute the syscall.
  if (::getsockname(listen_socket->descriptor(), reinterpret_cast<::sockaddr*>(&actual_addr), &addr_size) != 0)
  {
    jewels::log_cerr_error(
      "failed to open socket on {}:{}: {}",
      config.get_listen_address(),
      config.get_listen_port(),
      jewels::filesystem::ErrorCode(errno));
    return {};
  }

  auto bridge_server = jewels::memory::make_pmr_shared<TcpBridgeServer>(
    memres,
    memres,
    epoll,
    config.get_channel_name(),
    std::move(subscriber),
    std::move(*listen_socket),
    ::ntohs(actual_addr.sin_port),
    config.get_num_clients(),
    diagnostics_counters,
    mode);

  if (auto result = epoll->add(bridge_server->listen_fd(), EPOLLIN, bridge_server->shared_from_this()); !result)
  {
    jewels::log_cerr_error("failed to add server for {} to epoll", config.get_publisher_id());
    return {};
  }

  return bridge_server;
}

int TcpBridgeServer::listen_fd() const
{
  return listen_socket_.descriptor();
}

void TcpBridgeServer::notify(AbstractEPollManager& epoll, int /*efd*/, uint32_t events)
{
  if ((events & EPOLLIN) != 0)
  {
    accept_client(epoll);
  }
}

void TcpBridgeServer::notify(const Observer::Event& /*event*/)
{
  if (mode_ == TcpBridgeServerMode::overrun_test)
  {
    // Ignore notifies from the observer in unit test mode so we can test overrunning the buffer
    return;
  }
  for (auto& client : clients_)
  {
    if (client)
    {
      client->send_messages();
    }
  }
}

[[nodiscard]] std::string_view TcpBridgeServer::channel_name() const
{
  return channel_name_;
}

[[nodiscard]] TcpBridgeClientServerCounters TcpBridgeServer::get_and_reset_counters()
{
  TcpBridgeClientServerCounters result = *server_counters_;
  *server_counters_ = {};
  return result;
}

void TcpBridgeServer::accept_client(AbstractEPollManager& epoll)
{
  for (int retry = 0; retry < 3; retry++)
  {
    // Getting a false positive here...
    // NOLINTNEXTLINE(misc-const-correctness) False positive.
    jewels::filesystem::FileDescriptor client_fd{::accept(listen_fd(), nullptr, nullptr)};
    if (!client_fd)
    {
      diagnostics_counters_->client_socket_errors++;
      jewels::log_cerr_error(
        "Failed to accept connection from client for channel {}: ",
        channel_name_,
        jewels::filesystem::ErrorCode(errno));
      continue;
    }

    auto empty_slot = find_empty_slot();
    if (!empty_slot)
    {
      // Client list is full. Reject the new one.
      return;
    }

    if (!set_nonblocking(*client_fd, true))
    {
      return;
    }

    if (const auto result =
          jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(*client_fd, 1);
        !result)
    {
      diagnostics_counters_->client_socket_errors++;
      jewels::log_cerr_error("Failed to set tcp nodelay for channel {}: {}", channel_name_, result.error());
      return;
    }

    auto client = jewels::memory::make_pmr_shared<Client>(
      memres_,
      Client::ClientArgs{
        .memres = memres_,
        .epoll = jewels::memory::make_non_null_from_ref(*epoll_),
        .channel_name = channel_name_,
        .client_fd = std::move(client_fd),
        .subscriber = jewels::memory::make_non_null_from_ref(subscriber_),
        .diagnostics_counters = diagnostics_counters_,
        .server_counters = server_counters_,
      });
    auto result = epoll.add(client->client_fd(), EPOLLRDHUP | EPOLLOUT | EPOLLIN | EPOLLET, client->shared_from_this());
    if (!result)
    {
      diagnostics_counters_->client_socket_errors++;
      jewels::log_cerr_error(
        "TcpBridgeServer failed to register interest in client socket for channel {}: {}",
        channel_name_,
        result.error());
      return;
    }

    clients_[*empty_slot] = std::move(client);
    break;
  }
}

std::optional<size_t> TcpBridgeServer::find_empty_slot()
{
  for (size_t i = 0; i < clients_.size(); i++)
  {
    if (clients_[i] == nullptr || clients_[i]->client_fd() == -1)
    {
      clients_[i].reset();
      return i;
    }
  }

  return std::nullopt;
}

void TcpBridgeServer::send_null_header_if_waiting_for_ack()
{
  for (auto& client : clients_)
  {
    if (client)
    {
      client->send_null_header_if_waiting_for_ack();
    }
  }
}

bool TcpBridgeServer::is_waiting_for_ack() const
{
  return std::ranges::any_of(clients_, [](const auto& client) { return client && client->is_waiting_for_ack(); });
}

uint16_t TcpBridgeServer::listen_port() const noexcept
{
  return listen_port_;
}

TcpBridgeServer::Client::Client(ClientArgs args)
  : epoll_(args.epoll),
    channel_name_(args.channel_name),
    client_fd_(std::move(args.client_fd)),
    subscriber_(args.subscriber),
    diagnostics_counters_(std::move(args.diagnostics_counters)),
    server_counters_(std::move(args.server_counters)),
    lite_compressor_(args.memres),
    send_buffer_(args.memres)
{
  null_header_.checksum =
    clockwork_logging::compute_xxh3_checksum(std::as_bytes(jewels::as_single_item_span(null_header_.body)));
}

int TcpBridgeServer::Client::client_fd() const
{
  return *client_fd_;
}

void TcpBridgeServer::Client::notify(AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t events)
{
  if ((events & EPOLLRDHUP) != 0)
  {
    epoll_->remove(*client_fd_);
    client_fd_.forced_close();
  }

  if ((events & EPOLLIN) != 0)
  {
    receive_acknowledgements();
  }

  if ((events & EPOLLOUT) != 0)
  {
    const bool was_sending_message = !payload_.empty() && !sending_null_header_;
    send_pending_payload();
    if (was_sending_message && payload_.empty())
    {
      // Increment the last message iterator
      ++last_message_;
    }

    send_messages();
  }
}

void TcpBridgeServer::Client::send_messages()
{
  if (!payload_.empty())
  {
    // Ignore new messages until we finish sending any unfinished sends.
    return;
  }

  const auto subscriber_available = subscriber_->available();
  if (is_sentinel_iterator(last_message_))
  {
    last_message_ =
      subscriber_available.empty() ? subscriber_available.begin() : std::prev(std::end(subscriber_available));
  }

  auto available = pinion::available_starting_from(subscriber_available, last_message_);
  if (!available)
  {
    switch (available.error())
    {
    case ProgressError::fell_behind:
    {
      // We fell behind somehow. Fast foward.
      jewels::log_cerr_error(
        "Dropping {} messages for channel {}",
        std::distance(last_message_, std::prev(std::end(subscriber_available))),
        channel_name_);
      diagnostics_counters_->drop_count +=
        static_cast<size_t>(std::distance(last_message_, std::prev(std::end(subscriber_available))));
      available = {subscriber_available};
      last_message_ = std::prev(std::end(subscriber_available));
    }
    break;
    case ProgressError::in_the_future:
      diagnostics_counters_->progress_errors++;
      return;
    }
  }

  while (last_message_ != available->end())
  {
    auto slot = *last_message_;

    current_receive_time_ = jewels::time::SyncClock::now();
    current_message_size_ = slot.message().size();
    const auto publish_timestamp = slot.header()->publish_timestamp;
    const auto sequence_number = slot.header()->sequence_number;
    current_source_commit_timestamp_ = slot.header()->source_commit_timestamp;

    std::span<const std::span<const std::byte>> compressed_spans;
    uint64_t counts_checksum{};
    uint64_t data_checksum{};
    lite_compressor_.compress(
      jewels::Out{compressed_spans}, jewels::Out{counts_checksum}, jewels::Out{data_checksum}, slot.message());

    maybe_last_sequence_number_ = sequence_number;
    current_compression_end_time_ = jewels::time::SyncClock::now();

    const auto send_buffer_size = clockwork_logging::onboard::data_spans_size(compressed_spans);
    send_buffer_.resize(sizeof(TcpMessageHeader) + send_buffer_size + sizeof(TcpMessageTail));
    clockwork_logging::onboard::copy_data_spans(
      compressed_spans, std::span{send_buffer_}.subspan(sizeof(TcpMessageHeader), send_buffer_size));

    if (!subscriber_->still_available(last_message_))
    {
      jewels::log_cerr_error("Detected overrun while compressing message for channel {}", channel_name_);
      ++diagnostics_counters_->drop_count;
      ++last_message_;
      continue;
    }

    payload_ = std::span{send_buffer_};

    TcpMessageHeader header{};
    header.body.publish_timestamp = publish_timestamp;
    header.body.sequence_number = sequence_number;
    header.body.source_commit_timestamp = current_source_commit_timestamp_;
    header.body.message_length = send_buffer_size;
    header.checksum = clockwork_logging::compute_xxh3_checksum(std::as_bytes(jewels::as_single_item_span(header.body)));
    std::memcpy(payload_.data(), &header, sizeof(header));

    TcpMessageTail tail{};
    tail.counts_checksum = counts_checksum;
    tail.data_checksum = data_checksum;
    std::memcpy(payload_.last(sizeof(TcpMessageTail)).data(), &tail, sizeof(tail));

    send_pending_payload();
    if (!payload_.empty())
    {
      // We weren't able to send the entire payload. Don't send
      // anything else until we finish.
      break;
    }
    // Increment the last message iterator
    ++last_message_;
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): Complexity is due to repetitive error handling
void TcpBridgeServer::Client::send_pending_payload()
{
  if (payload_.empty())
  {
    return;
  }

  if (!client_fd_)
  {
    payload_ = {};
    return;
  }

  // Set TCP_NODELAY to force data to be sent immediately
  if (const auto result = jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(*client_fd_, 1);
      !result)
  {
    jewels::log_cerr_error("Failed to set tcp nodelay for channel {}: {}", channel_name_, result.error());
    epoll_->remove(*client_fd_);
    client_fd_.forced_close();
    diagnostics_counters_->closed_socket_count++;
    return;
  }

  /// Send until the entire payload has been sent or send fails with EAGAIN or EWOULDBLOCK
  while (true)
  {
    ssize_t bytes_sent = 0;
    bytes_sent = ::send(*client_fd_, payload_.data(), payload_.size(), MSG_NOSIGNAL);
    if (std::cmp_equal(bytes_sent, payload_.size()))
    {
      // Common case. We were able to send the entire payload.
      payload_ = {};

      if (sending_null_header_)
      {
        sending_null_header_ = false;
        return;
      }

      const auto current_time = jewels::time::SyncClock::now();
      const auto source_commit_stamp =
        jewels::time::SyncTime{std::chrono::nanoseconds(current_source_commit_timestamp_)};
      const auto receive_latency = current_receive_time_ - source_commit_stamp;
      const auto compression_time = current_compression_end_time_ - current_receive_time_;
      const auto transfer_time = current_time - current_compression_end_time_;
      const auto bridge_latency = current_time - source_commit_stamp;
      update_client_server_counters(
        current_message_size_,
        send_buffer_.size(),
        receive_latency,
        transfer_time,
        compression_time,
        bridge_latency,
        *server_counters_);
      return;
    }
    if (bytes_sent != -1)
    {
      // We weren't able to send the entire payload. Subtract whatever was sent
      // from the iovecs and leave the payload armed for a retry.
      payload_ = payload_.subspan(static_cast<size_t>(bytes_sent));
      return;
    }
    // We weren't able to send anything. Update counters depending on the
    // specific error.
    if (errno == EBADF || errno == ECONNRESET || errno == EPIPE)
    {
      // If we get any of these errors, the socket has been closed for some
      // reason. Clear the fd so we don't waste time during the interval
      // between now and when the client reconnects.
      jewels::log_cerr_error(
        "Closing socket for channel {} due to error: {}", channel_name_, jewels::filesystem::ErrorCode(errno));
      epoll_->remove(*client_fd_);
      client_fd_.forced_close();
      diagnostics_counters_->closed_socket_count++;
      payload_ = {};
      last_message_ = {};
      return;
    }
    if (errno == EWOULDBLOCK || errno == EAGAIN)
    {
      // Nothing to do in this case. Leave payload_ armed and try again once
      // we're woken up with EPOLLOUT.
      return;
    }
    if (errno == EINTR)
    {
      // Try again
      continue;
    }
    // For all other errors don't bother retrying. Just drop the message and
    // record the failure.
    jewels::log_cerr_error(
      "Failed to send message for channel {}: {}", channel_name_, jewels::filesystem::ErrorCode(errno));
    diagnostics_counters_->failed_sends++;
    payload_ = {};
    return;
  }
}

void TcpBridgeServer::Client::send_null_header_if_waiting_for_ack()
{
  if (!is_waiting_for_ack())
  {
    return;
  }

  payload_ = std::as_writable_bytes(jewels::as_single_item_span(null_header_));
  sending_null_header_ = true;

  send_pending_payload();
}

bool TcpBridgeServer::Client::is_waiting_for_ack() const
{
  return payload_.empty() && client_fd_ && maybe_last_sequence_number_;
}

void TcpBridgeServer::Client::receive_acknowledgements()
{
  if (!client_fd_)
  {
    return;
  }

  while (true)
  {
    uint8_t ack_byte{};
    auto bytes_received = ::recv(*client_fd_, &ack_byte, 1U, 0);
    if (bytes_received == 1)
    {
      if (maybe_last_sequence_number_ && ack_byte == static_cast<uint8_t>(*maybe_last_sequence_number_))
      {
        maybe_last_sequence_number_ = std::nullopt;
      }
      continue;
    }
    if (bytes_received == 0)
    {
      // The sender closed the socket for some reason. Close our end.
      epoll_->remove(*client_fd_);
      client_fd_.forced_close();
      jewels::log_cerr_error("Lost connection to server for channel {}", channel_name_);
      diagnostics_counters_->closed_socket_count++;
      return;
    }
    if (errno == EWOULDBLOCK || errno == EAGAIN)
    {
      // Nothing to do in this case. Wait to be woken up again.
      return;
    }
    if (errno == EINTR)
    {
      // Retry the read
      continue;
    }

    // All other errors are difficult to recover from. Just close the socket
    // and reconnect.
    diagnostics_counters_->failed_recvs++;
    epoll_->remove(*client_fd_);
    client_fd_.forced_close();
    jewels::log_cerr_error(
      "Failed to receive for channel {}. Closing socket: {}", channel_name_, jewels::filesystem::ErrorCode(errno));
    diagnostics_counters_->closed_socket_count++;
    return;
  }
}

} // namespace clockwork::pinion
