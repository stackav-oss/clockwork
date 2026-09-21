// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/detail/unix_socket.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/filesystem/mmap_region.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <memory_resource>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>

namespace clockwork::pinion
{

///
/// Common base class for shared memory backed channels
///
class ShmChannel : public virtual AbstractChannel
{
public:
  using BufferPtr = jewels::memory::pmr_unique_ptr<Buffer>;

  /// Internal message used to communicate notifications between ShmPublisher and ShmSubscriber
  struct NotifyMsg
  {
    std::byte placeholder; /// Simply 0 so the packet is not empty
  };

  ~ShmChannel() override;
  ShmChannel(const ShmChannel&) = delete;
  ShmChannel& operator=(const ShmChannel&) = delete;
  ShmChannel(ShmChannel&&) noexcept = default;
  ShmChannel& operator=(ShmChannel&&) noexcept = default;

  /// Get the layout of the subscribed buffer.
  [[nodiscard]] const BufferLayout& layout() const noexcept override;

  /// Create a subscriber for the channel
  [[nodiscard]] std::ranges::subrange<SlotRef> available() const override;

  /// Returns the number of messages that have been published on the channel
  [[nodiscard]] size_t get_publish_count() const noexcept override;

  /// Create a subscriber for the channel
  [[nodiscard]] SubscriberHandle make_subscriber();

  /// Get the file descriptor of the unix socket
  /// @return -1 if the socket is unix closed
  [[nodiscard]] int socket() const noexcept override;

  /// Set the unix socket
  /// @param[in] socket Unix socket
  void set_socket(UnixSocket socket) noexcept;

  /// Get the underlying comms buffer pointer
  [[nodiscard]] jewels::memory::ObjectPtr<Buffer> buffer() const noexcept;

  /// Close the notification socket.
  void close_socket();

  /// Always retuns true as SHM channels connect fully at creation
  [[nodiscard]] bool handshake() override;

  /// Get the socket namespace of the channel
  [[nodiscard]] const std::pmr::string& socket_ns() const noexcept;
  [[nodiscard]] const std::pmr::string& scope() const noexcept override;

  /// Get the shm filename (not full path, usually a uuid) of the channel
  [[nodiscard]] const std::pmr::string& filename() const noexcept;
  [[nodiscard]] const std::pmr::string& identifier() const noexcept override;

  /// Get the human readable channel name of the channel
  [[nodiscard]] const std::pmr::string& channel_name() const noexcept;
  [[nodiscard]] const std::pmr::string& name() const noexcept override;

  /// Get the resume behavior of the channel
  [[nodiscard]] ResumeBehavior resume_behavior() const noexcept;

protected:
  explicit ShmChannel(
    jewels::memory::MemoryResource memres,
    BufferPtr buffer,
    jewels::filesystem::MMapRegion map,
    UnixSocket socket,
    std::string_view socket_ns,
    std::string_view filename,
    std::string_view channel_name,
    ResumeBehavior resume_behavior);

  /// Open a file in shared memory and map it into memory, returning the map and corresponding buffer if successful and
  /// an error if not.
  /// @param memres resource used for allocations, only used during construction
  /// @param shm_dir directory to open the shared memory file in
  /// @param filename uuid string of the channel, used as the filename (in shm_dir)
  /// @param num_slots number of slots to size the buffer for (see BufferLayout)
  /// @param message_size size of the messages in the channel (see BufferLayout)
  /// @param role signals what role the buffer is being used for.  Publishers will open as read/write and create the
  /// file if it doesn't exist.  Subscribers will fail with Error::missing if the shared file doesn't exist.
  /// @param resume_behavior determines whether/how a channel can be reconnected
  static jewels::expected<std::tuple<jewels::filesystem::MMapRegion, BufferPtr>, Error> open_buffer(
    jewels::memory::MemoryResource memres,
    const jewels::filesystem::Directory& shm_dir,
    std::string_view filename,
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
  jewels::filesystem::MMapRegion map_;
  BufferPtr buffer_;
  UnixSocket socket_;
  std::pmr::string socket_ns_;
  std::pmr::string filename_;
  std::pmr::string channel_name_;
  ResumeBehavior resume_behavior_;
};

} // namespace clockwork::pinion
