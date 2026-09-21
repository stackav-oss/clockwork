// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/aligned_pointer.hh"
#include "clockwork/pinion/device_ptr.hh"
#include "jewels/math/power_of_two.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/meta/call.hh"
#include "jewels/meta/type_traits.hh"
#include "jewels/std/expected.hh"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace clockwork::pinion
{

/// Pinion header.
struct Header
{
  /// Sequence number for the message.
  uint64_t sequence_number;

  /// Timestamp the message was published.
  int64_t publish_timestamp;

  /// Timestamp that the pinion slot was committed for transmission by the original publisher.
  int64_t source_commit_timestamp;

  /// Timestamp that the pinion slot was committed for transmission by the most recent publisher.
  /// This will be different from the original_timestamp when a message has passed through a bridge.
  int64_t latest_commit_timestamp;
};

/// An alias for a std::byte that matches the const qualification of another type.
template <class ConstnessToMatch>
using ByteMatchingConstnessOf =
  jewels::meta::Call<jewels::meta::ConditionalT<std::is_const_v<ConstnessToMatch>>, const std::byte, std::byte>;

/// A non-owning interface for the data in each buffer slot.  Each channel
/// buffer is divided into slots and each slot has multiple fields.
/// When messages are passed to Cogs, the message bytes will be
/// converted to a typed reference.
template <typename T>
class BaseSlot
{
public:
  template <typename U>
  using MaybeConst = std::conditional_t<std::is_const_v<T>, const U, U>;

  /// Alignment of the entire slot.
  static constexpr auto slot_alignment{64UL};

  /// Alignment of each field within the slot in order.
  /// @{
  static constexpr auto header_alignment{slot_alignment};
  static constexpr auto message_alignment{slot_alignment};
  /// @}

  /// Byte offsets to all fields that are not depending on message size.
  /// @{
  static constexpr auto header_offset{0UL};
  static constexpr auto message_offset{
    jewels::math::round_up_to_power_of_two_multiple<message_alignment>(header_offset + sizeof(Header))};
  /// @}

  /// Padding that is not depending on message size.
  /// @{
  static constexpr auto header_to_message_padding{message_offset - (header_offset + sizeof(Header))};
  /// @}

  /// Create a slot from bytes and a message size.
  /// @param ptr A pointer to an underlying buffer.
  /// @param message_size The size of the message payload.
  explicit BaseSlot(
    AlignedPtr<T, slot_alignment> ptr, size_t message_size, DevicePtrFactory* dev_ptr_factory = nullptr) noexcept;

  // NOLINTNEXTLINE(google-explicit-constructor) allow implicit conversion to const
  BaseSlot(const BaseSlot<std::remove_const_t<T>>& other) noexcept
    requires std::is_const_v<T>;

  BaseSlot(const BaseSlot&) noexcept = default;
  BaseSlot(BaseSlot&&) noexcept = default;
  BaseSlot& operator=(const BaseSlot&) noexcept = default;
  BaseSlot& operator=(BaseSlot&&) noexcept = default;
  ~BaseSlot() noexcept = default;

  /// Access the Pinion header.
  [[nodiscard]] jewels::memory::ObjectPtr<MaybeConst<Header>> header() const noexcept;

  /// Access the message as bytes.
  [[nodiscard]] std::span<T> message() const noexcept;

  /// Get a pointer to device memory.  May be null
  [[nodiscard]] DevicePtr<MaybeConst<void>> device_ptr() const noexcept;

  /// Access the underlying bytes for the entire slot.
  [[nodiscard]] std::span<T> bytes() const noexcept;

  [[nodiscard]] std::array<std::span<T>, 2UL> headers_footers() const noexcept;

private:
  friend class BaseSlot<const T>;

  /// Span over the entire slot (excludes padding at the end for alignment).
  std::span<T> bytes_;

  /// Size of the message payload.
  size_t message_size_;

  /// Optional functor for creating a view of this slot on a device
  DevicePtrFactory* dev_ptr_factory_;
};

using Slot = BaseSlot<std::byte>;
using ConstSlot = BaseSlot<const std::byte>;

/// Get the byte offset to the trail padding
/// @param message_size Size of the message payload in bytes.
/// @return Offset in bytes.
constexpr size_t trail_padding_offset(size_t message_size) noexcept;

/// Get the size of the trail padding in bytes
/// @param message_size Size of the message payload in bytes.
/// @return Padding size in bytes.
constexpr size_t trail_padding_size(size_t message_size) noexcept;

/// Get the size of the slot in bytes (includes trail padding).
/// @param message_size Size of the message payload in bytes.
/// @return Size in bytes.
constexpr size_t slot_size(size_t message_size) noexcept;

/// Used to convert a slot payload to a typed message.
/// @param bytes A span of bytes representing a valid MessageType object.
/// @return A pointer to the MessageType object
template <class Type>
jewels::expected<jewels::memory::ObjectPtr<Type>, jewels::MonoError>
marshal_as(std::span<ByteMatchingConstnessOf<Type>> bytes) noexcept;

/// Used to convert a slot payload to a typed message.
/// @note Does not check size.  Assumes marshal_as would return a
/// valid expected.
/// @param bytes A span of bytes representing a valid MessageType object.
/// @return A pointer to the MessageType object
template <class Type>
jewels::memory::ObjectPtr<Type> unsafe_marshal_as(std::span<ByteMatchingConstnessOf<Type>> bytes) noexcept;

} // namespace clockwork::pinion

#include "clockwork/pinion/slot.inl"
