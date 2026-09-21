// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <nanobind/nanobind.h>

#include <cstddef>

namespace jewels::nanobind
{

/// Wrap an index to allow for negative indexing, for example 'foo[-1]'.
/// This function is inlined to avoid linker errors for nanobind::index_error().
inline size_t wrap_index(Py_ssize_t index, size_t size);
} // namespace jewels::nanobind

#include "jewels/nanobind/clk_bindings/wrap_index.inl"
