// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <utility>

namespace jewels
{

/// Invokes the provided function when the object is destroyed, allowing code to run whenever the enclosing scope ends.
/// Useful if you want to run code even if the code path makes an early return from a function.
/// @tparam FnType Type of the function called when the object is destroyed (inferred)
template <typename FnType>
struct ScopeGuard
{
  explicit ScopeGuard(FnType&& call_on_destruct_fn) noexcept
    : call_on_destruct_fn_(std::move(call_on_destruct_fn))
  {
  }

  // Boilerplate other constructors / assignment operator
  ScopeGuard() = delete;
  ScopeGuard(const ScopeGuard& other) = default;
  ScopeGuard(ScopeGuard&& other) noexcept = default;
  ScopeGuard& operator=(const ScopeGuard& other) = default;
  ScopeGuard& operator=(ScopeGuard&& other) noexcept = default;

  // Call associated function when object is destructed
  ~ScopeGuard()
  {
    call_on_destruct_fn_();
  }

  FnType call_on_destruct_fn_;
};

} // namespace jewels
