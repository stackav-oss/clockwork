// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/container/tap/var_string.hh"

#include <nanobind/nanobind.h>

namespace nanobind::detail
{

template <size_t capacity>
struct type_caster<::jewels::tap::VarArray<std::byte, capacity>>;

template <size_t size>
struct type_caster<std::array<std::byte, size>>;

} // namespace nanobind::detail

#include "jewels/nanobind/nb_byte_array.inl"
