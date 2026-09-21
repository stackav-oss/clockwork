// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <au/quantity.hh>
#include <nanobind/nanobind.h>

NAMESPACE_BEGIN(NB_NAMESPACE)
NAMESPACE_BEGIN(detail)

// Define a Nanobind type caster for au::Quantity<UnitT, RepT>.
//
// Nanobind knows how to convert many C++ types to Python out of the box.
// When we have a new type it doesn't support, we can add support by writing a type_caster
// and bringing it into scope of our binding definition.
//
// This is the type caster for Aurora Units. It automatically casts to the underlying representation
// which is usually float or double.
template <typename UnitT, typename RepT>
struct type_caster<au::Quantity<UnitT, RepT>>;

NAMESPACE_END(detail)
NAMESPACE_END(NB_NAMESPACE)

#include "jewels/nanobind/nb_au.inl"
