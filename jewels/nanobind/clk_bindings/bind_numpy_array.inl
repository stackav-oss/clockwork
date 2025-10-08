// IWYU pragma: private, include "jewels/nanobind/clk_bindings/bind_numpy_array.hh"
#pragma once

#include <au/quantity.hh>
#include <nanobind/eval.h>
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace jewels::nanobind
{

namespace detail
{

// Define a type trait to test if a type is au::Quantity<>
template <typename T>
struct IsAuQuantity : std::false_type
{
};

template <typename T, typename V>
struct IsAuQuantity<au::Quantity<T, V>> : std::true_type
{
};

template <typename T>
inline constexpr bool is_au_quantity_v = IsAuQuantity<T>::value;

} // namespace detail

template <typename Vector, typename Value>
void maybe_bind_numpy_array(::nanobind::class_<Vector>& vec_binding)
{
  namespace nb = ::nanobind;

  if constexpr (std::is_integral_v<Value> || std::is_floating_point_v<Value> || detail::is_au_quantity_v<Value>)
  {
    vec_binding.def(
      "__array__",
      [](const Vector& vec, nb::handle dtype, nb::handle copy)
      {
        // Our strategy will be to convert to an intermediate type, and then call the real np.array() method
        // to handle the potential dtype conversion.
        // This always copies, and raises an exception if someone tries to create a mutable array view.
        auto global_scope = nb::module_::import_("__main__").attr("__dict__");
        const nb::dict local_scope;
        local_scope["dtype"] = dtype;
        local_scope["copy"] = copy;

        if constexpr (detail::is_au_quantity_v<Value>)
        {
          // For aurora units, we must convert to the underlying type. Just make a list element-by-element.
          nb::typed<nb::list, Value> list;
          for (Value element : vec)
          {
            list.append(element);
          }
          local_scope["arr"] = list;
        }
        else
        {
          // If it's really a primitive type, we can use the more efficient nanobind::ndarray.
          constexpr int64_t num_columns = 1;
          std::array<size_t, num_columns> shape = {vec.size()};
          using ndarray_type = nb::ndarray<const Value, nb::ndim<num_columns>, nb::numpy>;
          ndarray_type arr = ndarray_type(vec.data(), num_columns, shape.data(), nb::handle());
          local_scope["arr"] = arr;
        }
        // Call the true np.array() function to handle dtype conversion.
        exec(
          R"(
      import numpy as np
      if copy is False:
          raise ValueError("FixedArray.__array__: copy=False isn't supported. A copy is always made.")
      ret = np.array(arr, dtype=dtype, copy=True)
      )",
          global_scope,
          local_scope);

        // return the result array
        return nb::object{local_scope["ret"]}.release();
      },
      nb::arg("dtype").none() = nb::none(),
      nb::arg("copy").none() = nb::none(),
      nb::sig(
        "def __array__(self, dtype: numpy.dtype | None = None, copy: bool | None = None) -> "
        "numpy.typing.NDArray[typing.Any]"));
  }
}

} // namespace jewels::nanobind
