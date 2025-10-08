// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/units/degrees.hh"
#include "jewels/units/hours.hh"
#include "jewels/units/meters.hh"
#include "jewels/units/miles.hh"
#include "jewels/units/quantity.hh"
#include "jewels/units/seconds.hh"

#include <au/magnitude.hh>
#include <au/packs.hh> // IWYU pragma: keep
#include <au/power_aliases.hh>
#include <au/unit_of_measure.hh>
#include <au/units/radians.hh>

namespace au
{
using MetersPerSecondCubed = decltype(au::Meters{} / au::cubed(au::Seconds{}));
using MetersPerSecondCubedF = au::QuantityF<MetersPerSecondCubed>;
using MetersPerSecondCubedD = au::QuantityD<MetersPerSecondCubed>;
constexpr auto mpsss = au::meters / au::cubed(au::second);

using MetersPerSecondSquared = decltype(au::Meters{} / au::squared(au::Seconds{}));
using MetersPerSecondSquaredF = au::QuantityF<MetersPerSecondSquared>;
using MetersPerSecondSquaredD = au::QuantityD<MetersPerSecondSquared>;
constexpr auto mpss = au::meters / au::squared(au::second);

using MetersPerSecond = decltype(au::Meters{} / au::Seconds{});
using MetersPerSecondF = au::QuantityF<MetersPerSecond>;
using MetersPerSecondD = au::QuantityD<MetersPerSecond>;
constexpr auto mps = au::meters / au::second;

using MilesPerHour = decltype(au::Miles{} / au::Hours{});
using MilesPerHourF = au::QuantityF<MilesPerHour>;
using MilesPerHourD = au::QuantityD<MilesPerHour>;
constexpr auto miles_per_hour = au::miles / au::hour;

using KilometersPerHour = decltype(au::Kilometers{} / au::Hours{});
using KilometersPerHourF = au::QuantityF<KilometersPerHour>;
using KilometersPerHourD = au::QuantityD<KilometersPerHour>;
constexpr auto kilometers_per_hour = au::kilometers / au::hour;

using OnePerMeter = decltype(au::inverse(au::Meters{}));
using OnePerMeterF = au::QuantityF<OnePerMeter>;
using OnePerMeterD = au::QuantityD<OnePerMeter>;
constexpr auto one_per_meter = au::inverse(au::meters);

using OnePerMeterPerSecond = decltype(au::inverse(au::Meters{}) * au::inverse(au::Seconds{}));
using OnePerMeterPerSecondF = au::QuantityF<OnePerMeterPerSecond>;
using OnePerMeterPerSecondD = au::QuantityD<OnePerMeterPerSecond>;
constexpr auto one_per_meter_per_second = au::inverse(au::meters) * au::inverse(au::seconds);

using RadsPerMeter = decltype(au::Radians{} / au::Meters{});
using RadsPerMeterF = au::QuantityF<RadsPerMeter>;
using RadsPerMeterD = au::QuantityD<RadsPerMeter>;
constexpr auto rads_per_meter = au::radians / au::meters;

using RadsPerMeterPerSecond = decltype(au::Radians{} / au::Meters{} / au::Seconds{});
using RadsPerMeterPerSecondF = au::QuantityF<RadsPerMeterPerSecond>;
using RadsPerMeterPerSecondD = au::QuantityD<RadsPerMeterPerSecond>;
constexpr auto rads_per_meter_per_second = au::radians / au::meters / au::seconds;

using RadsPerSecond = decltype(au::Radians{} / au::Seconds{});
using RadsPerSecondF = au::QuantityF<RadsPerSecond>;
using RadsPerSecondD = au::QuantityD<RadsPerSecond>;
constexpr auto rads_per_second = au::radians / au::seconds;

using RadsPerNanosecond = Giga<RadsPerSecond>;
using RadsPerNanosecondD = au::QuantityD<RadsPerNanosecond>;
inline constexpr auto rads_per_nanosecond = au::radians / au::nanoseconds;

using NanosecondsPerRad = decltype(au::inverse(RadsPerNanosecond{}));
using NanosecondsPerRadD = au::QuantityD<NanosecondsPerRad>;
inline constexpr auto nanoseconds_per_rad = au::nanoseconds / au::radians;

using RadsPerSecondSquared = decltype(au::inverse(au::squared(au::Seconds{})));
using RadsPerSecondSquaredF = au::QuantityF<RadsPerSecondSquared>;
using RadsPerSecondSquaredD = au::QuantityD<RadsPerSecondSquared>;
constexpr auto rads_per_second_squared = au::inverse(au::squared(au::seconds));

using RadsPerSecondCubed = decltype(au::inverse(au::cubed(au::Seconds{})));
using RadsPerSecondCubedF = au::QuantityF<RadsPerSecondCubed>;
using RadsPerSecondCubedD = au::QuantityD<RadsPerSecondCubed>;
constexpr auto rads_per_second_cubed = au::inverse(au::cubed(au::seconds));

using DegreesPerSecond = decltype(au::Degrees{} / au::Seconds{});
using DegreesPerSecondF = au::QuantityF<DegreesPerSecond>;
using DegreesPerSecondD = au::QuantityD<DegreesPerSecond>;
constexpr auto degps = au::degrees / au::second;

} // namespace au

namespace au
{
/// Create a second quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::MetersPerSecond>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_mps(unsigned long long int value) noexcept;

[[nodiscard]] constexpr au::QuantityI64<au::MetersPerSecondSquared>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_mpss(unsigned long long int value) noexcept;

} // namespace au

#include "jewels/units/kinematics.inl"
