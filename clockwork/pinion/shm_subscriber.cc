// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/shm_subscriber.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/time/sync_time.hh"

#include <cerrno>
#include <ctime>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace clockwork::pinion
{

// NOLINTNEXTLINE(readability-function-size) TODO OI-2956 Refactor ShmChannel
jewels::expected<ShmSubscriber, ShmChannel::Error> ShmSubscriber::open(
  jewels::memory::MemoryResource memres,
  const jewels::filesystem::Directory& shm_dir,
  std::string_view socket_ns,
  std::string_view filename,
  std::string_view channel_name,
  const BufferLayout& layout,
  size_t max_observers,
  SubscriberRole subscriber_role,
  ResumeBehavior resume_behavior)
{
  jewels::expected<UnixSocket, ShmChannel::Error> socket{jewels::unexpected(ShmChannel::Error::fatal)};
  if (subscriber_role != SubscriberRole::spy)
  {
    socket = ShmChannel::open_socket(socket_ns, filename, Role::subscriber);
    if (!socket)
    {
      return jewels::unexpected(socket.error());
    }
  }
  else
  {
    socket = UnixSocket{{}};
  }
  auto buffer_map = ShmChannel::open_buffer(memres, shm_dir, filename, layout, Role::subscriber, resume_behavior);
  if (!buffer_map)
  {
    return jewels::unexpected(buffer_map.error());
  }
  return ShmSubscriber(
    memres,
    socket_ns,
    filename,
    channel_name,
    subscriber_role,
    resume_behavior,
    std::move(std::get<BufferPtr>(*buffer_map)),
    std::move(std::get<jewels::filesystem::MMapRegion>(*buffer_map)),
    std::move(*socket),
    max_observers);
}

// NOLINTNEXTLINE(readability-function-size) TODO OI-2956 Refactor ShmChannel
ShmSubscriber::ShmSubscriber(
  jewels::memory::MemoryResource memres,
  std::string_view socket_ns,
  std::string_view filename,
  std::string_view channel_name,
  SubscriberRole subscriber_role,
  ResumeBehavior resume_behavior,
  BufferPtr buffer,
  jewels::filesystem::MMapRegion map,
  UnixSocket socket,
  size_t max_observers)
  : ShmChannel(
      memres, std::move(buffer), std::move(map), std::move(socket), socket_ns, filename, channel_name, resume_behavior),
    subscriber_role_(subscriber_role),
    observers_(memres)
{
  observers_.reserve(max_observers);
}

bool ShmSubscriber::add_observer(jewels::memory::ObjectPtr<Observer> observer) noexcept
{
  if (observers_.size() < observers_.capacity())
  {
    observers_.emplace_back(observer);
    return true;
  }
  return false;
}

bool ShmSubscriber::on_readable(AbstractEPollManager& epoll)
{
  NotifyMsg msg{};
  if (!is_connected())
  {
    return false;
  }
  while (true)
  {
    ssize_t result = ::recv(this->socket(), &msg, sizeof(msg), MSG_TRUNC);
    if (result == sizeof(msg))
    {
      for (auto& observer : observers_)
      {
        observer->notify(
          Observer::Event{.tail = msg.tail, .head = msg.head, .current_time = jewels::time::SyncClock::now()});
      }
      continue;
    }
    if (result == -1)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        return true;
      }
      jewels::log_cerr_error(
        "ShmSubscriber failed to read socket for {}: {}", channel_name(), jewels::filesystem::ErrorCode(errno));
      if (errno != EINTR && errno != ENOMEM)
      {
        // Only close the socket for fatal errors.
        jewels::log_cerr_error("Closing socket for {}", channel_name());
        epoll.remove(this->socket());
        close_socket();
        return false;
      }
      return true;
    }
    if (result == 0)
    {
      jewels::log_cerr_info("ShmSubscriber read end of file on socket for {}. Closing socket.", channel_name());
    }
    else
    {
      jewels::log_cerr_error(
        "ShmSubscriber read invalid message size for {}: got {} expected {}. Closing socket.",
        channel_name(),
        result,
        sizeof(msg));
    }
    epoll.remove(this->socket());
    close_socket();
    return false;
  }
}

void ShmSubscriber::on_reconnect_timer(AbstractEPollManager& epoll)
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
      channel_name(),
      jewels::filesystem::ErrorCode(errno));
    throw std::runtime_error("Failed to read from reconnect timer");
  }
  if (reconnect())
  {
    epoll.remove(*timer_fd_);
    timer_fd_.forced_close();
    if (!epoll.add(this->socket(), EPOLLIN | EPOLLHUP | EPOLLRDHUP, shared_from_this()))
    {
      throw std::runtime_error("internal error: could not add event to epoll");
    }
  }
}

void ShmSubscriber::create_reconnect_timer(AbstractEPollManager& epoll)
{
  jewels::log_cerr_info("Creating reconnect timer for {}", channel_name());
  timer_fd_ = jewels::filesystem::FileDescriptor{timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)};
  if (!timer_fd_)
  {
    throw std::runtime_error("internal error: could not create timer file descriptor");
  }
  auto spec = itimerspec{
    .it_interval = {.tv_sec = reconnect_interval_sec.count(), .tv_nsec = 0},
    .it_value = {.tv_sec = reconnect_interval_sec.count(), .tv_nsec = 0},
  };
  if (timerfd_settime(*timer_fd_, 0, &spec, nullptr) == -1)
  {
    throw std::runtime_error("internal error: could not set timer specification");
  }
  if (!epoll.add(*timer_fd_, EPOLLIN, shared_from_this()))
  {
    throw std::runtime_error("internal error: could not add timer event to epoll");
  }
}

void ShmSubscriber::notify(AbstractEPollManager& epoll, int /* efd */, uint32_t /*events*/)
{
  if (timer_fd_)
  {
    on_reconnect_timer(epoll);
  }
  else
  {
    if (!on_readable(epoll))
    {
      create_reconnect_timer(epoll);
    }
  }
}

bool ShmSubscriber::is_connected() const noexcept
{
  return this->socket() != -1;
}

bool ShmSubscriber::reconnect()
{
  if (is_connected())
  {
    jewels::log_cerr_error("Reconnect failed for {}: socket is already connected", channel_name());
    return false;
  }
  if (subscriber_role_ == SubscriberRole::spy)
  {
    jewels::log_cerr_error("Reconnect failed for {}: subscriber is opened in spy role", channel_name());
    return false;
  }
  auto socket = ShmChannel::open_socket(socket_ns(), filename(), Role::subscriber);
  if (!socket)
  {
    return false;
  }
  jewels::log_cerr_info("Reconnected to publisher for {}", channel_name());
  this->set_socket(*std::move(socket));
  for (auto& observer : observers_)
  {
    observer->notify(Observer::Event{.tail = 0, .head = 0, .current_time = jewels::time::SyncClock::now()});
  }
  return true;
}

int ShmSubscriber::timer_descriptor() const noexcept
{
  return *timer_fd_;
}

} // namespace clockwork::pinion
