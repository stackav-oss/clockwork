// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/meta/call_detail.hh"

namespace jewels::meta
{

/// Alias used to call a meta lambda.
template <class Fn, class... Args>
using Call = detail::Helper::Call<Fn, Args...>;

} // namespace jewels::meta
