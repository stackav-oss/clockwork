// IWYU pragma: private, include "jewels/nanobind/clk_bindings/bind_fixed_array.hh"

#pragma once
#include "jewels/nanobind/clk_bindings/bind_array_eq.hh"
#include "jewels/nanobind/clk_bindings/bind_numpy_array.hh"
#include "jewels/nanobind/clk_bindings/bind_str_and_repr.hh"
#include "jewels/nanobind/clk_bindings/cast_maybe_by_reference.hh"
#include "jewels/nanobind/clk_bindings/wrap_index.hh"

#include <Python.h>
#include <fmt/format.h>
#include <nanobind/make_iterator.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/detail/traits.h> // IWYU pragma: keep

#include <cstddef>
#include <new>
#include <stdexcept>
#include <utility>

namespace jewels::nanobind
{
namespace detail
{

template <typename Vector, typename Value>
void from_iter(Vector& vec, const nb::typed<nb::iterable, Value>& seq)
{
  size_t count = 0;
  for (const nb::handle handle : seq)
  {
    if (count < vec.size())
    {
      vec.at(count) = cast_maybe_by_reference<Value>(handle);
    }
    else
    {
      throw std::length_error(fmt::format("Array of length {} set from too-large iterator.", vec.size()));
    }
    count++;
  }
  if (count != vec.size())
  {
    throw std::length_error(
      fmt::format("Array of length {} set from iterator with too few elements ({}).", vec.size(), count));
  }
}

template <typename Vector, typename Value>
void bind_array_copy_constructable_methods(::nanobind::class_<Vector>& vec_binding)
{
  namespace nb = ::nanobind;
  // operations requiring copy construction
  static_assert(nb::detail::is_copy_constructible_v<Value>);
  vec_binding.def(nb::init<const Vector&>(), "Copy constructor");

  vec_binding.def(
    "__init__",
    [](Vector* vec, nb::typed<nb::iterable, Value> seq)
    {
      new (vec) Vector();
      from_iter(*vec, seq);
    },
    "Construct from an iterable object");
  vec_binding.def(
    "from_iter",
    [](Vector& vec, nb::typed<nb::iterable, Value> seq) { from_iter(vec, seq); },
    "Set elements from an iterable object. Length must match exactly.");

  nb::implicitly_convertible<nb::iterable, Vector>();

  vec_binding
    .def(
      "__setitem__",
      [](Vector& vec, Py_ssize_t index, const Value& value) { vec.at(wrap_index(index, vec.size())) = value; })
    .def(
      "__setitem__",
      [](Vector& vec, const nb::slice& slice, const nb::typed<nb::list, Value&>& value)
      {
        auto [start, stop, step, length] = slice.compute(vec.size());

        if (length != value.size())
        {
          throw nb::index_error(
            "The left and right hand side of the slice "
            "assignment have mismatched sizes!");
        }

        for (size_t i = 0; i < length; ++i)
        {
          vec.at(static_cast<size_t>(start)) = cast_maybe_by_reference<Value>(value[i]);
          start += step;
        }
      });
}

template <typename Vector, typename Value>
void bind_array_equality_comparable_methods(::nanobind::class_<Vector>& vec_binding)
{
  namespace nb = ::nanobind;
  static_assert(nb::detail::is_equality_comparable_v<Value>);
  bind_array_common_equality_comparable_methods<Vector, Value>(vec_binding);
}

} // namespace detail

template <typename Vector, typename Value, ::nanobind::rv_policy policy, typename... Args>
::nanobind::class_<Vector> bind_array(::nanobind::handle scope, const char* name, Args&&... args)
{
  namespace nb = ::nanobind;

  static_assert(
    !nb::detail::is_base_caster_v<nb::detail::make_caster<Value>> || nb::detail::is_copy_constructible_v<Value> ||
      (policy != nb::rv_policy::automatic_reference && policy != nb::rv_policy::copy),
    "bind_vector(): the generated __getitem__ would copy elements, so the "
    "element type must be copy-constructible");

  const nb::handle cl_cur = nb::type<Vector>();
  if (cl_cur.is_valid())
  {
    // Binding already exists, don't re-create
    return borrow<nb::class_<Vector>>(cl_cur);
  }

  auto vec_binding =
    nb::class_<Vector>(scope, name, std::forward<Args>(args)...)
      .def(nb::init<>(), "Default constructor")
      .def("__len__", [](const Vector& vec) { return vec.size(); })
      .def(
        "__bool__", [](const Vector& vec) { return !vec.empty(); }, "Check whether the vector is nonempty")
      .def(
        "__iter__",
        [](Vector& vec) { return nb::make_iterator<policy>(nb::type<Vector>(), "Iterator", vec.begin(), vec.end()); },
        nb::keep_alive<0, 1>())
      .def(
        "__getitem__",
        [](Vector& vec, Py_ssize_t index) -> Value& { return vec.at(wrap_index(index, vec.size())); },
        policy)
      .def(
        "__getitem__",
        [](const Vector& vec, const nb::slice& slice) -> nb::typed<nb::list, Value*>
        {
          auto [start, stop, step, length] = slice.compute(vec.size());
          nb::typed<nb::list, Value*> result;

          for (size_t count = 0; count < length; ++count)
          {
            result.append(&vec.at(static_cast<size_t>(start)));
            start += step;
          }

          return result;
        },
        policy);

  detail::bind_array_copy_constructable_methods<Vector, Value>(vec_binding);
  detail::bind_array_equality_comparable_methods<Vector, Value>(vec_binding);
  detail::bind_str_and_repr<Vector>(vec_binding);

  maybe_bind_numpy_array<Vector, Value>(vec_binding);

  return vec_binding;
}

} // namespace jewels::nanobind
