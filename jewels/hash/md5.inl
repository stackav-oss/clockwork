// IWYU pragma: private, include "jewels/hash/md5.hh"
#pragma once

#include "jewels/hash/md5.hh"

#include <array>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <span>
#include <string_view>

namespace jewels::hash
{

std::array<std::byte, md5_byte_length> md5(std::string_view data) noexcept
{
  return md5(std::as_bytes(std::span{data.data(), data.size()}));
}

std::size_t Md5Hash::operator()(const std::array<std::byte, md5_byte_length>& md5data) const noexcept
{
  static_assert(sizeof(std::size_t) * 2 == md5_byte_length);
  std::size_t first{};
  std::size_t second{};
  std::memcpy(&first, md5data.data(), sizeof(std::size_t));
  std::memcpy(&second, std::next(md5data.data(), sizeof(std::size_t)), sizeof(std::size_t));
  return first ^ second;
}

} // namespace jewels::hash
