// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/detail/mmap_region.hh"
#include "clockwork/pinion/detail/unix_socket.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <wise_enum.h>

#include <cstdint>
#include <string_view>
#include <tuple>

namespace clockwork::pinion
{

///
/// Common base class for shared memory backed channels
///
class ShmChannel : public AbstractEPollCallback
{
public:
  using BufferPtr = jewels::memory::pmr_unique_ptr<Buffer>;
  WISE_ENUM_CLASS_MEMBER(
    (Error, uint8_t),
    fatal,  // Assorted failures with no clear resolution
    dirty,  // Shm file exists but can't be resumed
    missing // Shm file doesn't exist and creating is wasn't attempted
  )
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

  /// Internal message used to communicate notifications between ShmPublisher and ShmSubscriber
  struct NotifyMsg
  {
    uint64_t tail; /// The id / BufferIndex of the oldest valid message a time of publish
    uint64_t head; /// The id / BufferIndex of the published message
  };

  ~ShmChannel() override;
  ShmChannel(const ShmChannel&) = delete;
  ShmChannel& operator=(const ShmChannel&) = delete;
  ShmChannel(ShmChannel&&) = default;
  ShmChannel& operator=(ShmChannel&&) = default;

  /// Create a subscriber for the channel
  [[nodiscard]] SubscriberHandle make_subscriber();

  /// Adds the given observer to the channel's notification list.  For ShmPublishers, this means in-process
  /// notifications while ShmSubscribers forward socket notifications to the observer.
  /// @param observer the observer to add
  /// @return true if the observer was added, false otherwise (likely the observer collection is full)
  [[nodiscard]] virtual bool add_observer(jewels::memory::ObjectPtr<Observer> observer) noexcept = 0;

  /// Get the file descriptor of the unix socket
  /// @return -1 if the socket is unix closed
  [[nodiscard]] int socket() const noexcept;

  /// Set the unix socket
  /// @param[in] socket Unix socket
  void set_socket(UnixSocket socket) noexcept;

  /// Get the underlying comms buffer pointer
  [[nodiscard]] jewels::memory::ObjectPtr<Buffer> buffer() const noexcept;

  //// Close the notification socket.
  void close_socket();

  /// Get the resume behavior of the channel
  [[nodiscard]] ResumeBehavior resume_behavior() const noexcept;

protected:
  explicit ShmChannel(BufferPtr buffer, MMapRegion map, UnixSocket socket, ResumeBehavior resume_behavior);

  /// Open a file in shared memory and map it into memory, returning the map and corresponding buffer if successful and
  /// an error if not.
  /// @param memres resource used for allocations, only used during construction
  /// @param shm_dir directory to open the shared memory file in
  /// @param name name of the channel, used as the filename (in shm_dir)
  /// @param num_slots number of slots to size the buffer for (see BufferLayout)
  /// @param message_size size of the messages in the channel (see BufferLayout)
  /// @param role signals what role the buffer is being used for.  Publishers will open as read/write and create the
  /// file if it doesn't exist.  Subscribers will fail with Error::missing if the shared file doesn't exist.
  /// @param resume_behavior determines whether/how a channel can be reconnected
  static jewels::expected<std::tuple<MMapRegion, BufferPtr>, Error> open_buffer(
    jewels::memory::MemoryResource memres,
    const jewels::filesystem::Directory& shm_dir,
    std::string_view name,
    const BufferLayout& layout,
    Role role,
    ShmChannel::ResumeBehavior resume_behavior);

  /// Create and setup a unix socket used for IPC notifications.  The listen / connect address will be `socket_ns+name`
  /// in the abstract namespace. Upon successful return, publishers will be bound but not listening / subscriber will
  /// have connected to the address
  ///
  /// For publishers, an explicit call to listen is required once the shared memory buffer has been created.
  ///
  /// @param socket_ns a prefix applied to name to generate the socket name
  /// @param name channel name appended to socket_ns to generate the socket name
  /// @param role signals what role the socket will play.  Publishers listen, subscribers connect
  static jewels::expected<UnixSocket, Error> open_socket(std::string_view socket_ns, std::string_view name, Role role);

private:
  MMapRegion map_;
  BufferPtr buffer_;
  UnixSocket socket_;
  ResumeBehavior resume_behavior_;
};

} // namespace clockwork::pinion
