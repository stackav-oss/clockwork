// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/detail/unix_socket.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/shm_channel.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/filesystem/mmap_region.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork::pinion
{
///
/// Represents a shared memory channel capable of publishing.
/// It also allows local-only subscriber handles to be created that will use lighter-weight publish notifications.
///
// NOLINTNEXTLINE(fuchsia-multiple-inheritance) shared_from_this is non-interface multi-inherited but done via diamond
class ShmPublisher : public ShmChannel, public AbstractPublisher
{
public:
  /// Minimum debug logging message interval
  static constexpr auto min_log_cerr_interval = std::chrono::seconds(1);

  /// Attempt to open the shared memory buffer file and create a channel around it.  If the file doesn't exist it will
  /// be created, but if it exists and is an invalid size then Error::dirty is returned.
  /// @param memres resource used for allocations, only used during construction
  /// @param shm_dir directory to open the shared memory file in
  /// @param socket_ns prefix to apply to the name to create the socket address to listen on
  /// @param filename UUID string name of the channel, used as the filename (in shm_dir)
  /// @param channel_name Human readable channel name
  /// @param layout Pinion buffer layout
  /// @param max_observers maximum size of the in-process observer collection
  /// @param max_clients maximum number of the socket connections
  /// @param resume_behavior determines whether/how a channel can be reconnected
  static jewels::expected<std::shared_ptr<ShmPublisher>, Error> open(
    jewels::memory::MemoryResource memres,
    const jewels::filesystem::Directory& shm_dir,
    std::string_view socket_ns,
    std::string_view filename,
    std::string_view channel_name,
    const BufferLayout& layout,
    size_t max_observers,
    size_t max_clients,
    ResumeBehavior resume_behavior);

  /// public for make_shared, use open() instead
  ShmPublisher(
    jewels::memory::MemoryResource memres,
    std::string_view socket_ns,
    std::string_view filename,
    std::string_view channel_name,
    BufferPtr buffer,
    jewels::filesystem::MMapRegion map,
    UnixSocket socket,
    size_t max_observers,
    size_t max_clients,
    ResumeBehavior resume_behavior);

  ~ShmPublisher() override = default;
  ShmPublisher(const ShmPublisher&) = delete;
  ShmPublisher(ShmPublisher&&) = delete;
  ShmPublisher& operator=(const ShmPublisher&) = delete;
  ShmPublisher& operator=(ShmPublisher&&) = delete;

  ///
  /// Adds the given observer to the internal PublisherHandle, if it hasn't been extracted
  /// @param observer the observer to add
  /// @return true if observer was added, false otherwise (e.g. publisher was extracted or collection full)
  ///
  [[nodiscard]] bool add_observer(jewels::memory::ObjectPtr<Observer> observer) noexcept override;

  ///
  /// Return a reference to the local publisher handle
  /// Throws if called after `extract_publisher()`
  ///
  PublisherHandle& publisher() override;

  ///
  /// Moves the internal publisher to the caller, allowing the caller to exclusively own it.  As a result publisher
  /// based APIs on this class are disabled.
  /// @return the publisher handle or an error if it was already extracted
  ///
  jewels::expected<PublisherHandle, jewels::MonoError> extract_publisher() noexcept override;

  ///
  /// For use by the event loop, accepts pending connections on the listening socket
  /// @return true if all pending connections could be accepted, false if any were rejected
  ///
  [[nodiscard]] bool on_connect_pending() override;

  ///
  /// Returns the number of clients connected to the socket
  /// @note This doesn't poll the sockets to ensure they are still connected
  ///
  [[nodiscard]] size_t num_clients() const noexcept override;

  ///
  /// Handles epoll notifications
  ///
  void notify(AbstractEPollManager& epoll, int efd, uint32_t events) override;

protected:
  /// Reserve space for the next message(s).
  /// @note There should only ever be one publisher calling this method.
  /// @note After calling reserve, either commit or discard must be called before calling reserve again.
  [[nodiscard]] jewels::expected<PublisherReservation, ReserveError>
  reserve(size_t count, bool connected) noexcept override;

  /// Notify all the channel's observers that messages were published
  void notify(const Observer::Event& event) override;

private:
  ///
  /// Bridge class that holds connected sockets and forwards notifications from the publisher to them
  ///
  struct SocketClients : Observer
  {
  public:
    SocketClients(
      jewels::memory::MemoryResource resource,
      std::string_view channel_name,
      size_t max_clients,
      std::shared_ptr<jewels::LogCerrThrottle> log_cerr_throttle);

    /// Add or discard the client connection.  Returns false if the client connection was discarded and closed.
    [[nodiscard]] bool add(UnixSocket client);

    /// Gets a notification from the publisher handle and forwards it to all sockets
    /// If a socket has disconnected it's pruned from the list here
    void notify(const Event& event) override;

    /// Returns the current number of sockets in the collection (which are assumed active)
    /// @note This doesn't poll the sockets to ensure they are still connected
    [[nodiscard]] size_t count() const noexcept;

    ///
    /// Close connections to disconnected clients to make space to accept new connections
    ///
    void close_disconnected_sockets();

    /// Close a client connection if it is disconnected
    /// @param[in] socket Socket to check
    /// @return True of the socket was closed.
    [[nodiscard]] bool close_socket_if_disconnected(UnixSocket& socket);

  private:
    /// Gets a notification from the publisher handle and forwards it to one socket
    /// If a socket has disconnected it's pruned from the list here
    /// @requires The caller must be holding `mutex_`
    /// @param[in] event Notify event
    /// @param[in] iter Iterator into the clients vector for the client to nofify
    /// @return Iterator for the next client to notify
    std::pmr::vector<UnixSocket>::iterator
    notify_client(const Event& event, std::pmr::vector<UnixSocket>::iterator iter);

    mutable std::mutex mutex_;
    std::pmr::string channel_name_;
    std::pmr::vector<UnixSocket> clients_;
    std::shared_ptr<jewels::LogCerrThrottle> log_cerr_throttle_;
  };

  std::optional<PublisherHandle> publisher_;
  std::shared_ptr<jewels::LogCerrThrottle> log_cerr_throttle_;
  jewels::memory::pmr_unique_ptr<SocketClients> socket_clients_;
  std::pmr::vector<jewels::memory::ObjectPtr<Observer>> observers_;
};

} // namespace clockwork::pinion
