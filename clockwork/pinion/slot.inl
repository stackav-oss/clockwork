// IWYU pragma: private, include "clockwork/pinion/slot.hh"
#pragma once

#include "clockwork/pinion/slot.hh"

#include "clockwork/memory/start_lifetime_as.hh"
#include "jewels/math/power_of_two.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/meta/type_traits.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <span>

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
