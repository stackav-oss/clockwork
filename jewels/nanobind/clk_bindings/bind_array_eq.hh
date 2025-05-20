// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <nanobind/nanobind.h>

namespace jewels::nanobind
{

/// equality comparison methods common to VarArray and FixedArray
template <typename Vector, typename Value>
void bind_array_common_equality_comparable_methods(::nanobind::class_<Vector>& vec_binding);

} // namespace jewels::nanobind

#include "jewels/nanobind/clk_bindings/bind_array_eq.inl"
