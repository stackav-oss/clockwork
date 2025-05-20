// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace jewels::meta
{

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
