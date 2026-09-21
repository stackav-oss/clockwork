// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string>

namespace clockwork::pinion
{
class PublisherHandle;

///
/// Common base class for pinion channels
///
class AbstractChannel : public AbstractEPollCallback
{
public:
  enum class Role : uint8_t
  {
    publisher,
    subscriber,
  };
  WISE_ENUM_CLASS_MEMBER(
    (ResumeBehavior, uint8_t),
    no_resume,   // Channels will not be reestablished
    dirty_resume // Reestablish channels. Previously enqueued "dirty" data (i.e. data published that has not yet been
                 // read by subscribers when the connection is disconnected) will be read upon reconnection.
  )
  WISE_ENUM_CLASS_MEMBER(
    (Error, uint8_t),
    fatal,  // Assorted failures with no clear resolution
    dirty,  // Shm file exists but can't be resumed
    missing // Shm file doesn't exist and creating it wasn't attempted
  )

  AbstractChannel() = default;
  ~AbstractChannel() override;
  AbstractChannel(const AbstractChannel&) = delete;
  AbstractChannel& operator=(const AbstractChannel&) = delete;
  AbstractChannel(AbstractChannel&&) = default;
  AbstractChannel& operator=(AbstractChannel&&) = default;

  /// Get the layout of the subscribed channel.
  [[nodiscard]] virtual const BufferLayout& layout() const noexcept = 0;

  /// Get the available range of messages at the time of this call.
  /// It's possible that immediately after calling this, the oldest
  /// message is already written over.  It is important to take this
  /// into consideration before reading.  Subscribers are responsible
  /// for determining which messages in this range they should
  /// consume.
  /// @return A range of available messages.
  [[nodiscard]] virtual std::ranges::subrange<SlotRef> available() const = 0;

  ///
  /// Returns the number of messages that have been published on the channel
  ///
  [[nodiscard]] virtual size_t get_publish_count() const noexcept = 0;

  /// Adds the given observer to the channel's notification list.  For Publishers, this means in-process
  /// notifications while Subscribers forward socket notifications to the observer.
  /// @param observer the observer to add
  /// @return true if the observer was added, false otherwise (likely the observer collection is full)
  [[nodiscard]] virtual bool add_observer(jewels::memory::ObjectPtr<Observer> observer) noexcept = 0;

  /// Get the file descriptor of the unix socket
  /// @return -1 if the socket is closed
  [[nodiscard]] virtual int socket() const noexcept = 0;

  /// Do any additional handshaking. Return true when ready.
  [[nodiscard]] virtual bool handshake() = 0;

  /// Get the socket namespace of the channel
  [[nodiscard]] virtual const std::pmr::string& scope() const noexcept = 0;

  /// Get the shm filename (not full path, usually a uuid) of the channel
  [[nodiscard]] virtual const std::pmr::string& identifier() const noexcept = 0;

  /// Get the human readable channel name of the channel
  [[nodiscard]] virtual const std::pmr::string& name() const noexcept = 0;
};

// NOLINTNEXTLINE(fuchsia-multiple-inheritance) Observer is pure virtual
class AbstractPublisher : public virtual AbstractChannel, protected Observer
{
public:
  ///
  /// Return a reference to the local publisher handle
  /// Throws if called after `extract_publisher()`
  ///
  virtual PublisherHandle& publisher() = 0;

  ///
  /// Moves the internal publisher to the caller, allowing the caller to exclusively own it.  As a result publisher
  /// based APIs on this class are disabled.
  /// @return the publisher handle or an error if it was already extracted
  ///
  virtual jewels::expected<PublisherHandle, jewels::MonoError> extract_publisher() noexcept = 0;

  ///
  /// For use by the event loop, accepts pending connections on the listening socket
  /// @return true if all pending connections could be accepted, false if any were rejected
  ///
  [[nodiscard]] virtual bool on_connect_pending() = 0;

  ///
  /// Returns the number of clients connected to the socket
  /// @note This doesn't poll the sockets to ensure they are still connected
  ///
  [[nodiscard]] virtual size_t num_clients() const noexcept = 0;

protected:
  // Provides access to the reserve functions
  friend PublisherHandle;

  /// Reserve space for the next message(s).
  /// @note There should only ever be one publisher calling this method.
  /// @note After calling reserve, either commit or discard must be called before calling reserve again.
  [[nodiscard]] virtual jewels::expected<PublisherReservation, ReserveError>
  reserve(size_t count, bool connected) noexcept = 0;
};

class AbstractSubscriber : public virtual AbstractChannel
{
public:
  /// Disable notifications, e.g. by closing the notification socket.  This cannot be undone but removes overhead (and
  /// warning messages) for manually polled channels.
  virtual void disable_notifications() = 0;

  /// Test whether the channel is connected to the publisher
  /// @return True if the channel is publisher or is connected to the publisher
  [[nodiscard]] virtual bool is_connected() const noexcept = 0;
};

///
/// This class provides a move-only wrapper for shared_ptr<AbstractPublisher> to help maintain unique ownership and
/// prevent accidentally creating multiple publishers on a channel.  It also serves as a compatibility shim for the
/// legacy channel interface.
///
/// It must be defined here due to restrictions put in place by the build system.
///
class PublisherHandle
{
public:
  explicit PublisherHandle(jewels::memory::ObjectPtr<AbstractPublisher> publisher);

  ~PublisherHandle() = default;
  PublisherHandle(PublisherHandle&& other) = default;
  PublisherHandle(const PublisherHandle&) = delete;
  void operator=(const PublisherHandle&) = delete;
  void operator=(PublisherHandle&&) = delete;

  /// Get the layout of the underlying buffer.
  [[nodiscard]] const BufferLayout& layout() const noexcept;

  /// Provides the number of messages that have been published on the channel so far.
  /// Currently only needed to validate that no messages have been published
  [[nodiscard]] size_t get_publish_count() const noexcept;

  /// Add an observer (e.g., a subscriber) to be notified whenever a
  /// new message is committed.
  /// @note This does not take ownership of the observer.
  /// @note Can only add observers if there is reserved space.  If
  /// full, an observer will not be added.
  /// @param observer The observer to notify.
  /// @return True if added and false otherwise.
  [[nodiscard]] bool add_observer(jewels::memory::ObjectPtr<Observer> observer) noexcept;

  /// Reserve space for the next message.
  /// @note There should only ever be one publisher calling this
  /// method.
  [[nodiscard]] jewels::expected<PublisherReservation, ReserveError> reserve(bool connected = true) noexcept;

  [[nodiscard]] jewels::expected<PublisherReservation, ReserveError> reserve(size_t count) noexcept;

  [[nodiscard]] jewels::expected<PublisherReservation, ReserveError> reserve(size_t count, bool connected) noexcept;

private:
  jewels::memory::ObjectPtr<AbstractPublisher> publisher_;
};
} // namespace clockwork::pinion
