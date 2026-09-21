// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <type_traits>

namespace jewels::meta
{
namespace detail
{

template <class Type, class... Types>
using TypeCount =
  std::integral_constant<std::size_t, (std::size_t{0U} + ... + std::size_t{std::is_same_v<Type, Types>})>;

template <class... Types>
using Unique = std::bool_constant<((TypeCount<Types, Types...>::value == 1U) && ...)>;

} // namespace detail

/// Meta lambda that returns the original type.
struct IdentityT
{
  template <class T>
  using Type = T;
};

/// Meta lambda to add l-value reference qualification.
struct AddLRefT
{
  template <class T>
  using Type = T&;
};

/// Meta lambda to add const l-value reference qualification.
struct AddConstLRefT
{
  template <class T>
  using Type = const T&;
};

/// Meta lambda that returns whether every argument is a unique type.
struct UniqueT
{
  template <class... Types>
  using Type = detail::Unique<Types...>;
};

/// Meta lambda to choose one type or another depending on a condition.
template <bool condition>
struct ConditionalT
{
  template <class, class IfFalse>
  using Type = IfFalse;
};

/// Specialization for when the condition is true.
template <>
struct ConditionalT<true>
{
  template <class IfTrue, class>
  using Type = IfTrue;
};

} // namespace jewels::meta
