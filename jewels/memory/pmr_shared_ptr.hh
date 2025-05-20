// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/memory_resource.hh"

namespace jewels::memory
{

///
/// Allocates an object of type T using the provided memory resource and wraps it in a std::shared_ptr using args to
/// construct a T. This is a simple wrapper for `std::allocate_shared` that cuts out some boilerplate related to
/// `memres`.
///
template <typename T, typename... Args>
auto make_pmr_shared(jewels::memory::MemoryResource memres, Args&&... args);

} // namespace jewels::memory

#include "jewels/memory/pmr_shared_ptr.inl"
