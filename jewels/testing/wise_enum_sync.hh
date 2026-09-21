// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <catch2/catch_test_macros.hpp>

// lint is turned off as the macro format allows Check2 to give expected
// debug output as to the failure. if a template function is used
// the output is translated to REQUIRE(false), losing the context
// as to which enum or name is the problem

// For matching two wise enum types, perform a series of Catch2 checks to verify the
// names and values match for both enum declarations
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage) No way around this. We're emitting Catch2 macros.
#define REQUIRE_WISE_ENUM_SYNC(WiseEnumType1, WiseEnumType2) \
  {SECTION("number of elements match"){REQUIRE(wise_enum::size<WiseEnumType1> == wise_enum::size<WiseEnumType2>); \
  } \
\
  SECTION("enum values and names match") \
  { \
    for (const auto val1 : wise_enum::range<WiseEnumType1>) \
    { \
      const auto maybe_val2 = wise_enum::from_string<WiseEnumType2>(val1.name); \
      REQUIRE(maybe_val2); \
      REQUIRE(static_cast<WiseEnumType1>(*maybe_val2) == val1.value); \
      REQUIRE(wise_enum::to_string(*maybe_val2) == val1.name); \
    } \
  } \
  }
