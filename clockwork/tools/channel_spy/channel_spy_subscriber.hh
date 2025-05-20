// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/shm_subscriber.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/tools/channel_spy/types.hh"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace clockwork::tools
{

/// Class to subscribe to a channel and call a callback function for messages it receives.
///
/// This subscriber always reads the newest message on the channel rather than trying to iterate over
/// all messages. If messages are coming faster than they can be read messages will be skipped. The
/// message received in each callback is always the lastest message on the channel.
class ChannelSpySubscriber
{
  /// Generic callback function
  using GenericCallbackFunction = std::function<void(
    uint64_t sequence_number,
    int64_t message_time,
    std::span<const std::byte> data,
    const pinion::SubscriberHandle& subscriber_handle,
    pinion::BufferIterator buffer_iter)>;

public:
  /// Make a channel spy subscriber to receive raw message data
  /// @param[in] shm_dir Shared memory directory
  /// @param[in] socket_ns Pinion socket namespace
  /// @param[in] name Name of the channel file in shm_dir (UUID string)
  /// @param[in] num_slots Number of pinion buffer slots
  /// @param[in] message_size Pinion buffer message size
  /// @param[in] callback_fn Raw message data callback function
  [[nodiscard]] static std::unique_ptr<ChannelSpySubscriber> make_subscriber(
    std::string_view shm_dir,
    std::string_view socket_ns,
    std::string_view name,
    size_t num_slots,
    size_t message_size,
    RawMessageCallback callback_fn);

  /// Make a channel spy subscriber to receive python callbacks
  /// @param[in] shm_dir Shared memory directory
  /// @param[in] socket_ns Pinion socket namespace
  /// @param[in] name Name of the channel file in shm_dir (UUID string)
  /// @param[in] num_slots Number of pinion buffer slots
  /// @param[in] message_size Pinion buffer message size
  /// @param[in] callback_fn Message callback function
  [[nodiscard]] static std::unique_ptr<ChannelSpySubscriber> make_subscriber(
    std::string_view shm_dir,
    std::string_view socket_ns,
    std::string_view name,
    size_t num_slots,
    size_t message_size,
    PythonCallback callback_fn);

  /// Make a channel spy subscriber to receive deserialized messages
  /// @tparam MessageType Subscribed message type
  /// @param[in] shm_dir Shared memory directory
  /// @param[in] socket_ns Pinion socket namespace
  /// @param[in] name Name of the channel file in shm_dir (UUID string)
  /// @param[in] num_slots Number of pinion buffer slots
  /// @param[in] callback_fn Message callback function
  template <typename MessageType>
  [[nodiscard]] static std::unique_ptr<ChannelSpySubscriber> make_subscriber(
    std::string_view shm_dir,
    std::string_view socket_ns,
    std::string_view name,
    size_t num_slots,
    DeserializedMessageCallback<MessageType> callback_fn)
    requires(TappyType<MessageType> || TachyonType<MessageType>);

  ~ChannelSpySubscriber() noexcept = default;
  ChannelSpySubscriber(const ChannelSpySubscriber&) = delete;
  ChannelSpySubscriber& operator=(const ChannelSpySubscriber&) = delete;
  ChannelSpySubscriber(ChannelSpySubscriber&&) = delete;
  ChannelSpySubscriber& operator=(ChannelSpySubscriber&&) = delete;

  /// Poll the channel and issue a callback if a new message is found
  void poll();

private:
  /// Make a channel spy subscriber with a generic callback function
  /// @param[in] shm_dir Shared memory directory
  /// @param[in] socket_ns Pinion socket namespace
  /// @param[in] name Name of the channel file in shm_dir (UUID string)
  /// @param[in] num_slots Number of pinion buffer slots
  /// @param[in] message_size Pinion buffer message size
  /// @param[in] callback_fn Raw message data callback function
  [[nodiscard]] static std::unique_ptr<ChannelSpySubscriber> make_subscriber(
    std::string_view shm_dir,
    std::string_view socket_ns,
    std::string_view name,
    size_t num_slots,
    size_t message_size,
    GenericCallbackFunction callback_fn);

  /// Private constructor, use make_subscription to create an instance
  /// @param[in] subscriber Pinion shared memory channel subscriber
  /// @param[in] subscriber_handle Pinion subscriber handle
  /// @param[in] callback_fn Message callback function
  ChannelSpySubscriber(
    std::shared_ptr<pinion::ShmSubscriber> subscriber,
    pinion::SubscriberHandle subscriber_handle,
    GenericCallbackFunction callback_fn);

  /// Pinion shared memory channel subscriber
  std::shared_ptr<pinion::ShmSubscriber> subscriber_;

  /// Pinion subscriber handle
  pinion::SubscriberHandle subscriber_handle_;

  /// Last message buffer iterator
  pinion::BufferIterator last_iter_;

  /// Callback function
  GenericCallbackFunction callback_fn_;
};

} // namespace clockwork::tools

#include "clockwork/tools/channel_spy/channel_spy_subscriber.inl"
