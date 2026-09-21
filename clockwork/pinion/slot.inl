// IWYU pragma: private, include "clockwork/pinion/slot.hh"
#pragma once

#include "clockwork/pinion/slot.hh"

#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/aligned_pointer.hh"
#include "clockwork/pinion/device_ptr.hh"
#include "jewels/math/power_of_two.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/meta/type_traits.hh"
#include "jewels/std/expected.hh"

#include <array>
#include <cstddef>
#include <span>
#include <type_traits>

namespace clockwork::pinion
{

namespace detail
{

template <class Type>
jewels::memory::ObjectPtr<Type> marshal_as(std::span<ByteMatchingConstnessOf<Type>, sizeof(Type)> bytes) noexcept
{
  return start_lifetime_as<Type>(bytes);
}

} // namespace detail

template <typename T>
BaseSlot<T>::BaseSlot(
  AlignedPtr<T, slot_alignment> ptr, size_t message_size, DevicePtrFactory* dev_ptr_factory) noexcept
  : bytes_{std::span<T>{ptr.get(), slot_size(message_size)}},
    message_size_{message_size},
    dev_ptr_factory_(dev_ptr_factory)
{
}

template <typename T>
BaseSlot<T>::BaseSlot(const BaseSlot<std::remove_const_t<T>>& other) noexcept
  requires std::is_const_v<T>
  : bytes_(other.bytes_), message_size_(other.message_size_), dev_ptr_factory_(other.dev_ptr_factory_)
{
}

template <typename T>
jewels::memory::ObjectPtr<typename BaseSlot<T>::template MaybeConst<Header>> BaseSlot<T>::header() const noexcept
{
  return detail::marshal_as<MaybeConst<Header>>(bytes_.template subspan<header_offset, sizeof(Header)>());
}

template <typename T>
std::span<T> BaseSlot<T>::message() const noexcept
{
  return bytes_.subspan(message_offset, message_size_);
}

template <typename T>
DevicePtr<typename BaseSlot<T>::template MaybeConst<void>> BaseSlot<T>::device_ptr() const noexcept
{
  if (dev_ptr_factory_ == nullptr)
  {
    return {};
  }
  return dev_ptr_factory_->make_device_ptr(message().size(), message().data());
}

template <typename T>
std::span<T> BaseSlot<T>::bytes() const noexcept
{
  return bytes_;
}

template <typename T>
std::array<std::span<T>, 2UL> BaseSlot<T>::headers_footers() const noexcept
{
  auto all_bytes = bytes();
  // Everything before the message.
  auto headers = all_bytes.subspan(0, message_offset);
  // Everything after the message.
  auto footers = all_bytes.subspan(message_offset + message_size_);
  return std::array{headers, footers};
}

template <class Type>
jewels::expected<jewels::memory::ObjectPtr<Type>, jewels::MonoError>
marshal_as(std::span<ByteMatchingConstnessOf<Type>> bytes) noexcept
{
  if (bytes.size() != sizeof(Type))
  {
    return jewels::unexpected{jewels::MonoError{}};
  }
  return unsafe_marshal_as<Type>(bytes.template first<sizeof(Type)>());
}

template <class Type>
jewels::memory::ObjectPtr<Type> unsafe_marshal_as(std::span<ByteMatchingConstnessOf<Type>> bytes) noexcept
{
  return start_lifetime_as<Type>(bytes.template first<sizeof(Type)>());
}

constexpr size_t trail_padding_offset(size_t message_size) noexcept
{
  return Slot::message_offset + message_size;
}

constexpr size_t trail_padding_size(size_t message_size) noexcept
{
  return slot_size(message_size) - trail_padding_offset(message_size);
}

constexpr size_t slot_size(size_t message_size) noexcept
{
  return jewels::math::round_up_to_power_of_two_multiple<Slot::slot_alignment>(trail_padding_offset(message_size));
}

} // namespace clockwork::pinion
