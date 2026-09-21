// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/units/earth.hh"
#include "jewels/units/kinematics.hh"

#include <au/magnitude.hh>
#include <au/power_aliases.hh>
#include <au/unit_of_measure.hh>
#include <catch2/catch_test_macros.hpp>

namespace au::testing
{
TEST_CASE("G")
{
  constexpr auto one_g = au::earth_surface_gs(1.);
  CHECK(one_g.in(mpss) == 9.80665);
}

} // namespace au::testing
