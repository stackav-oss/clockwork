// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/filesystem/error_code.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/std/expected.hh"

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

namespace clockwork
{
class AbstractEPollManager;
template <typename Callback>
class EPollCallbackImpl;

///
/// Abstract interface for an object that registers events with epoll
///
struct AbstractEPollCallback : public std::enable_shared_from_this<AbstractEPollCallback>
{
  AbstractEPollCallback() = default;

  AbstractEPollCallback(const AbstractEPollCallback&) = default;
  AbstractEPollCallback(AbstractEPollCallback&&) = default;
  AbstractEPollCallback& operator=(const AbstractEPollCallback&) = default;
  AbstractEPollCallback& operator=(AbstractEPollCallback&&) = default;

  virtual ~AbstractEPollCallback();

  ///
  /// Called when epoll indicates that the registered file descriptor has pending events
  /// @param epoll the AbstractEPollManager that is notifying this callback
  /// @param efd the file descriptor that has the event
  /// @param events the bitmask of events detected by epoll
  ///
  virtual void notify(AbstractEPollManager& epoll, int efd, uint32_t events) = 0;

  ///
  /// Move the provided lambda / functor into a new callback
  ///
  template <typename Callback>
  static std::shared_ptr<AbstractEPollCallback> make(jewels::memory::MemoryResource memres, Callback&& callback)
  {
    return jewels::memory::make_pmr_shared<EPollCallbackImpl<Callback>>(memres, std::forward<Callback>(callback));
  }
};

///
/// A canonical AbstractEPollCallback implementation that binds a lambda
///
template <typename Callback>
class EPollCallbackImpl : public AbstractEPollCallback
{
public:
  ///
  /// Move the provided lambda / functor into this callback
  ///
  explicit EPollCallbackImpl(Callback&& callback)
    : callback_(std::move(callback))
  {
  }

  ///
  /// Dispatch the notification to the lambda / functor
  ///
  void notify(AbstractEPollManager& epoll, int efd, uint32_t events) override
  {
    callback_(epoll, efd, events);
  }

private:
  Callback callback_;
};

///
/// A utility that manages generic callbacks tied to an underlying epoll instance
///
class AbstractEPollManager
{
public:
  AbstractEPollManager() = default;

  AbstractEPollManager(const AbstractEPollManager&) = default;
  AbstractEPollManager(AbstractEPollManager&&) = default;
  AbstractEPollManager& operator=(const AbstractEPollManager&) = default;
  AbstractEPollManager& operator=(AbstractEPollManager&&) = default;

  virtual ~AbstractEPollManager();

  ///
  /// Registers the given file descriptor with underlying epoll handle
  /// @param efd the file descriptor to watch for events on
  /// @param events the the bitmask of event types, see epoll_ctl(2)
  /// @param callback a reference to the associated callback.  This reference is kept until remove(fd) is called
  ///
  [[nodiscard]] virtual jewels::expected<void, jewels::filesystem::ErrorCode>
  add(int efd, uint32_t events, const std::shared_ptr<AbstractEPollCallback>& callback) = 0;

  ///
  /// Modifies the given file descriptor. The callback reference is unchanged.
  /// @param efd the file descriptor to watch for events on
  /// @param events the the bitmask of event types, see epoll_ctl(2)
  ///
  [[nodiscard]] virtual jewels::expected<void, jewels::filesystem::ErrorCode> modify(int efd, uint32_t events) = 0;

  ///
  /// Unregisters the given file descriptor with underlying epoll handle and releases the callback reference
  /// @param efd the file descriptor to watch for events on
  ///
  virtual void remove(int efd) = 0;

  ///
  /// Waits once for events on the registers file descriptors and dispatches any received events to the associated
  /// callbacks
  /// @param timeout maximum time to wait for an event. negative values mean indefinitely (or until a signal)
  ///
  [[nodiscard]] virtual jewels::expected<void, jewels::filesystem::ErrorCode>
  wait(std::chrono::milliseconds timeout) = 0;
};

///
/// Interface to represent a type that can be registered as a callback with an epoll manager.
///
class EPollable
{
public:
  EPollable() = default;

  EPollable(const EPollable&) = delete;
  void operator=(const EPollable&) = delete;
  EPollable(EPollable&&) = default;
  EPollable& operator=(EPollable&&) = default;

  virtual ~EPollable() = default;

  ///
  /// Register with an epoll manager.
  /// @param manager The manager to register with.
  /// @return A valid expected on success or an error.
  ///
  [[nodiscard]] virtual jewels::expected<void, jewels::MonoError> register_with(AbstractEPollManager& manager) = 0;
};

} // namespace clockwork
