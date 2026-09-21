// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <type_traits>

namespace jewels
{
/// Invokes the provided function when the object is destroyed, allowing code to run whenever the enclosing scope ends.
/// Useful if you want to run code even if the code path makes an early return from a function.
/// @tparam FnType Type of the function called when the object is destroyed (inferred)
template <typename FnType>
class ScopeGuard
{
public:
  explicit ScopeGuard(FnType&& call_on_destruct_fn) noexcept(std::is_nothrow_move_constructible_v<FnType>);

  // Boilerplate other constructors / assignment operator
  ScopeGuard() = delete;
  ScopeGuard(ScopeGuard&& other) noexcept(std::is_nothrow_move_constructible_v<FnType>);

  // Disabling copy and assignment, because these lead to a scope guard firing multiple times, or ambiguity about when a
  // scope guard is firing.
  ScopeGuard(const ScopeGuard& other) = delete;
  ScopeGuard& operator=(const ScopeGuard& other) = delete;
  ScopeGuard& operator=(ScopeGuard&& other) noexcept = delete;

  // Call associated function when object is destructed
  ~ScopeGuard() noexcept(std::is_nothrow_invocable_v<FnType>);

  void dismiss() noexcept;

private:
  FnType call_on_destruct_fn_;
  bool active_{true};
};
} // namespace jewels

#include "jewels/scope_guard/scope_guard.inl"
