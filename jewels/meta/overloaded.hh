// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace jewels::meta
{

// Helper type for variant visitation
template <class... Ts>
struct Overloaded : Ts... // NOLINT(fuchsia-multiple-inheritance) This is needed to provide the overload behavior
{
  using Ts::operator()...;
};

// Explicit deduction guide (not needed as of C++20)
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

} // namespace jewels::meta
