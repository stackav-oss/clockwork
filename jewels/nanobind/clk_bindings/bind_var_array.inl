// IWYU pragma: private, include "jewels/nanobind/clk_bindings/bind_var_array.hh"

#pragma once
#include "jewels/nanobind/clk_bindings/bind_array_eq.hh"
#include "jewels/nanobind/clk_bindings/bind_numpy_array.hh"
#include "jewels/nanobind/clk_bindings/bind_str_and_repr.hh"
#include "jewels/nanobind/clk_bindings/cast_maybe_by_reference.hh"
#include "jewels/nanobind/clk_bindings/wrap_index.hh"

#include <nanobind/make_iterator.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/detail/traits.h> // IWYU pragma: keep
#include <nanobind/stl/unique_ptr.h>    // IWYU pragma: keep

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace jewels::nanobind
{
namespace detail
{

template <typename Vector, typename Value>
void bind_pop(::nanobind::class_<Vector>& vec_binding)
{
  namespace nb = ::nanobind;

  // Pop will have a different return value based on whether we have to keep this off the stack or not.
  constexpr bool cast_by_ref = detail::should_cast_by_reference<Value>();
  using PoppedReturnValue = std::conditional_t<cast_by_ref, std::unique_ptr<Value>, Value>;
  vec_binding.def(
    "pop",
    [](Vector& vec, Py_ssize_t unwrapped_index) -> PoppedReturnValue
    {
      size_t index = wrap_index(unwrapped_index, vec.size());
      PoppedReturnValue result;
      if constexpr (cast_by_ref)
      {
        result = std::make_unique<Value>(std::move(vec.at(index)));
      }
      else
      {
        result = std::move(vec.at(index));
      }
      vec.erase(vec.begin() + static_cast<int64_t>(index));
      return result;
    },
    nb::arg("index") = -1,
    "Remove and return item at `index` (default last).");
}

template <typename Vector, typename Value>
// NOLINTNEXTLINE(readability-function-cognitive-complexity) TODO(OI-3787)
void bind_var_array_copy_constructable_methods(::nanobind::class_<Vector>& vec_binding)
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
      for (nb::handle handle : seq)
      {
        vec->push_back(cast_maybe_by_reference<Value>(handle));
      }
    },
    "Construct from an iterable object");

  vec_binding.def(
    "from_iter",
    [](Vector& vec, nb::typed<nb::iterable, Value> seq)
    {
      vec.clear();
      for (nb::handle handle : seq)
      {
        vec.push_back(cast_maybe_by_reference<Value>(handle));
      }
    },
    "Set elements from an iterable object.");

  nb::implicitly_convertible<nb::iterable, Vector>();

  vec_binding
    .def(
      "append", [](Vector& vec, const Value& value) { vec.push_back(value); }, "Append `arg` to the end of the list.")
    .def(
      "insert",
      [](Vector& vec, Py_ssize_t index, const Value& element)
      { vec.insert(vec.begin() + static_cast<int64_t>(wrap_index(index, vec.size())), element); },
      "Insert object `arg1` before index `arg0`.");

  bind_pop<Vector, Value>(vec_binding);

  vec_binding
    .def(
      "extend",
      [](Vector& vec, const nb::typed<nb::list, Value>& src)
      {
        if (vec.size() + src.size() > vec.capacity())
        {
          throw std::length_error{"VarArray: extend: Insufficient capacity."};
        }

        for (size_t k = 0; k < src.size(); k++)
        {
          vec.push_back(cast_maybe_by_reference<Value>(src[k]));
        }
      },
      "Extend `self` by appending elements from `arg`.")
    .def(
      // Also add an `extend` that directly takes an iterator, to avoid an unnecessary conversion to a list.
      // The error handling happens inline instead of up front, because we don't know the iterator length ahead of time.
      "extend",
      [](Vector& vec, nb::typed<nb::iterable, Value> seq)
      {
        const size_t original_size = vec.size();
        for (nb::handle handle : seq)
        {
          if (vec.full())
          {
            // If the vector is full, restore the original size and throw an error.
            vec.resize(original_size);
            throw std::length_error{"VarArray: extend: Insufficient capacity."};
          }
          vec.push_back(cast_maybe_by_reference<Value>(handle));
        }
      },
      "Extend `self` by appending elements from an iterable object")
    .def(
      "__setitem__",
      [](Vector& vec, Py_ssize_t index, const Value& value) { vec.at(wrap_index(index, vec.size())) = value; })
    .def(
      "__delitem__",
      [](Vector& vec, Py_ssize_t index)
      { vec.erase(vec.begin() + static_cast<int64_t>(wrap_index(index, vec.size()))); })
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
      })
    .def(
      "__delitem__",
      [](Vector& vec, const nb::slice& slice)
      {
        auto [start, stop, step, length] = slice.compute(vec.size());
        if (length == 0)
        {
          return;
        }

        stop = start + ((static_cast<int64_t>(length) - 1) * step);
        if (start > stop)
        {
          std::swap(start, stop);
          step = -step;
        }

        if (step == 1)
        {
          vec.erase(vec.begin() + start, vec.begin() + stop + 1);
        }
        else
        {
          // Deleting for non-contiguous elements (step != 1) is pretty inefficient right now.
          // It's N^2 instead of N.
          for (size_t count = 0; count < length; ++count)
          {
            vec.erase(vec.begin() + stop);
            stop -= step;
          }
        }
      });
}

template <typename Vector, typename Value>
void bind_var_array_equality_comparable_methods(::nanobind::class_<Vector>& vec_binding)
{
  namespace nb = ::nanobind;

  static_assert(nb::detail::is_equality_comparable_v<Value>);

  // Common equality comparisons.
  bind_array_common_equality_comparable_methods<Vector, Value>(vec_binding);

  // VarArray also gets "remove".
  vec_binding.def(
    "remove",
    [](Vector& vec, const Value& element)
    {
      auto iter = std::find(vec.begin(), vec.end(), element);
      if (iter != vec.end())
      {
        vec.erase(iter);
      }
      else
      {
        throw nb::value_error("VarArray.remove(x): x not in list.");
      }
    },
    "Remove first occurrence of `arg`.");
}

} // namespace detail

template <typename Vector, typename Value, ::nanobind::rv_policy policy, typename... Args>
::nanobind::class_<Vector> bind_var_array(::nanobind::handle scope, const char* name, Args&&... args)
{
  namespace nb = ::nanobind;

  static_assert(
    !nb::detail::is_base_caster_v<nb::detail::make_caster<Value>> || nb::detail::is_copy_constructible_v<Value> ||
      (policy != nb::rv_policy::automatic_reference && policy != nb::rv_policy::copy),
    "bind_vector(): the generated __getitem__ would copy elements, so the "
    "element type must be copy-constructible");

  nb::handle cl_cur = nb::type<Vector>();
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
        policy)
      .def(
        "clear", [](Vector& vec) { vec.clear(); }, "Remove all items.")
      .def("capacity", [](Vector& vec) { return vec.capacity(); }, "Get the max capacity of the VarArray.");

  detail::bind_var_array_copy_constructable_methods<Vector, Value>(vec_binding);
  detail::bind_var_array_equality_comparable_methods<Vector, Value>(vec_binding);
  detail::bind_str_and_repr<Vector>(vec_binding);

  maybe_bind_numpy_array<Vector, Value>(vec_binding);

  return vec_binding;
}

} // namespace jewels::nanobind
