// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "jewels/uuid/uuid.hh"

#include <nanobind/nanobind.h>

NAMESPACE_BEGIN(NB_NAMESPACE)
NAMESPACE_BEGIN(detail)

// Define a Nanobind type caster for UUID.
// Converts between C++ jewels::Uuid<> and python uuid.UUID.
template <typename TagType>
struct type_caster<jewels::Uuid<TagType>>;

NAMESPACE_END(detail)
NAMESPACE_END(NB_NAMESPACE)

#include "jewels/nanobind/uuid_caster.inl"
