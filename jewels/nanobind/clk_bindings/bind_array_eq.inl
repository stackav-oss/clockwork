// IWYU pragma: private, include "jewels/nanobind/clk_bindings/bind_array_eq.hh"

#pragma once

#include "jewels/nanobind/clk_bindings/cast_maybe_by_reference.hh"

#include <nanobind/nanobind.h>
#include <nanobind/stl/detail/traits.h> // IWYU pragma: keep

#include <algorithm> // IWYU pragma: keep
#include <cstddef>
// IWYU pragma: no_include <pyport.h>

namespace jewels::nanobind
{

namespace detail
{

// Compare a Vector to a nanobind list.
template <typename Vector, typename Value>
bool vec_equals_list(const Vector& lhs, const ::nanobind::typed<::nanobind::list, const Value&>& rhs)
{
  if (lhs.size() != rhs.size())
  {
    return false;
  }
  for (size_t index = 0; index < lhs.size(); index++)
  {
    if (!casted_value_is_equal(lhs.at(index), rhs[index]))
    {
      return false;
    }
  }
  return true;
}
} // namespace detail

/// equality comparison methods common to VarArray and FixedArray
template <typename Vector, typename Value>
void bind_array_common_equality_comparable_methods(::nanobind::class_<Vector>& vec_binding)
{
  namespace nb = ::nanobind;

  static_assert(nb::detail::is_equality_comparable_v<Value>);

  // Same exact types are compared directly.
  vec_binding.def("__eq__", [](const Vector& lhs, const Vector& rhs) -> bool { return lhs == rhs; })
    .def("__ne__", [](const Vector& lhs, const Vector& rhs) -> bool { return lhs != rhs; });

  // Lists also considered for comparison purposes, because it's pretty inconvenient to convert to bound arrays.
  // Compare element by element.
  // Use comparison functions from 'cast_maybe_by_reference' to avoid stack allocation/copying.
  vec_binding
    .def(
      "__eq__",
      [](const Vector& lhs, const nb::typed<nb::list, const Value&>& rhs) -> bool
      { return detail::vec_equals_list(lhs, rhs); })
    .def(
      "__ne__",
      [](const Vector& lhs, const nb::typed<nb::list, const Value&>& rhs) -> bool
      { return !detail::vec_equals_list(lhs, rhs); });

  // All other types are considered not equal.
  vec_binding.def("__eq__", [](const Vector& /* lhs */, nb::handle /* rhs */) -> bool { return false; })
    .def("__ne__", [](const Vector& /* lhs */, nb::handle /* rhs */) -> bool { return true; });

  vec_binding
    .def(
      "__contains__",
      [](const Vector& vec, const Value& element) { return std::find(vec.begin(), vec.end(), element) != vec.end(); })
    .def(
      "__contains__", // fallback for incompatible types
      [](const Vector&, nb::handle) { return false; },
      // without arg().none(), "assert None not in MyArray()" gives a TypeError exception
      nb::arg().none())
    .def(
      "count",
      [](const Vector& vec, const Value& element) { return std::count(vec.begin(), vec.end(), element); },
      "Return number of occurrences of `arg`.");
}

} // namespace jewels::nanobind
