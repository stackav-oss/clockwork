// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/meta/types.hh"

#include <type_traits>

namespace jewels::meta
{

// Adds a type to the front of a Types pack.
template <class, class>
struct Prepend;

template <class T, class... Args>
struct Prepend<T, Types<Args...>>
{
  using type = Types<T, Args...>;
};

/// A filter over a pack of types that keeps those types matching the provided Predicate. Returns Types<> if no
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
} // namespace jewels::meta
