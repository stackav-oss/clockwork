// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/abstract_channel_factory.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/shm_channel.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>

namespace clockwork::pinion
{
class ShmSubscriber;
class ShmPublisher;

///
/// Convience tool for opening channels
///
class ShmChannelFactory : public AbstractChannelFactory
{
public:
  ~ShmChannelFactory() override;
  ShmChannelFactory(const ShmChannelFactory&) = delete;
  ShmChannelFactory(ShmChannelFactory&&) = default;
  ShmChannelFactory& operator=(const ShmChannelFactory&) = delete;
  ShmChannelFactory& operator=(ShmChannelFactory&&) = default;

  ///
  /// Create a factory using canonical values for the socket_ns and shm_dir, potentially including a namespace in the
  /// path to allow for creation of independent shm channel pools
  /// @param memres resource used for allocations, only used during construction
  /// @param nmsp optional namespace for pool isolation, must not include '/'
  ///
  static jewels::expected<ShmChannelFactory, jewels::MonoError> make(
    jewels::memory::MemoryResource memres,
    std::string_view nmsp = "",
    std::string_view root = "/dev/shm",
    ShmChannel::ResumeBehavior resume_behavior = ShmChannel::ResumeBehavior::dirty_resume);

  ///
  /// Create a factory that will hold common creation data
  /// @param memres resource used for allocations, only used during construction
  /// @param shm_dir directory to open the shared memory file in
  /// @param socket_ns prefix to apply to the name to create the socket address to listen on
  /// @param resume_behavior determines whether/how a channel can be reconnected

  ShmChannelFactory(
    jewels::memory::MemoryResource memres,
    jewels::filesystem::Directory shm_dir,
    std::string_view socket_ns,
    ShmChannel::ResumeBehavior resume_behavior);

  ///
  /// Attempt to open a shared memory channel using the common settings of this factory.
  /// @param uuid_str UUID string name of the channel, used as the filename (in shm_dir)
  /// @param channel_name Human readable channel name
  /// @param layout the BufferLayout to use for the channel's backing buffer
  /// @param max_subscribers maximum size of the in-process and socket observer collections
  ///@{
  jewels::expected<std::shared_ptr<AbstractPublisher>, pinion::ShmChannel::Error> open_publisher(
    std::string_view uuid_str,
    std::string_view channel_name,
    const BufferLayout& layout,
    size_t max_subscribers) override;
  jewels::expected<std::shared_ptr<AbstractSubscriber>, pinion::ShmChannel::Error> open_subscriber(
    std::string_view uuid_str,
    std::string_view channel_name,
    const BufferLayout& layout,
    size_t max_subscribers) override;
  jewels::expected<std::shared_ptr<AbstractSubscriber>, pinion::ShmChannel::Error>
  open_spy(std::string_view uuid_str, std::string_view channel_name, const BufferLayout& layout, size_t max_subscribers)
    override;

  jewels::expected<std::shared_ptr<ShmPublisher>, pinion::ShmChannel::Error> open_shm_publisher(
    std::string_view uuid_str, std::string_view channel_name, const BufferLayout& layout, size_t max_subscribers);
  jewels::expected<std::shared_ptr<ShmSubscriber>, pinion::ShmChannel::Error> open_shm_subscriber(
    std::string_view uuid_str, std::string_view channel_name, const BufferLayout& layout, size_t max_subscribers);
  jewels::expected<std::shared_ptr<ShmSubscriber>, pinion::ShmChannel::Error> open_shm_spy(
    std::string_view uuid_str, std::string_view channel_name, const BufferLayout& layout, size_t max_subscribers);
  ///@}

  ///
  /// Gets the socket namespace
  ///
  [[nodiscard]] const std::pmr::string& socket_ns() const noexcept;

  ///
  /// Gets the internally held shm directory
  ///
  jewels::filesystem::Directory& directory() noexcept;

  ///
  /// Gets the resume behavior
  ///
  [[nodiscard]] ShmChannel::ResumeBehavior resume_behavior() const noexcept;

private:
  template <typename T>
  jewels::expected<std::shared_ptr<T>, pinion::ShmChannel::Error>
  to_shared(jewels::expected<T, pinion::ShmChannel::Error>&& obj);

  jewels::memory::MemoryResource memres_;
  jewels::filesystem::Directory shm_dir_;
  std::pmr::string socket_ns_;
  ShmChannel::ResumeBehavior resume_behavior_;
};

} // namespace clockwork::pinion
