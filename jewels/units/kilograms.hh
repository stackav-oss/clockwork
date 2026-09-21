// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/units/quantity.hh"

#include <au/au.hh>     // IWYU pragma: export
#include <au/prefix.hh> // IWYU pragma: export
#include <au/unit_of_measure.hh>
#include <au/units/grams.hh> // IWYU pragma: export

namespace au
{
using Kilograms = Kilo<Grams>;
using KilogramsF = QuantityF<Kilograms>;
using KilogramsD = QuantityD<Kilograms>;

constexpr auto kilogram = kilo(gram);
constexpr auto kilograms = kilo(grams);
} // namespace au
