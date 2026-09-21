// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>

namespace jewels
{
/// Backport of C++26 std::atomic_fetch_max_explicit.
/// @param obj Atomic object to update.
/// @param arg Value to compare with the current value of `obj`. If `arg` is greater than the current value of `obj`,
/// `obj` is replaced with `arg`.
/// @param order Memory order for the operation.
/// @return The value of `obj` before the operation.
template <class T>
T atomic_fetch_max_explicit(
  std::atomic<T>* obj, typename std::atomic<T>::value_type arg, std::memory_order order) noexcept;
} // namespace jewels

#include "jewels/std/atomic.inl"
