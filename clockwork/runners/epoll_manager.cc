// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/runners/epoll_manager.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <sys/epoll.h>
#include <utility>

namespace clockwork
{
namespace
{
// Alias for the C type to keep style consistent.
using EPollEvent = struct epoll_event;

template <typename To, typename From>
To saturate_cast(From value)
{
  if (value > std::numeric_limits<To>::max())
  {
    return std::numeric_limits<To>::max();
  }
  if (value < std::numeric_limits<To>::lowest())
  {
    return std::numeric_limits<To>::lowest();
  }
  return static_cast<To>(value);
}
} // namespace

EPollManager::EPollManager(jewels::memory::MemoryResource memres)
  : epoll_(::epoll_create1(EPOLL_CLOEXEC)), callbacks_(memres)
{
  if (!epoll_)
  {
    throw std::runtime_error("failed to create epoll instance");
  }
}

jewels::expected<void, jewels::filesystem::ErrorCode>
EPollManager::add(int efd, uint32_t events, const std::shared_ptr<AbstractEPollCallback>& callback)
{
  EPollEvent event{.events = events, .data = {.fd = efd}};
  event.events = events;
  event.data.fd = efd;
  // Lock callbacks_ before doing the epoll_ctl as otherwise a separate thread running epoll_wait might immediately wake
  // for this fd but find that callbacks_[fd] hasn't been populated yet.
  const std::unique_lock lock(mutex_);
  if (::epoll_ctl(*epoll_, EPOLL_CTL_ADD, efd, &event) == -1)
  {
    return jewels::unexpected(jewels::filesystem::ErrorCode{errno});
  }
  callbacks_[efd] = callback;
  return {};
}

void EPollManager::remove(int efd)
{
  bool removed_map = false;
  int removed_errno = 0;
  {
    const std::unique_lock lock(mutex_);
    if (callbacks_.erase(efd) > 0)
    {
      removed_map = true;
    }
    if (::epoll_ctl(*epoll_, EPOLL_CTL_DEL, efd, nullptr) == -1)
    {
      removed_errno = errno;
    }
  }
  if (!removed_map || removed_errno != 0)
  {
    jewels::log_cerr_warn(
      "removal of fd failed (map exists: {}, epoll_ctl error: '{}')",
      removed_map,
      jewels::filesystem::ErrorCode(removed_errno));
  }
}

jewels::expected<void, jewels::filesystem::ErrorCode> EPollManager::wait(std::chrono::milliseconds timeout)
{
  constexpr size_t max_events = 10;

  std::array<EPollEvent, max_events> events{};
  const int timeout_ms = (timeout.count() < 0 ? -1 : saturate_cast<int>(timeout.count()));

  const int result = ::epoll_wait(*epoll_, events.data(), events.size(), timeout_ms);
  if (result == -1)
  {
    return jewels::unexpected(jewels::filesystem::ErrorCode{errno});
  }
  if (result > 0)
  {
    const size_t recv = std::min(events.size(), static_cast<size_t>(result));
    for (size_t idx = 0; idx < recv; idx++)
    {
      const auto& event = events.at(idx);
      std::shared_ptr<AbstractEPollCallback> callback;
      {
        const std::unique_lock lock(mutex_);
        auto callback_it = callbacks_.find(event.data.fd);
        if (callback_it != callbacks_.end())
        {
          callback = callback_it->second;
        }
      }
      if (callback)
      {
        callback->notify(*this, event.data.fd, event.events);
      }
    }
  }
  return {};
}

} // namespace clockwork
