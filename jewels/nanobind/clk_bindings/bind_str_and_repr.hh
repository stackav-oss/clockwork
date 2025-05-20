// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <nanobind/nanobind.h>

namespace jewels::nanobind::detail
{

// Add __str__ and __repr__ to a vector-like nanobind bound  type.
// Looks like "[a, b, c, d]".
// Difference between __str__ and __repr__ is that __str__ truncates long lists to avoid spamming the console, similar
// to how numpy does it.
template <typename Vector>
::nanobind::class_<Vector> bind_str_and_repr(::nanobind::class_<Vector>& vec_binding);

} // namespace jewels::nanobind::detail

#include "jewels/nanobind/clk_bindings/bind_str_and_repr.inl"
