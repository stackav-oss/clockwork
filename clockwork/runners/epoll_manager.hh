// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_epoll_manager.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/memory_resource.hh"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <unordered_map>

namespace clockwork
{

///
/// A utility that manages generic callbacks tied to an underlying epoll instance
///
class EPollManager : public AbstractEPollManager
{
public:
  explicit EPollManager(jewels::memory::MemoryResource memres);

  ///
  /// Registers the given file descriptor with underlying epoll handle
  /// @param efd the file descriptor to watch for events on
  /// @param events the the bitmask of event types, see epoll_ctl(2)
  /// @param callback a reference to the associated callback.  This reference is kept until remove(fd) is called
  ///
  [[nodiscard]] jewels::expected<void, jewels::filesystem::ErrorCode>
  add(int efd, uint32_t events, const std::shared_ptr<AbstractEPollCallback>& callback) override;

  ///
  /// Modifies the given file descriptor. The callback reference is unchanged.
  /// @param efd the file descriptor to watch for events on
  /// @param events the the bitmask of event types, see epoll_ctl(2)
  ///
  [[nodiscard]] jewels::expected<void, jewels::filesystem::ErrorCode> modify(int efd, uint32_t events) override;

  ///
  /// Unregisters the given file descriptor with underlying epoll handle and releases the callback reference
  /// @param efd the file descriptor to watch for events on
  ///
  void remove(int efd) override;

  ///
  /// Waits once for events on the registers file descriptors and dispatches any received events to the associated
  /// callbacks
  /// @param timeout maximum time to wait for an event. negative values mean indefinitely (or until a signal)
  ///
  [[nodiscard]] jewels::expected<void, jewels::filesystem::ErrorCode> wait(std::chrono::milliseconds timeout) override;

private:
  std::mutex mutex_;
  jewels::filesystem::FileDescriptor epoll_;
  std::pmr::unordered_map<int, std::shared_ptr<AbstractEPollCallback>> callbacks_;
};

} // namespace clockwork
