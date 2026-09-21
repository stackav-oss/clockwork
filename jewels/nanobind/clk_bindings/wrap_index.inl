// IWYU pragma: private, include "jewels/nanobind/clk_bindings/wrap_index.hh"
#pragma once

#include <nanobind/nanobind.h>

#include <cstddef>

namespace jewels::nanobind
{

/// Wrap an index to allow for negative indexing, for example 'foo[-1]'.
/// This function is inlined to avoid linker errors for nanobind::index_error().
inline size_t wrap_index(Py_ssize_t index, size_t size)
{
  if (index < 0)
  {
    index += static_cast<Py_ssize_t>(size);
  }

  if (index < 0 || static_cast<size_t>(index) >= size)
  {
    throw ::nanobind::index_error();
  }

  return static_cast<size_t>(index);
}

} // namespace jewels::nanobind
