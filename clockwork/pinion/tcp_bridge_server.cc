// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/tcp_bridge_server.hh"

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "clockwork/pinion/detail/socket_common.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/shm_channel.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/serialization/cpp/tachyon_lite_compressor.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/uuid/uuid.hh"

#include <arpa/inet.h>

#include <cerrno>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <netinet/in.h>
#include <numeric>
#include <ranges>
#include <ratio>
#include <span>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <utility>

namespace clockwork::pinion
{

namespace
{

/// Number of chunks to use when breaking up a message for bulk transfer
constexpr size_t num_bulk_data_chunks = 20U;

/// Get the receive acknowledgement timeout to use for a client connection
/// @param[in] mode Bridge server mode
/// @param[in] is_bulk_data True if the connection does bulk transfers
/// @return Time to wait for an ack from the client before closing a connection
[[nodiscard]] std::chrono::nanoseconds get_recv_ack_timeout(TcpBridgeServer::Mode mode, bool is_bulk_data)
{
  if (mode == TcpBridgeServer::Mode::unit_test)
  {
    return TcpBridgeServer::bridge_server_unit_test_recv_ack_timeout;
  }
  return is_bulk_data ? TcpBridgeServer::bridge_server_bulk_recv_ack_timeout
                      : TcpBridgeServer::bridge_server_recv_ack_timeout;
}

} // namespace

TcpBridgeServer::TcpBridgeServer(TcpBridgeServerParams& params)
  : memres_(params.memres),
    epoll_(std::move(params.epoll)),
    channel_name_(params.channel_name, params.memres),
    is_bulk_data_(params.is_bulk_data),
    uuid_str_(std::move(params.uuid_str)),
    layout_(params.layout),
    channel_factory_(std::move(params.channel_factory)),
    listen_socket_(std::move(params.listen_socket)),
    listen_port_(params.listen_port),
    clients_(params.max_clients, nullptr, params.memres),
    diagnostics_state_(std::move(params.diagnostics_state)),
    server_counters_mutex_(jewels::memory::make_pmr_shared<std::mutex>(params.memres)),
    server_counters_(jewels::memory::make_pmr_shared<TcpBridgeClientServerCounters>(params.memres)),
    mode_(params.mode),
    lite_compressor_(std::move(params.lite_compressor))
{
}

std::shared_ptr<TcpBridgeServer> TcpBridgeServer::make(
  jewels::memory::MemoryResource memres,
  const Tappy<TcpBridgeServerConfig>& config,
  std::shared_ptr<AbstractChannelFactory> channel_factory,
  jewels::memory::ObjectPtr<AbstractEPollManager> epoll,
  std::shared_ptr<TcpBridgeDiagnosticsState> diagnostics_state,
  Mode mode)
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

  std::shared_ptr<clockwork_logging::LiteCompressorInterface> lite_compressor;
  if (config.get_schema_encoding() == clockwork_logging::SchemaEncoding::clockwork_tachyon)
  {
    try
    {
      lite_compressor = serialization::TachyonLiteCompressor::make_compressor(
        memres, config.get_channel_name(), config.get_schema_definition());
    }
    catch (const std::runtime_error& exc)
    {
      jewels::log_cerr_warn("failed create lite compressor for {}, using default", config.get_channel_name());
      lite_compressor = jewels::memory::make_pmr_shared<clockwork_logging::LiteCompressor>(memres, memres);
    }
  }
  else
  {
    lite_compressor = jewels::memory::make_pmr_shared<clockwork_logging::LiteCompressor>(memres, memres);
  }
  TcpBridgeServerParams server_params{
    .memres = memres,
    .epoll = epoll,
    .channel_name = config.get_channel_name(),
    .is_bulk_data = config.get_is_bulk_data(),
    .uuid_str = config.get_publisher_id().to_string(memres),
    .layout =
      BufferLayout{
        .num_slots = config.get_buffer_layout().get_num_slots(),
        .message_size = config.get_buffer_layout().get_message_size(),
        .is_published_once = config.get_buffer_layout().get_is_published_once(),
      },
    .channel_factory = std::move(channel_factory),
    .listen_socket = std::move(*listen_socket),
    .listen_port = ::ntohs(actual_addr.sin_port),
    .max_clients = config.get_num_clients(),
    .diagnostics_state = std::move(diagnostics_state),
    .mode = mode,
    .lite_compressor = std::move(lite_compressor),
  };

  auto bridge_server = jewels::memory::make_pmr_shared<TcpBridgeServer>(memres, server_params);

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

void TcpBridgeServer::notify(AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t events)
{
  if ((events & EPOLLIN) != 0)
  {
    accept_client();
  }
}

[[nodiscard]] std::string_view TcpBridgeServer::channel_name() const
{
  return channel_name_;
}

[[nodiscard]] TcpBridgeClientServerCounters TcpBridgeServer::get_and_reset_counters()
{
  const std::lock_guard guard{*server_counters_mutex_};
  TcpBridgeClientServerCounters result = *server_counters_;
  *server_counters_ = {};
  return result;
}

void TcpBridgeServer::accept_client()
{
  for (int retry = 0; retry < 3; retry++)
  {
    // Getting a false positive here...
    // NOLINTNEXTLINE(misc-const-correctness) False positive.
    jewels::filesystem::FileDescriptor client_fd{::accept(listen_fd(), nullptr, nullptr)};
    if (!client_fd)
    {
      diagnostics_state_->increment_client_socket_errors();
      jewels::log_cerr_error(
        "Failed to accept connection from client for channel {}: {}",
        channel_name_,
        jewels::filesystem::ErrorCode(errno));
      continue;
    }

    auto empty_slot = find_empty_slot();
    if (!empty_slot)
    {
      // Client list is full. Reject the new one.
      jewels::log_cerr_warn("Client list is full, rejecting connection from client for channel {}", channel_name_);
      return;
    }

    const auto send_timeout_sec = std::chrono::duration_cast<std::chrono::seconds>(bridge_server_send_timeout);
    const auto send_timeout_usec =
      std::chrono::duration_cast<std::chrono::microseconds>(bridge_server_send_timeout - send_timeout_sec);
    ::timeval send_timeout{.tv_sec = send_timeout_sec.count(), .tv_usec = send_timeout_usec.count()};
    if (::setsockopt(*client_fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(::timeval)) == -1)
    {
      jewels::log_cerr_error(
        "Failed to set send timeout for channel {}: {}", channel_name_, jewels::filesystem::ErrorCode(errno));
      diagnostics_state_->increment_client_socket_errors();
      return;
    }

    auto subscriber_result = channel_factory_->open_subscriber(uuid_str_, channel_name_, layout_, 1);
    if (!subscriber_result)
    {
      jewels::log_cerr_error(
        "Could not create subscriber '{}': {}", channel_name_, wise_enum::to_string(subscriber_result.error()));
      return;
    }

    if (!set_nonblocking(subscriber_result.value()->socket(), false))
    {
      jewels::log_cerr_error("Failed to make unix socket blocking for '{}'", channel_name_);
      return;
    }

    auto client = jewels::memory::make_pmr_shared<Client>(
      memres_,
      Client::ClientArgs{
        .memres = memres_,
        .channel_name = channel_name_,
        .is_bulk_data = is_bulk_data_,
        .client_fd = std::move(client_fd),
        .subscriber = *std::move(subscriber_result),
        .diagnostics_state = diagnostics_state_,
        .server_counters_mutex = server_counters_mutex_,
        .server_counters = server_counters_,
        .mode = mode_,
        .lite_compressor = lite_compressor_->clone(),
      });

    clients_[*empty_slot] = std::move(client);
    break;
  }
}

std::optional<size_t> TcpBridgeServer::find_empty_slot()
{
  for (size_t i = 0; i < clients_.size(); i++)
  {
    if (clients_[i] == nullptr || !clients_[i]->is_running())
    {
      clients_[i].reset();
      return i;
    }
  }

  return std::nullopt;
}

uint16_t TcpBridgeServer::listen_port() const noexcept
{
  return listen_port_;
}

[[nodiscard]] size_t TcpBridgeServer::get_num_clients() const noexcept
{
  return std::accumulate(
    clients_.begin(),
    clients_.end(),
    size_t{0U},
    [](size_t sum, auto& client) { return sum + ((client && client->is_running()) ? 1U : 0U); });
}

TcpBridgeServer::Client::Client(ClientArgs args)
  : channel_name_(args.channel_name),
    is_bulk_data_(args.is_bulk_data),
    client_fd_(std::move(args.client_fd)),
    subscriber_(args.subscriber),
    diagnostics_state_(std::move(args.diagnostics_state)),
    server_counters_mutex_(std::move(args.server_counters_mutex)),
    server_counters_(std::move(args.server_counters)),
    lite_compressor_(std::move(args.lite_compressor)),
    send_buffer_(args.memres),
    last_send_time_(jewels::time::SteadyClock::now()),
    last_ack_recv_time_(jewels::time::SteadyClock::now()),
    mode_(args.mode),
    recv_ack_timeout_(get_recv_ack_timeout(mode_, is_bulk_data_))
{
  worker_thread_ = std::thread([this]() { worker_thread_main(); });
}

TcpBridgeServer::Client::~Client()
{
  request_stop();
  if (worker_thread_.joinable())
  {
    worker_thread_.join();
  }
}

void TcpBridgeServer::Client::request_stop()
{
  worker_stop_requested_.store(true, std::memory_order_relaxed);
}

[[nodiscard]] bool TcpBridgeServer::Client::is_running() const
{
  return worker_is_running_.load(std::memory_order_acquire);
}

void TcpBridgeServer::Client::worker_thread_main()
{
  while (!subscriber_->handshake())
  {
    constexpr auto handshake_retry_interval = std::chrono::milliseconds(100);
    jewels::log_cerr_info("Waiting for subscriber initialization to complete ({})", subscriber_->name());
    std::this_thread::sleep_for(handshake_retry_interval);
  }
  send_messages();
  send_null_header();
  while (client_fd_)
  {
    const auto prev_is_waiting_for_ack = is_waiting_for_ack();
    const auto notify_timeout =
      prev_is_waiting_for_ack ? bridge_server_fast_notify_timeout : bridge_server_notify_timeout;
    bool was_notified{};
    if (!jewels::ok(receive_notify(notify_timeout, jewels::Out{was_notified})))
    {
      break;
    }
    if (worker_stop_requested_.load(std::memory_order_relaxed))
    {
      break;
    }

    receive_acknowledgements();
    if (!client_fd_)
    {
      break;
    }

    bool message_sent = was_notified ? send_messages() : false;
    if (!client_fd_)
    {
      break;
    }

    if (jewels::time::SteadyClock::now() - last_ack_recv_time_ > recv_ack_timeout_)
    {
      // Client stopped sending acks, close the cnnection and let the client reconnect
      client_fd_.forced_close();
      jewels::log_cerr_error(
        "Client has stopped sending acknowledgements for channel {}, closing connection", channel_name_);
      diagnostics_state_->increment_closed_socket_count();
      break;
    }

    if (!message_sent && (is_waiting_for_ack() || !prev_is_waiting_for_ack))
    {
      send_null_header();
    }
  }
  worker_is_running_.store(false, std::memory_order_release);
}

jewels::BinaryOutcome
TcpBridgeServer::Client::receive_notify(std::chrono::nanoseconds timeout, jewels::Out<bool> was_notified)
{
  const auto timeout_sec = std::chrono::duration_cast<std::chrono::seconds>(timeout);
  const auto timeout_usec = std::chrono::duration_cast<std::chrono::microseconds>(timeout - timeout_sec);
  ::timeval recv_timeout{.tv_sec = timeout_sec.count(), .tv_usec = timeout_usec.count()};
  if (::setsockopt(subscriber_->socket(), SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(::timeval)) == -1)
  {
    client_fd_.forced_close();
    jewels::log_cerr_error(
      "Failed to set socket timoeout on notify socket for {}: {}", channel_name_, jewels::filesystem::ErrorCode(errno));
    diagnostics_state_->increment_closed_socket_count();
    return jewels::failure;
  }

  ShmChannel::NotifyMsg msg{};
  ssize_t result = ::recv(subscriber_->socket(), &msg, sizeof(msg), MSG_TRUNC);
  if (result == sizeof(msg))
  {
    *was_notified = true;
    return jewels::success;
  }
  if (result == -1)
  {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      *was_notified = false;
      return jewels::success;
    }
    jewels::log_cerr_error(
      "Failed to receieve notify from publisher for {}: {}", channel_name_, jewels::filesystem::ErrorCode(errno));
    if (errno != EINTR && errno != ENOMEM)
    {
      // Close the client connection and let the client reconnect
      client_fd_.forced_close();
      diagnostics_state_->increment_closed_socket_count();
      return jewels::failure;
    }
    *was_notified = false;
    return jewels::success;
  }
  if (result == 0)
  {
    jewels::log_cerr_info("Read end of file on notify socket for {}. Closing socket.", channel_name_);
  }
  else
  {
    jewels::log_cerr_error(
      "Read invalid message size on notify socket for {}: got {} expected {}. Closing socket.",
      channel_name_,
      result,
      sizeof(msg));
  }
  client_fd_.forced_close();
  diagnostics_state_->increment_closed_socket_count();
  return jewels::failure;
}

[[nodiscard]] bool TcpBridgeServer::Client::send_messages()
{
  bool message_sent = false;
  const auto subscriber_available = subscriber_->available();

  if (last_message_.is_sentinel())
  {
    last_message_ =
      subscriber_available.empty() ? subscriber_available.begin() : std::prev(std::end(subscriber_available));
  }

  auto available = available_starting_from(subscriber_available, last_message_);
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
      diagnostics_state_->increment_drop_count(
        static_cast<size_t>(std::distance(last_message_, std::prev(std::end(subscriber_available)))));
      available = {subscriber_available};
      last_message_ = std::prev(std::end(subscriber_available));
    }
    break;
    case ProgressError::in_the_future:
      diagnostics_state_->increment_progress_errors();
      return message_sent;
    }
  }

  while (client_fd_ && last_message_ != available->end())
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
    lite_compressor_->compress(
      jewels::Out{compressed_spans}, jewels::Out{counts_checksum}, jewels::Out{data_checksum}, slot.message());

    const auto send_buffer_size = clockwork_logging::onboard::data_spans_size(compressed_spans);
    send_buffer_.resize(sizeof(TcpMessageHeader) + send_buffer_size + sizeof(TcpMessageTail));
    clockwork_logging::onboard::copy_data_spans(
      compressed_spans, std::span{send_buffer_}.subspan(sizeof(TcpMessageHeader), send_buffer_size));

    if (!last_message_.is_valid())
    {
      jewels::log_cerr_error("Detected overrun while compressing message for channel {}", channel_name_);
      // We will have been notified since we were overrun. Let the next call to send_messages will
      // handle jumping to the end of the available messages.
      return message_sent;
    }

    last_sequence_number_ = sequence_number;
    maybe_unacked_sequence_number_ = sequence_number;
    current_compression_end_time_ = jewels::time::SyncClock::now();

    const auto payload = std::span{send_buffer_};

    TcpMessageHeader header{};
    header.body.payload_type = PayloadType::message;
    header.body.publish_timestamp = publish_timestamp;
    header.body.sequence_number = sequence_number;
    header.body.source_commit_timestamp = current_source_commit_timestamp_;
    header.body.message_length = send_buffer_size;
    header.checksum = clockwork_logging::compute_xxh3_checksum(std::as_bytes(jewels::as_single_item_span(header.body)));
    std::memcpy(payload.data(), &header, sizeof(header));

    TcpMessageTail tail{};
    tail.counts_checksum = counts_checksum;
    tail.data_checksum = data_checksum;
    std::memcpy(payload.last(sizeof(TcpMessageTail)).data(), &tail, sizeof(tail));

    if (is_bulk_data_)
    {
      send_bulk_payload(payload, PayloadType::message, current_receive_time_ + max_bridge_bulk_data_transmit_delay);
    }
    else
    {
      send_payload(payload, PayloadType::message);
    }
    if (client_fd_)
    {
      // Increment the last message iterator
      ++last_message_;
      message_sent = true;
    }
  }
  return message_sent;
}

[[nodiscard]] bool TcpBridgeServer::Client::send_payload_chunk(std::span<std::byte> payload)
{
  if (!client_fd_)
  {
    return false;
  }

  /// Send until the entire payload has been sent or send fails
  while (true)
  {
    // Set TCP_NODELAY to force data to be sent immediately
    if (const auto result =
          jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(*client_fd_, 1);
        !result)
    {
      jewels::log_cerr_error("Failed to set tcp nodelay for channel {}: {}", channel_name_, result.error());
      client_fd_.forced_close();
      diagnostics_state_->increment_closed_socket_count();
      return false;
    }

    const ssize_t bytes_sent = ::send(*client_fd_, payload.data(), payload.size(), MSG_NOSIGNAL);
    if (std::cmp_equal(bytes_sent, payload.size()))
    {
      last_send_time_ = jewels::time::SteadyClock::now();
      return true;
    }
    if (bytes_sent != -1)
    {
      // Partial send
      payload = payload.subspan(static_cast<size_t>(bytes_sent));
      continue;
    }
    if (errno == EINTR)
    {
      // Keep sending
      continue;
    }
    // Close the connection and let the client reconnect
    client_fd_.forced_close();
    jewels::log_cerr_error(
      "Failed to send message for channel {}: {}", channel_name_, jewels::filesystem::ErrorCode(errno));
    diagnostics_state_->increment_failed_sends();
    diagnostics_state_->increment_closed_socket_count();
    return false;
  }
}

void TcpBridgeServer::Client::update_counters_for_message()
{
  const auto current_time = jewels::time::SyncClock::now();
  const auto source_commit_stamp = jewels::time::SyncTime{std::chrono::nanoseconds(current_source_commit_timestamp_)};
  const auto receive_latency = current_receive_time_ - source_commit_stamp;
  const auto compression_time = current_compression_end_time_ - current_receive_time_;
  const auto transfer_time = current_time - current_compression_end_time_;
  const auto bridge_latency = current_time - source_commit_stamp;
  std::lock_guard guard{*server_counters_mutex_};
  update_client_server_counters(
    current_message_size_,
    send_buffer_.size(),
    receive_latency,
    transfer_time,
    compression_time,
    bridge_latency,
    *server_counters_);
}

void TcpBridgeServer::Client::send_payload(std::span<std::byte> payload, PayloadType payload_type)
{
  if (!send_payload_chunk(payload))
  {
    return;
  }

  if (payload_type == PayloadType::message)
  {
    update_counters_for_message();
  }
}

void TcpBridgeServer::Client::send_bulk_payload(
  std::span<std::byte> payload, PayloadType payload_type, jewels::time::SyncTime transmission_deadline)
{
  auto next_send_time = jewels::time::SyncClock::now();
  if (next_send_time >= transmission_deadline || payload.size() < num_bulk_data_chunks)
  {
    // No need to handle the send payload result
    send_payload(payload, payload_type);
    return;
  }

  const auto chunk_size = payload.size() / num_bulk_data_chunks;
  const auto remainder = payload.size() - (chunk_size * num_bulk_data_chunks);
  const auto send_interval = (transmission_deadline - next_send_time) / num_bulk_data_chunks;

  for (size_t i = 0U; i < num_bulk_data_chunks && client_fd_; ++i)
  {
    next_send_time += send_interval;
    std::this_thread::sleep_until(next_send_time);
    const auto chunk = payload.first(i == 0U ? chunk_size + remainder : chunk_size);
    if (!send_payload_chunk(chunk))
    {
      return;
    }
    payload = payload.subspan(chunk.size());
  }

  if (payload_type == PayloadType::message)
  {
    update_counters_for_message();
  }
}

void TcpBridgeServer::Client::send_null_header()
{
  TcpMessageHeader header{};
  if (maybe_unacked_sequence_number_)
  {
    header.body.payload_type = PayloadType::null_header;
    std::lock_guard guard{*server_counters_mutex_};
    ++server_counters_->null_header_count;
  }
  else
  {
    header.body.payload_type = PayloadType::keep_alive;
    std::lock_guard guard{*server_counters_mutex_};
    ++server_counters_->keep_alive_count;
  }
  header.body.sequence_number = maybe_unacked_sequence_number_.value_or(last_sequence_number_);
  header.checksum = clockwork_logging::compute_xxh3_checksum(std::as_bytes(jewels::as_single_item_span(header.body)));
  const auto payload = std::as_writable_bytes(jewels::as_single_item_span(header));
  send_payload(payload, header.body.payload_type);
}

bool TcpBridgeServer::Client::is_waiting_for_ack() const
{
  return client_fd_ && maybe_unacked_sequence_number_;
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
    auto bytes_received = ::recv(*client_fd_, &ack_byte, 1U, MSG_DONTWAIT);
    if (bytes_received == 1)
    {
      if (maybe_unacked_sequence_number_ && ack_byte == static_cast<uint8_t>(*maybe_unacked_sequence_number_))
      {
        maybe_unacked_sequence_number_ = std::nullopt;
      }
      last_ack_recv_time_ = jewels::time::SteadyClock::now();
      continue;
    }
    if (bytes_received == 0)
    {
      // The sender closed the socket for some reason. Close our end.
      client_fd_.forced_close();
      jewels::log_cerr_error("Lost connection to client for channel {}", channel_name_);
      diagnostics_state_->increment_closed_socket_count();
      return;
    }
    if (errno == EWOULDBLOCK || errno == EAGAIN)
    {
      // Nothing left to read
      return;
    }
    if (errno == EINTR)
    {
      // Retry the read
      continue;
    }

    // All other errors are difficult to recover from. Just close the socket and let the client reconnect
    diagnostics_state_->increment_failed_recvs();
    client_fd_.forced_close();
    jewels::log_cerr_error(
      "Failed to receive for channel {}. Closing socket: {}", channel_name_, jewels::filesystem::ErrorCode(errno));
    diagnostics_state_->increment_closed_socket_count();
    return;
  }
}

} // namespace clockwork::pinion
