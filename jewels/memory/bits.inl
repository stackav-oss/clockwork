// IWYU pragma: private, include "jewels/memory/bits.hh"

#pragma once

#include "jewels/memory/bits.hh"

#include <cstddef>
#include <cstring>
#include <span>

namespace jewels::memory
{

template <class T>
[[nodiscard]] T bit_cast_to(std::span<const std::byte, sizeof(T)> bytes) noexcept
{
  T dest{};
  std::memcpy(&dest, bytes.data(), sizeof(T));
  return dest;
}

template <class T>
void write_as_bytes(const T& value, std::span<std::byte, sizeof(T)> bytes) noexcept
{
  std::memcpy(bytes.data(), &value, sizeof(T));
}

} // namespace jewels::memory
