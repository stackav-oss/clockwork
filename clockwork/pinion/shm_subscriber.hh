// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/detail/mmap_region.hh"
#include "clockwork/pinion/detail/unix_socket.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/shm_channel.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork::pinion
{

///
/// Class for a shared memory backed subscriber-only channel
///
class ShmSubscriber : public ShmChannel
{
public:
  /// Retry interval for reconnecting to the publisher
  static constexpr auto reconnect_interval_sec = std::chrono::seconds(1);

  enum class SubscriberRole : uint8_t
  {
    subscriber,
    spy
  };

  /// Attempt to open the shared memory buffer file and create a channel around it.  If the file doesn't exist, this
  /// will return Error::missing and should be retried if it's creation is racing with a publisher (which will create
  /// the file if needed).
  /// @param memres resource used for allocations, only used during construction
  /// @param shm_dir directory to open the shared memory file in
  /// @param socket_ns prefix to apply to the name to create the socket address to connect to
  /// @param name name of the channel, used as the filename (in shm_dir)
  /// @param num_slots number of slots to size the buffer for (see BufferLayout)
  /// @param message_size size of the messages in the channel (see BufferLayout)
  /// @param max_observers maximum number of observers to notify of socket events
  /// @param subscriber_role Indicates whether the subscriber is a normal subscriber or a spy
  /// @param resume_behavior determines whether/how a channel can be reconnected
  static jewels::expected<ShmSubscriber, Error> open(
    jewels::memory::MemoryResource memres,
    const jewels::filesystem::Directory& shm_dir,
    std::string_view socket_ns,
    std::string_view name,
    const BufferLayout& layout,
    size_t max_observers,
    SubscriberRole subscriber_role,
    ResumeBehavior resume_behavior);

  ///
  /// Attempt to add the observer to this channel
  /// These will be notified when new data is available, as signaled by the unix socket
  /// @return true if successful, false if not (collection is full)
  ///
  [[nodiscard]] bool add_observer(jewels::memory::ObjectPtr<Observer> observer) noexcept override;

  ///
  /// For use by the event loop, causes the channel to read from the socket and process any pending data
  /// @param[in] epoll EPoll manager driving the event loop
  /// @return true if successful, false if an error occurred and the socket needs to be closed and reconnected
  ///
  [[nodiscard]] bool on_readable(AbstractEPollManager& epoll);

  ///
  /// For use by the event loop, causes the channel to read from the timer_fd and try to reconnect to the publisher
  /// @param[in] epoll EPoll manager driving the event loop
  ///
  void on_reconnect_timer(AbstractEPollManager& epoll);

  ///
  /// Create a timer_fd to trigger callbacks until the channel can reconnect to the publisher
  /// @param[in] epoll EPoll manager driving the event loop
  /// @throws std::runtime_error on failure.
  ///
  void create_reconnect_timer(AbstractEPollManager& epoll);

  ///
  /// Handles epoll notifications
  ///
  void notify(AbstractEPollManager& epoll, int efd, uint32_t events) override;

  /// Test whether the channel is connected to the publisher
  /// @return True if the channel is publisher or is connected to the publisher
  [[nodiscard]] bool is_connected() const noexcept;

  /// Reconnect the channel to the publisher
  /// @return True if the channel was reconnected
  [[nodiscard]] bool reconnect();

  /// Get the timer descriptor used to poll for reconnects
  /// @return -1 if the socket is connected
  [[nodiscard]] int timer_descriptor() const noexcept;

private:
  explicit ShmSubscriber(
    jewels::memory::MemoryResource memres,
    std::string_view socket_ns,
    std::string_view name,
    SubscriberRole subscriber_role,
    ResumeBehavior resume_behavior,
    BufferPtr buffer,
    MMapRegion map,
    UnixSocket socket,
    size_t max_observers);

  /// Socket namespace
  std::pmr::string socket_ns_;

  /// Channel name
  std::pmr::string name_;

  /// Subscriber role
  SubscriberRole subscriber_role_;

  /// Channel observers
  std::pmr::vector<jewels::memory::ObjectPtr<Observer>> observers_;

  /// Time file descriptor used to schedule reconnect callbacks
  jewels::filesystem::FileDescriptor timer_fd_;
};

} // namespace clockwork::pinion
