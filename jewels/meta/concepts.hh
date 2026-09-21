// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <type_traits> // IWYU pragma: keep
#include <utility>

namespace jewels::meta
{

/// A backport of std::is_same because the stdlib we use doesn't have the <concepts> header
template <class T, class U>
concept SameAs = std::is_same_v<T, U> && std::is_same_v<U, T>;

/// Checks that a types decays to a type that satisfies SameAs.
template <class T, class U>
concept DecaysTo = SameAs<std::decay_t<T>, U>;

/// Backport of std::convertible_to.
template <class From, class To>
concept ConvertibleTo = std::is_convertible_v<From, To> && requires { static_cast<To>(std::declval<From>()); };

/// Check if a type can be casted to another type.
template <class From, class To>
concept CastsTo = requires { static_cast<To>(std::declval<From>()); };

/// A backport of std::integral
template <class T>
concept Integral = std::is_integral_v<T>;

/// A concept representing an ImplicitLifetime type.
/// https://en.cppreference.com/w/cpp/language/classes#Implicit-lifetime_class
template <class Type>
concept ImplicitLifetimeType =
  !std::is_reference_v<Type> &&
  (std::is_trivially_constructible_v<Type> || std::is_trivially_copy_constructible_v<Type>) &&
  std::is_trivially_destructible_v<Type>;

/// A concept to represent either a mutable or const std::byte type.
template <class Type>
concept Byte = !std::is_reference_v<Type> && (SameAs<Type, std::byte> || SameAs<Type, const std::byte>);

} // namespace jewels::meta
