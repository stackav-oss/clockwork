// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <memory>
#include <string_view>

namespace clockwork::pinion
{

///
/// Convience tool for opening channels
///
class AbstractChannelFactory
{
public:
  AbstractChannelFactory() = default;
  virtual ~AbstractChannelFactory();
  AbstractChannelFactory(const AbstractChannelFactory&) = default;
  AbstractChannelFactory(AbstractChannelFactory&&) = default;
  AbstractChannelFactory& operator=(const AbstractChannelFactory&) = default;
  AbstractChannelFactory& operator=(AbstractChannelFactory&&) = default;

  ///
  /// Attempt to open a shared memory channel with the specified role using the common settings of this factory
  /// @param role indicates if this is to be a Publisher or Subscriber
  /// @param identifier UUID string name of the channel, used as the filename (in shm_dir)
  /// @param name internal name of the channel, e.g. used as the filename in shm_dir
  /// @param layout the BufferLayout to use for the channel's backing buffer
  /// @param max_subscribers maximum size of the in-process and socket observer collections
  ///
  jewels::expected<std::shared_ptr<AbstractChannel>, AbstractChannel::Error> open(
    AbstractChannel::Role role,
    std::string_view identifier,
    std::string_view name,
    const BufferLayout& layout,
    size_t max_subscribers);

  ///
  /// Attempt to open a shared memory channel using the common settings of this factory.
  /// @param identifier UUID string name of the channel, used as the filename (in shm_dir)
  /// @param name Human readable channel name
  /// @param layout the BufferLayout to use for the channel's backing buffer
  /// @param max_subscribers maximum size of the in-process and socket observer collections
  ///@{
  virtual jewels::expected<std::shared_ptr<AbstractPublisher>, AbstractChannel::Error> open_publisher(
    std::string_view identifier, std::string_view name, const BufferLayout& layout, size_t max_subscribers) = 0;
  virtual jewels::expected<std::shared_ptr<AbstractSubscriber>, AbstractChannel::Error> open_subscriber(
    std::string_view identifier, std::string_view name, const BufferLayout& layout, size_t max_subscribers) = 0;
  virtual jewels::expected<std::shared_ptr<AbstractSubscriber>, AbstractChannel::Error>
  open_spy(std::string_view identifier, std::string_view name, const BufferLayout& layout, size_t max_subscribers) = 0;
  ///@}
};

///
/// A RAII registration of channel factories that allow passing a 'current' factory down the stack.
/// Primarily meant to provide the main/scaffolding factory (with the associated namespace etc) to multi-channel
/// subscriber cogs.
///
class ChannelFactoryContext
{
public:
  /// Constructor
  /// @param[in] factory Shared memory channel factory
  explicit ChannelFactoryContext(AbstractChannelFactory& factory);
  ~ChannelFactoryContext();
  ChannelFactoryContext(const ChannelFactoryContext&) = delete;
  ChannelFactoryContext(ChannelFactoryContext&&) = delete;
  ChannelFactoryContext& operator=(const ChannelFactoryContext&) = delete;
  ChannelFactoryContext& operator=(ChannelFactoryContext&&) = delete;

  static AbstractChannelFactory* get();

private:
  AbstractChannelFactory& factory_;
  ChannelFactoryContext* previous_;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): RAII managed trivial type
  static ChannelFactoryContext* current_;
};

} // namespace clockwork::pinion
