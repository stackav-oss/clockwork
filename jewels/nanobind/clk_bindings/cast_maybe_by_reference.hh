// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <nanobind/nanobind.h>

#include <type_traits>

namespace jewels::nanobind
{
namespace detail
{

/// The check for 'cast_maybe_by_reference'. See that function's documentation below.
template <typename T>
constexpr bool should_cast_by_reference()
{
  // cast by reference if this is a bound type
  using Caster = ::nanobind::detail::make_caster<T>;
  return ::nanobind::detail::is_base_caster_v<Caster>;
}
} // namespace detail

/// Cast a nanobind::handle to a C++ type value or reference.
///
/// If the handle points to a bound type, we want to cast to a reference to the underlying type
/// to avoid a copy, to avoid stack allocation, and for correct mutable reference semantics.
/// If the handle points to a primitive type, there is no reference to point to, we must cast by value which does a
/// conversion. This matches the expected non-mutable semantics for primitives.
template <typename T>
std::conditional_t<detail::should_cast_by_reference<T>(), T&, T> cast_maybe_by_reference(::nanobind::handle value)
{
  namespace nb = ::nanobind;

  if constexpr (detail::should_cast_by_reference<T>())
  {
    // Return by reference
    return nb::cast<T&>(value);
  }
  else
  {
    // Return by value
    return nb::cast<T>(value);
  }
}

/// Cast a value and do an equality check.
/// Cast errors return false because types are different.
/// Comparison is done by reference, and there is no stack allocation except a pointer, unless type is a primitive.
template <typename T>
bool casted_value_is_equal(const T& lhs, ::nanobind::handle rhs_handle)
{
  try
  {
    return lhs == cast_maybe_by_reference<T>(rhs_handle);
  }
  catch (const ::nanobind::cast_error&)
  {
    return false;
  }
}

} // namespace jewels::nanobind
