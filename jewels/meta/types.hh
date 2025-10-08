// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace jewels::meta
{

/// An empty struct.
struct Void
{
};

/// Holds a non-type template parameter.
template <auto in_value>
struct NonType
{
  static constexpr auto value{in_value};
};

/// A non type.
template <auto value>
inline constexpr NonType<value> non_type_v{};

/// A pack of types.
template <class...>
struct Types
{
};

/// A value that is always false.  Useful in places where you'd want to write `static_assert(false)` such as an `else{}`
/// of a constexpr-if clause.
/// @note Do not specialize this.
template <class>
inline constexpr bool always_false_v{false};

/// A type version of the above variable template.
/// @note Do not specialize this.
template <class>
class AlwaysFalse : public std::false_type
{
};

// Helper for getting the number of types in a types pack
template <class... T>
constexpr std::size_t size(Types<T...> /*unused*/)
{
  return sizeof...(T);
}

namespace detail
{
/// Base class for specializing to capture the indexed type.
template <class T, class = void>
struct IndexedTypeHelper
{
};

/// Specialization for accessing the nth element of a Types structure.
template <class... Args>
struct IndexedTypeHelper<Types<Args...>, void>
{
  template <size_t n>
  using Type = std::tuple_element_t<n, std::tuple<Args...>>;
};
} // namespace detail

/// Structure for indexing into a set of types.
struct IndexedType
{
  template <size_t n, class T>
  using Type = detail::IndexedTypeHelper<T>::template Type<n>;
};
} // namespace jewels::meta
