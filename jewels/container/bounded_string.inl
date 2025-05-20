// IWYU pragma: private, include "jewels/container/bounded_string.hh"
#pragma once
#include "jewels/container/bounded_string.hh"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace jewels::container
{

template <size_t max_length, class CharT, class Traits>
constexpr BasicBoundedString<max_length, CharT, Traits>::BasicBoundedString() noexcept
{
  Traits::assign(data_[0], CharT());
}

template <size_t max_length, class CharT, class Traits>
constexpr BasicBoundedString<max_length, CharT, Traits>::BasicBoundedString(const CharT* data, size_t size)
  : size_(std::min(max_length, size))
{
  Traits::copy(data_.data(), data, size_);
  Traits::assign(data_.at(size_), CharT());
}

template <size_t max_length, class CharT, class Traits>
template <size_t other_max>
constexpr BasicBoundedString<max_length, CharT, Traits>::BasicBoundedString(
  const other_bounded_string<other_max>& other) noexcept
  requires(other_max <= max_length)
  : size_{other.size()}
{
  Traits::copy(data_.data(), other.data(), size_ + 1);
}

template <size_t max_length, class CharT, class Traits>
constexpr BasicBoundedString<max_length, CharT, Traits>
BasicBoundedString<max_length, CharT, Traits>::truncated(string_view string) noexcept
{
  return {string.data(), string.size()};
}

template <size_t max_length, class CharT, class Traits>
constexpr std::optional<BasicBoundedString<max_length, CharT, Traits>>
BasicBoundedString<max_length, CharT, Traits>::try_make(string_view string) noexcept
{
  if (string.size() <= max_length)
  {
    return BasicBoundedString<max_length, CharT, Traits>{string.data(), string.size()};
  }
  return std::nullopt;
}

template <size_t max_length, class CharT, class Traits>
template <size_t other_max>
constexpr BasicBoundedString<max_length, CharT, Traits>&
BasicBoundedString<max_length, CharT, Traits>::operator=(const other_bounded_string<other_max>& other) noexcept
  requires(other_max <= max_length)
{
  size_ = other.size();
  Traits::copy(data_.data(), other.data(), size_ + 1);
  return *this;
}

template <size_t max_length, class CharT, class Traits>
constexpr bool BasicBoundedString<max_length, CharT, Traits>::try_concat(std::string_view tail) noexcept
{
  if (size_ + tail.size() > max_length)
  {
    return false;
  }
  Traits::copy(data_.data() + size_, tail.data(), tail.size());
  size_ += tail.size();
  data_.data()[size_] = 0;
  return true;
}

template <size_t max_length, class CharT, class Traits>
template <size_t other_length>
constexpr bool BasicBoundedString<max_length, CharT, Traits>::operator==(
  const BasicBoundedString<other_length, CharT, Traits>& other) const noexcept
{
  return to_string_view() == other.to_string_view();
}

template <size_t max_length, class CharT, class Traits>
auto format_as(const BasicBoundedString<max_length, CharT, Traits>& input)
{
  return input.to_string_view();
}

} // namespace jewels::container
