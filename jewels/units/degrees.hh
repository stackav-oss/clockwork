// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/units/quantity.hh"

#include <au/au.hh>            // IWYU pragma: export
#include <au/prefix.hh>        // IWYU pragma: export
#include <au/units/degrees.hh> // IWYU pragma: export

#include <cstdint>

namespace au
{
using DegreesF = au::QuantityF<au::Degrees>;
using DegreesD = au::QuantityD<au::Degrees>;
using DegreesU16 = Quantity<au::Degrees, uint16_t>;

using CentiDegrees = Centi<Degrees>;
using CentiDegreesU16 = Quantity<CentiDegrees, uint16_t>;

constexpr auto centidegrees = centi(degrees);
} // namespace au
