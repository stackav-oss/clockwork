// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/units/kilograms.hh"
#include "jewels/units/magnitude.hh"
#include "jewels/units/quantity.hh"

#include <au/au.hh> // IWYU pragma: export
#include <au/power_aliases.hh>
#include <au/prefix.hh>       // IWYU pragma: export
#include <au/units/grams.hh>  // IWYU pragma: export
#include <au/units/meters.hh> // IWYU pragma: export

namespace au
{
using KilogramsPerMeterCubed = decltype(Kilo<Grams>{} / au::cubed(au::Meters{}));
using KilogramsPerMeterCubedF = QuantityF<KilogramsPerMeterCubed>;
using KilogramsPerMeterCubedD = QuantityD<KilogramsPerMeterCubed>;
constexpr auto kg_per_m3 = au::kilograms / au::cubed(au::meters);
} // namespace au
