// IWYU pragma: private, include "jewels/container/tap/var_string.hh"
#pragma once

#include "jewels/container/tap/var_string.hh"

#include "jewels/container/tap/var_array.hh"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace jewels::tap
{

template <size_t fixed_capacity>
template <class... Rest>
  requires(std::is_same_v<std::common_type_t<char, Rest...>, char> && (sizeof...(Rest) + 1UL) < fixed_capacity)
VarString<fixed_capacity>::VarString(char first, Rest... rest) noexcept
  : fields_{
      .storage =
        detail::initialize<char, fixed_capacity>(std::make_index_sequence<sizeof...(rest) + 1UL>{}, first, rest...),
      .size{sizeof...(Rest) + 1UL}}
{
}

template <size_t fixed_capacity>
template <size_t in_size>
  requires(in_size <= fixed_capacity)
VarString<fixed_capacity>::VarString(
  const char (&in_str)[in_size]) noexcept // NOLINT(modernize-avoid-c-arrays) This is specifically for C-style strings.
{
  // std::end(...) would include the null terminator as part of the
  // string.  Need std::prev(std::end(...)) to trim the extra
  // terminator.
  Base::insert_impl(Base::end(), std::begin(in_str), std::prev(std::end(in_str)));
  Base::wipe(fixed_capacity - (in_size - 1UL));
}

template <size_t fixed_capacity>
const char* VarString<fixed_capacity>::c_str() const noexcept
{
  return this->data();
}

template <size_t fixed_capacity>
constexpr std::string_view VarString<fixed_capacity>::string_view() const noexcept
{
  return std::string_view(this->data(), this->size());
}

template <size_t fixed_capacity>
constexpr VarString<fixed_capacity>::operator std::string_view() const noexcept
{
  return string_view();
}

template <size_t fixed_capacity>
constexpr bool VarString<fixed_capacity>::try_set(std::string_view other) noexcept
{
  return Base::try_set(other);
}

template <size_t fixed_capacity>
constexpr void VarString<fixed_capacity>::set_truncate(std::string_view str) noexcept
{
  // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) `.size()` is used to prevent overruns
  std::ignore = Base::try_set(std::span{str.data(), std::min(str.size(), fixed_capacity - 1U)});
}

template <size_t fixed_capacity>
bool operator==(const VarString<fixed_capacity>& lhs, const VarString<fixed_capacity>& rhs)
{
  return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

template <size_t fixed_capacity>
bool operator!=(const VarString<fixed_capacity>& lhs, const VarString<fixed_capacity>& rhs)
{
  return !(lhs == rhs);
}

/// Enables support for fmt.
/// NOTE: `fmt10/format.h` needs to be included for this trait to be discoverable rather than just `fmt10/core.h`.
/// Neither are included here to avoid adding a spurious dependency
template <size_t fixed_capacity>
auto format_as(const VarString<fixed_capacity>& string)
{
  return std::string_view{string};
}

} // namespace jewels::tap
