// IWYU pragma: private, include "jewels/nanobind/clk_bindings/bind_str_and_repr.hh"
#pragma once
#include "jewels/nanobind/clk_bindings/bind_str_and_repr.hh"

#include <nanobind/nanobind.h>

#include <cstddef>
#include <limits>
#include <memory>
#include <sstream>

namespace jewels::nanobind::detail
{
namespace nb = ::nanobind;

template <typename Vector>
nb::str render_vector_as_list(nb::handle_t<Vector> handle, const std::streamoff max_str_len)
{

  const std::size_t length = nb::len(handle);
  std::ostringstream oss;
  oss << "[";
  for (size_t k = 0; k < length; k++)
  {
    nb::object item = handle[k];
    oss << nb::repr(item).c_str();
    if (k + 1 < length)
    {
      oss << ", ";

      // Don't show it it's too long to avoid spamming the console, similar to numpy.
      // If you need to see the whole thing, __repr__ always works.
      if (oss.tellp() > static_cast<std::streamoff>(max_str_len))
      {
        oss << "...";
        break;
      }
    }
  }
  oss << "]";
  return nb::str(oss.str().c_str());
}

template <typename Vector>
::nanobind::class_<Vector> bind_str_and_repr(::nanobind::class_<Vector>& vec_binding)
{
  namespace nb = ::nanobind;

  vec_binding
    .def(
      "__repr__",
      [](nb::handle_t<Vector> handle)
      { return render_vector_as_list(handle, std::numeric_limits<std::streamoff>::max()); })
    .def(
      "__str__",
      [](nb::handle_t<Vector> handle)
      {
        constexpr std::streamoff max_str_len = 40;
        return render_vector_as_list(handle, max_str_len);
      });
  return vec_binding;
}

} // namespace jewels::nanobind::detail
