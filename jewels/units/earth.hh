// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <au/quantity.hh>
#include <au/units/standard_gravity.hh> // IWYU pragma: export

namespace au
{
using GravityF = au::QuantityF<au::StandardGravity>;
using GravityD = au::QuantityD<au::StandardGravity>;
constexpr auto earth_surface_gs = QuantityMaker<au::StandardGravity>{};

} // namespace au
