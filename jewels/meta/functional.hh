// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/meta/types.hh"

#include <tuple>
#include <type_traits>

namespace jewels::meta
{

/// @brief Adds a type to the front of a Types pack.
template <class, class>
struct Prepend;

template <class T, class... Args>
struct Prepend<T, Types<Args...>>
{
  using type = Types<T, Args...>;
};

/// @brief A filter over a pack of types that keeps those types matching the provided Predicate. Returns Types<> if no
/// types match.
template <template <class> class Predicate, class...>
struct KeepIf;

template <template <class> class Predicate>
struct KeepIf<Predicate, Types<>>
{
  using type = Types<>;
};

template <template <class> class Predicate, class Head, class... Tail>
struct KeepIf<Predicate, Types<Head, Tail...>>
{
  using type = std::conditional_t<
    Predicate<Head>::value,
    typename Prepend<Head, typename KeepIf<Predicate, Types<Tail...>>::type>::type,
    typename KeepIf<Predicate, Types<Tail...>>::type>;
};

/// @brief Applies a template class around a pack of types.
template <template <class...> class Template, class...>
struct Apply;

template <template <class> class Template, class... OrigTypes>
struct Apply<Template, Types<OrigTypes...>>
{
  using type = Types<Template<OrigTypes>...>;
};

/// @brief Provides a tuple type from the provided Types pack.
template <class T, class = T>
struct AsTuple;

template <class... Ts>
struct AsTuple<Types<Ts...>>
{
  using type = std::tuple<Ts...>;
};

} // namespace jewels::meta
