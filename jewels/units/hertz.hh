// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/units/quantity.hh"

#include <au/units/hertz.hh> // IWYU pragma: export

namespace au
{
using HertzF = au::QuantityF<au::Hertz>;
using HertzD = au::QuantityD<au::Hertz>;
} // namespace au
