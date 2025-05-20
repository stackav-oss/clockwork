// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/aligned_pointer.hh"
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
class Slot
{
public:
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
  explicit Slot(AlignedPtr<slot_alignment> ptr, size_t message_size) noexcept;

  /// Access the Pinion header.
  /// @{
  [[nodiscard]] jewels::memory::ObjectPtr<const Header> header() const noexcept;
  [[nodiscard]] jewels::memory::ObjectPtr<Header> header() noexcept;
  /// @}

  /// Access the message as bytes.
  /// @{
  [[nodiscard]] std::span<const std::byte> message() const noexcept;
  [[nodiscard]] std::span<std::byte> message() noexcept;
  /// @}

  /// Access the underlying bytes for the entire slot.
  /// @{
  [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
  [[nodiscard]] std::span<std::byte> bytes() noexcept;
  /// @}

  std::array<std::span<std::byte>, 2UL> headers_footers() noexcept;

private:
  /// Span over the entire slot (excludes padding at the end for alignment).
  std::span<std::byte> bytes_;

  /// Size of the message payload.
  size_t message_size_;
};

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
