// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <utility>

namespace jewels
{

// Backport of https://en.cppreference.com/w/cpp/utility/functional/identity
struct Identity
{
  template <typename T>
  constexpr T&& operator()(T&& value) const noexcept
  {
    return std::forward<T>(value);
  }
};

} // namespace jewels
