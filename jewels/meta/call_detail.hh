// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace jewels::meta::detail
{

/// Helper struct to hold the details invoking lambdas, transforms, etc.
/// @note May need to add a specialization on the size of the argument pack to prevent the error of expanding a pack
/// into a non-pack template, but this currently works in both clang and gcc. we'll defer that to later.
struct Helper
{
  template <class Fn, class... Args>
  using Call = typename Fn::template Type<Args...>;
};

} // namespace jewels::meta::detail
