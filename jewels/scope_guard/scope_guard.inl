// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
// IWYU pragma: private, include "jewels/scope_guard/scope_guard.hh"

#pragma once

#include "jewels/scope_guard/scope_guard.hh"

#include <type_traits>
#include <utility>

namespace jewels
{
template <typename FnType>
ScopeGuard<FnType>::ScopeGuard(FnType&& call_on_destruct_fn) noexcept(std::is_nothrow_move_constructible_v<FnType>)
  : call_on_destruct_fn_(std::move(call_on_destruct_fn))
{
}

template <typename FnType>
ScopeGuard<FnType>::ScopeGuard(ScopeGuard&& other) noexcept(std::is_nothrow_move_constructible_v<FnType>)
  : call_on_destruct_fn_(std::move(other.call_on_destruct_fn_)), active_(std::exchange(other.active_, false))
{
}

template <typename FnType>
ScopeGuard<FnType>::~ScopeGuard() noexcept(std::is_nothrow_invocable_v<FnType>)
{
  if (active_)
  {
    call_on_destruct_fn_();
  }
}

template <typename FnType>
void ScopeGuard<FnType>::dismiss() noexcept
{
  active_ = false;
}
} // namespace jewels
