// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// Nanobind provides <nanobind/stl/bind_vector.h>, which mostly works with VarArray, but it needs slight modification.
// This copies bind_vector.h and makes necessary changes to pass clockwork quality standards.

#pragma once

#include <nanobind/nanobind.h>

namespace jewels::nanobind
{
template <
  typename Vector,
  typename Value,
  ::nanobind::rv_policy policy = ::nanobind::rv_policy::automatic_reference,
  typename... Args>
::nanobind::class_<Vector> bind_var_array(::nanobind::handle scope, const char* name, Args&&... args);

} // namespace jewels::nanobind

#include "jewels/nanobind/clk_bindings/bind_var_array.inl"
