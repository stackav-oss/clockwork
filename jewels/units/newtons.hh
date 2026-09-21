// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/units/quantity.hh"

#include <au/au.hh>     // IWYU pragma: export
#include <au/prefix.hh> // IWYU pragma: export
#include <au/unit_of_measure.hh>
#include <au/units/meters.hh>  // IWYU pragma: export
#include <au/units/newtons.hh> // IWYU pragma: export

namespace au
{
using NewtonsF = QuantityF<Newtons>;
using NewtonsD = QuantityD<Newtons>;

using NewtonMeters = decltype(au::Newtons{} * au::Meters{});
using NewtonMetersF = QuantityF<NewtonMeters>;
using NewtonMetersD = QuantityD<NewtonMeters>;
constexpr auto newton_meters = au::newtons * au::meters;
} // namespace au
