// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace jewels
{

/// Hint to the compiler that a condition is unlikely to be true.
/// @note This should only be used in very specific instances.
/// @param bool_like A type that is convertible to a bool.
/// @return The result of the conversion to bool.
template <class BoolLike>
__attribute__((always_inline)) inline bool unlikely(BoolLike&& bool_like) noexcept;

} // namespace jewels

#include "jewels/compiler/intrinsics.inl"
