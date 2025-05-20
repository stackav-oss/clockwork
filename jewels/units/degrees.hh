// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/units/magnitude.hh"
#include "jewels/units/quantity.hh"

#include <au/au.hh>            // IWYU pragma: export
#include <au/prefix.hh>        // IWYU pragma: export
#include <au/units/degrees.hh> // IWYU pragma: export

namespace au
{
using DegreesF = au::QuantityF<au::Degrees>;
using DegreesD = au::QuantityD<au::Degrees>;
} // namespace au
