// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/container/tap/var_string.hh"

#include <nanobind/nanobind.h>

NAMESPACE_BEGIN(NB_NAMESPACE)
NAMESPACE_BEGIN(detail)

// This is the type caster for VarString<T, size>.
// It uses uses nanobind's built-in caster for string_view under the hood.
template <size_t fixed_capacity>
struct type_caster<jewels::tap::VarString<fixed_capacity>>;

NAMESPACE_END(detail)
NAMESPACE_END(NB_NAMESPACE)

#include "jewels/nanobind/nb_var_string.inl"
