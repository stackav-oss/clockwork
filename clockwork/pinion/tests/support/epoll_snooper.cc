// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/tests/support/epoll_snooper.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/std/expected.hh"

namespace clockwork::pinion
{

jewels::expected<void, jewels::filesystem::ErrorCode>
EPollSnooper::add(int efd, uint32_t /*events*/, const std::shared_ptr<AbstractEPollCallback>& callback)
{
  callbacks_[efd] = callback;
  fds_.push_back(efd);
  return {};
}

jewels::expected<void, jewels::filesystem::ErrorCode> EPollSnooper::modify(int /*efd*/, uint32_t /*events*/)
{
  return {};
}

void EPollSnooper::remove(int efd)
{
  callbacks_.erase(efd);
}

jewels::expected<void, jewels::filesystem::ErrorCode> EPollSnooper::wait(std::chrono::milliseconds /*timeout*/)
{
  return {};
}

std::optional<int> EPollSnooper::pop_fd()
{
  if (fds_.empty())
  {
    return std::nullopt;
  }
  auto efd = fds_.back();
  fds_.pop_back();
  return efd;
}

void EPollSnooper::notify(int efd, uint32_t events)
{
  callbacks_.at(efd)->notify(*this, efd, events);
}

} // namespace clockwork::pinion
