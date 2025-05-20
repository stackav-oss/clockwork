// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <nanobind/nanobind.h>

namespace jewels::nanobind
{

/// Add the __array__ method to this binding, if element type Value is a primitive of au::Quantity type.
/// This copies the underlying data. It's meant for convenience when interoperating with libraries that support numpy
/// arrays, like matplotlib and plotly.
template <typename Vector, typename Value>
void maybe_bind_numpy_array(::nanobind::class_<Vector>& vec_binding);

} // namespace jewels::nanobind

#include "jewels/nanobind/clk_bindings/bind_numpy_array.inl"
