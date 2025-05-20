// IWYU pragma: private, include "jewels/hash/sha1.hh"
#pragma once

#include "jewels/hash/sha1.hh"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>

namespace jewels::hash
{

template <typename... Source>
Sha1::Sha1(const Source&... data)
  : Sha1()
{
  (update(data), // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay) - Variadic arguments `data` may decay to
                 // pointers when string literals are passed, but this is safe as `update` handles `string_view`.
   ...);
}

void Sha1::update(std::string_view data)
{
  update(std::as_bytes(std::span(data)));
}

template <typename T, size_t n>
void Sha1::update(const std::array<T, n>& data)
{
  update(std::as_bytes(std::span(data)));
}

template <typename T, size_t extent>
  requires(!std::is_same_v<T, std::byte>)
void Sha1::update(std::span<const T, extent> data)
{
  update(std::as_bytes(data));
}

} // namespace jewels::hash
