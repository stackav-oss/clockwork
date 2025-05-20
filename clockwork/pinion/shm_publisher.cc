// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/shm_publisher.hh"

#include "clockwork/pinion/detail/socket_common.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"

#include <cerrno>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <tuple>
#include <utility>

namespace clockwork::pinion
{

// NOLINTNEXTLINE(readability-function-size) TODO OI-2956 Refactor ShmChannel
jewels::expected<ShmPublisher, ShmChannel::Error> ShmPublisher::open(
  jewels::memory::MemoryResource memres,
  const jewels::filesystem::Directory& shm_dir,
  std::string_view socket_ns,
  std::string_view name,
  const BufferLayout& layout,
  size_t max_observers,
  size_t max_clients,
  ResumeBehavior resume_behavior)
{
  // Create socket first.  Because this (uniquely) binds the listener, it should fail if another publisher exists which
  // will avoid potential awkwardness with the shm buffer
  auto socket = ShmChannel::open_socket(socket_ns, name, Role::publisher);
  if (!socket)
  {
    return jewels::unexpected(socket.error());
  }
  auto buffer_map = ShmChannel::open_buffer(memres, shm_dir, name, layout, Role::publisher, resume_behavior);
  if (!buffer_map)
  {
    return jewels::unexpected(buffer_map.error());
  }
  /// When creating the publisher add 1 to the max_observers to account for the socket observer that this will add
  PublisherHandle publisher_handle{
    jewels::memory::make_non_null_from_ref(*std::get<1>(*buffer_map)), 1 + max_observers, memres};
  /// Listen after the shared memory area has been created so subscribers won't connect and then fail to open
  if (const auto listen_result = socket->listen(static_cast<int>(max_clients)); !listen_result)
  {
    jewels::log_cerr_error("Failed to listen on socket: {}", jewels::filesystem::ErrorCode(listen_result.error()));
    return jewels::unexpected(Error::fatal);
  }
  return ShmPublisher(
    memres,
    name,
    std::move(std::get<BufferPtr>(*buffer_map)),
    std::move(std::get<MMapRegion>(*buffer_map)),
    std::move(*socket),
    std::move(publisher_handle),
    max_clients,
    resume_behavior);
}

// NOLINTNEXTLINE(readability-function-size) TODO OI-2956 Refactor ShmChannel
ShmPublisher::ShmPublisher(
  jewels::memory::MemoryResource memres,
  std::string_view name,
  BufferPtr buffer,
  MMapRegion map,
  UnixSocket socket,
  PublisherHandle publisher,
  size_t max_clients,
  ResumeBehavior resume_behavior)
  : ShmChannel(std::move(buffer), std::move(map), std::move(socket), resume_behavior),
    name_(name, memres),
    publisher_(std::move(publisher)),
    log_cerr_throttle_(jewels::memory::make_pmr_shared<jewels::LogCerrThrottle>(memres, min_log_cerr_interval)),
    socket_clients_(
      jewels::memory::make_pmr_unique<SocketClients>(memres, memres, name_, max_clients, log_cerr_throttle_))
{
  if (!publisher_->add_observer(jewels::memory::make_non_null_from_ref(*socket_clients_)))
  {
    throw std::runtime_error("Failed to add socket observer to PublisherHandle");
  }
}

PublisherHandle& ShmPublisher::publisher()
{
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) This is expected to throw
  return publisher_.value();
}

jewels::expected<PublisherHandle, jewels::MonoError> ShmPublisher::extract_publisher() noexcept
{
  jewels::expected<PublisherHandle, jewels::MonoError> result{jewels::unexpected(jewels::MonoError())};
  if (publisher_)
  {
    result.emplace(std::move(*publisher_));
    publisher_.reset();
  }
  return result;
}

bool ShmPublisher::add_observer(jewels::memory::ObjectPtr<Observer> observer) noexcept
{
  if (!publisher_)
  {
    return false;
  }
  return publisher_->add_observer(observer);
}

size_t ShmPublisher::num_clients() const noexcept
{
  return socket_clients_->count();
}

bool ShmPublisher::on_connect_pending()
{
  socket_clients_->close_disconnected_sockets();
  bool discard = false;
  for (int retry = 0; retry < 3;)
  {
    jewels::filesystem::FileDescriptor client{::accept(this->socket(), nullptr, nullptr)};
    if (!client)
    {
      // For a non-blocking socket EAGAIN means there are no pending connections
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        return !discard;
      }
      // Error handling can be a bit of a pain here because Linux can return network level errors in addition to
      // accept() errors. While this could enumerate all known/possible errors, there aren't a lot of
      // handling options other than log and retry. By retrying, this will flush the pending network errors
      // and any accept() problems will persist.
      jewels::log_cerr_error(
        "ShmPublisher failed to accept connection for {}: {}", name_, jewels::filesystem::ErrorCode(errno));
      retry++;
      continue;
    }
    if (!socket_clients_->add(UnixSocket(std::move(client))))
    {
      discard = true;
    }
    jewels::log_cerr_info("ShmPublisher accepted connection for {}", name_);
  }
  // This means accept() kept returning errors through the retries
  return false;
}

void ShmPublisher::notify(AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t events)
{
  if ((events & EPOLLIN) != 0)
  {
    if (!on_connect_pending())
    {
      // No action, errors are logged in on_connect pending
    }
  }
}

ShmPublisher::SocketClients::SocketClients(
  jewels::memory::MemoryResource resource,
  std::string_view name,
  size_t max_clients,
  std::shared_ptr<jewels::LogCerrThrottle> log_cerr_throttle)
  : name_(name, resource), clients_(resource), log_cerr_throttle_(std::move(log_cerr_throttle))
{
  clients_.reserve(max_clients);
}

size_t ShmPublisher::SocketClients::count() const noexcept
{
  const std::lock_guard lock(mutex_);
  return clients_.size();
}

bool ShmPublisher::SocketClients::add(UnixSocket client)
{
  const std::lock_guard lock(mutex_);
  if (clients_.size() >= clients_.capacity())
  {
    return false;
  }
  if (!set_nonblocking(client.descriptor(), true))
  {
    return false;
  }
  clients_.emplace_back(std::move(client));
  // Notify the client to make sure they see the messages already in the buffer
  notify_client({}, std::prev(clients_.end())); // NOLINT(cert-err33-c) False positive
  return true;
}

std::pmr::vector<UnixSocket>::iterator
ShmPublisher::SocketClients::notify_client(const Event& event, std::pmr::vector<UnixSocket>::iterator iter)
{
  NotifyMsg msg{.tail = event.tail, .head = event.head};
  const auto result = ::send(iter->descriptor(), &msg, sizeof(msg), 0);
  if (result == -1)
  {
    // If the connection is gone remove the dead socket
    if (errno == ECONNRESET || errno == EPIPE)
    {
      jewels::log_cerr_info("ShmPublisher removed subscriber for {}", name_);
      return clients_.erase(iter);
    }
    // If this given a retry it means that the kernel buffer has filled up with 100+ pending messages.  Since those
    // pending messages will suffice for waking the remote process if/when they do get flushed, this message can just
    // be discarded
    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      if (close_socket_if_disconnected(*iter))
      {
        return clients_.erase(iter);
      }
      jewels::log_cerr_warn_throttled(
        *log_cerr_throttle_, "ShmPublisher failed to send notification for {} because send buffer is full", name_);
    }
    else
    {
      // The client notifications are blocking since there isn't an easy way to retry a notification if the socket
      // buffer is full (which it should never be)
      jewels::log_cerr_error(
        "ShmPublisher failed to send notification for {}: {}", name_, jewels::filesystem::ErrorCode(errno));
    }
  }
  return std::next(iter);
}

void ShmPublisher::SocketClients::notify(const Event& event)
{
  const std::lock_guard lock(mutex_);
  for (auto iter = clients_.begin(); iter != clients_.end();)
  {
    iter = notify_client(event, iter);
  }
}

bool ShmPublisher::SocketClients::close_socket_if_disconnected(UnixSocket& socket)
{
  uint8_t msg{};
  // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection) Socket is non-blocking
  const auto result = ::recv(socket.descriptor(), &msg, sizeof(msg), MSG_TRUNC);
  if (result == 0)
  {
    socket.close();
    jewels::log_cerr_error("Closing connection to inactive subscriber for {}", name_);
    return true;
  }
  if (result != -1)
  {
    socket.close();
    jewels::log_cerr_error("Internal error, received message from subscriber for {}", name_);
    return true;
  }
  if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
  {
    socket.close();
    jewels::log_cerr_info(
      "Closing connection to inactive subscriber for {}: {}", name_, jewels::filesystem::ErrorCode(errno));
    return true;
  }
  return false;
}

void ShmPublisher::SocketClients::close_disconnected_sockets()
{
  const std::lock_guard lock(mutex_);
  for (auto iter = clients_.begin(); iter != clients_.end();)
  {
    if (close_socket_if_disconnected(*iter))
    {
      iter = clients_.erase(iter);
    }
    else
    {
      ++iter;
    }
  }
}

} // namespace clockwork::pinion
