// IWYU pragma: private, include "jewels/container/tap/bitset.hh"
#pragma once

#include "jewels/container/tap/bitset.hh"

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"

#include <array>
#include <bit>
#include <cstddef>
#include <span>
#include <stdexcept>

namespace jewels::tap
{

template <size_t size_p>
constexpr size_t Bitset<size_p>::size() noexcept
{
  return bit_size;
}

template <size_t size_p>
jewels::BinaryOutcome Bitset<size_p>::test(jewels::Out<bool> value_out, size_t index) const noexcept
{
  if (!is_valid_index(index))
  {
    return jewels::failure;
  }
  *value_out = (byte_at(byte_index(index)) & bit_mask(index)) != 0U;
  return jewels::success;
}

template <size_t size_p>
constexpr bool Bitset<size_p>::test(size_t index) const
{
  if (!is_valid_index(index))
  {
    throw std::out_of_range{"Bitset::test: index out of range"};
  }
  return (byte_at(byte_index(index)) & bit_mask(index)) != 0U;
}

template <size_t size_p>
constexpr Bitset<size_p>& Bitset<size_p>::set(size_t index)
{
  return set(index, true);
}

template <size_t size_p>
constexpr jewels::BinaryOutcome Bitset<size_p>::try_set(size_t index) noexcept
{
  return try_set(index, true);
}

template <size_t size_p>
constexpr jewels::BinaryOutcome Bitset<size_p>::try_set(size_t index, bool value) noexcept
{
  if (!is_valid_index(index))
  {
    return jewels::failure;
  }
  const auto storage_index = byte_index(index);
  const auto mask = bit_mask(index);
  const auto current = byte_at(storage_index);
  storage_[storage_index] = static_cast<std::byte>(value ? (current | mask) : (current & ~mask));
  return jewels::success;
}

template <size_t size_p>
constexpr Bitset<size_p>& Bitset<size_p>::set(size_t index, bool value)
{
  if (jewels::fails(try_set(index, value)))
  {
    throw std::out_of_range{"Bitset::set: index out of range"};
  }
  return *this;
}

template <size_t size_p>
constexpr Bitset<size_p>& Bitset<size_p>::reset(size_t index)
{
  return set(index, false);
}

template <size_t size_p>
constexpr jewels::BinaryOutcome Bitset<size_p>::try_reset(size_t index) noexcept
{
  return try_set(index, false);
}

template <size_t size_p>
constexpr Bitset<size_p>& Bitset<size_p>::reset() noexcept
{
  for (auto& byte : storage_)
  {
    byte = std::byte{0};
  }
  return *this;
}

template <size_t size_p>
constexpr Bitset<size_p>& Bitset<size_p>::flip(size_t index)
{
  if (jewels::fails(try_flip(index)))
  {
    throw std::out_of_range{"Bitset::flip: index out of range"};
  }
  return *this;
}

template <size_t size_p>
constexpr jewels::BinaryOutcome Bitset<size_p>::try_flip(size_t index) noexcept
{
  if (!is_valid_index(index))
  {
    return jewels::failure;
  }
  const auto storage_index = byte_index(index);
  storage_[storage_index] = static_cast<std::byte>(byte_at(storage_index) ^ bit_mask(index));
  return jewels::success;
}

template <size_t size_p>
constexpr Bitset<size_p>& Bitset<size_p>::flip() noexcept
{
  for (auto& byte : storage_)
  {
    byte = static_cast<std::byte>(~std::to_integer<unsigned char>(byte));
  }
  clear_unused_bits();
  return *this;
}

template <size_t size_p>
constexpr bool Bitset<size_p>::all() const noexcept
{
  for (size_t index = 0U; index + 1U < byte_size; ++index)
  {
    if (byte_at(index) != full_byte_mask)
    {
      return false;
    }
  }
  return (byte_at(byte_size - 1U) & last_byte_mask()) == last_byte_mask();
}

template <size_t size_p>
constexpr bool Bitset<size_p>::any() const noexcept
{
  for (size_t index = 0U; index + 1U < byte_size; ++index)
  {
    if (byte_at(index) != 0U)
    {
      return true;
    }
  }
  return (byte_at(byte_size - 1U) & last_byte_mask()) != 0U;
}

template <size_t size_p>
constexpr bool Bitset<size_p>::none() const noexcept
{
  return !any();
}

template <size_t size_p>
constexpr size_t Bitset<size_p>::count() const noexcept
{
  size_t result{0U};
  for (size_t index = 0U; index + 1U < byte_size; ++index)
  {
    result += static_cast<size_t>(std::popcount(byte_at(index)));
  }
  return result +
         static_cast<size_t>(std::popcount(static_cast<unsigned char>(byte_at(byte_size - 1U) & last_byte_mask())));
}

template <size_t size_p>
constexpr std::span<const std::byte, Bitset<size_p>::byte_size> Bitset<size_p>::bytes() const noexcept
{
  return storage_;
}

template <size_t size_p>
constexpr bool Bitset<size_p>::is_valid_index(size_t index) noexcept
{
  return index < bit_size;
}

template <size_t size_p>
constexpr size_t Bitset<size_p>::byte_index(size_t index) noexcept
{
  return index / bits_per_byte;
}

template <size_t size_p>
constexpr unsigned char Bitset<size_p>::bit_mask(size_t index) noexcept
{
  return static_cast<unsigned char>(1U << (index % bits_per_byte));
}

template <size_t size_p>
constexpr unsigned char Bitset<size_p>::last_byte_mask() noexcept
{
  constexpr size_t remainder = bit_size % bits_per_byte;
  if constexpr (remainder == 0U)
  {
    return full_byte_mask;
  }
  else
  {
    return static_cast<unsigned char>((1U << remainder) - 1U);
  }
}

template <size_t size_p>
constexpr unsigned char Bitset<size_p>::byte_at(size_t index) const noexcept
{
  return std::to_integer<unsigned char>(storage_[index]);
}

template <size_t size_p>
constexpr void Bitset<size_p>::clear_unused_bits() noexcept
{
  storage_[byte_size - 1U] = static_cast<std::byte>(byte_at(byte_size - 1U) & last_byte_mask());
}

template <size_t size_p>
constexpr bool Bitset<size_p>::operator==(const Bitset& other) const noexcept
{
  for (size_t index = 0U; index + 1U < Bitset<size_p>::byte_size; ++index)
  {
    if (storage_[index] != other.storage_[index])
    {
      return false;
    }
  }
  const auto final_mask = last_byte_mask();
  return (byte_at(byte_size - 1U) & final_mask) == (other.byte_at(byte_size - 1U) & final_mask);
}

} // namespace jewels::tap
