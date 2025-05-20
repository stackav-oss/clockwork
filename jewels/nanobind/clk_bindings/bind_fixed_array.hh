// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// Nanobind provides <nanobind/stl/bind_vector.h> and <nanobind/stl/bind_map.h>, but there is no
// <nanobind/stl/bind_array.h>. There's an open feature request https://github.com/wjakob/nanobind/discussions/565 and
// the author/maintainer is open to it, but it's not a priority. This file implements an array binding by adapting
// <nanobind/stl/bind_vector.h> and bringing it to stack av code standards.

#pragma once

#include <nanobind/nanobind.h>

namespace jewels::nanobind
{

template <
  typename Vector,
  typename Value,
  ::nanobind::rv_policy policy = ::nanobind::rv_policy::automatic_reference,
  typename... Args>
::nanobind::class_<Vector> bind_array(::nanobind::handle scope, const char* name, Args&&... args);

} // namespace jewels::nanobind
#include "jewels/nanobind/clk_bindings/bind_fixed_array.inl"
