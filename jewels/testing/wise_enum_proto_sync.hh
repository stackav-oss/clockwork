// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <catch2/catch_test_macros.hpp>

// lint is turned off as the macro format allows Check2 to give expected
// debug output as to the failure. if a tempalte function is used
// the output is translated to REQUIRE(false), losing the context
// as to which enum or name is the problem

// for matching wise enum and proto enum types, peform a series of Catch2
// checks to verify the names and types match for both enum declarations
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage) No way around this. We're emitting Catch2 macros.
#define REQUIRE_WISE_ENUM_PROTO_SYNC(WiseEnumType, ProtoEnumType) \
  { \
    const auto* descriptor = ProtoEnumType##_descriptor(); \
\
    SECTION("number of elements match") \
    { /* adapt element count check if the wise enum does not include a _UNSPECIFIED member */ \
      uint32_t offset = 1; \
      auto result = std::ranges::find_if( \
        wise_enum::range<WiseEnumType>, [](const auto& val) { return static_cast<int>(val.value) == 0; }); \
      if (result != std::ranges::end(wise_enum::range<WiseEnumType>)) \
      { \
        offset = 0; \
      } \
      REQUIRE(static_cast<uint32_t>(descriptor->value_count()) == (wise_enum::size<WiseEnumType> + offset)); \
    } \
\
    SECTION("enum values match") \
    { /* proto side is required to define an enum with value 0 */ \
      REQUIRE(ProtoEnumType##_IsValid(0)); \
      for (auto val : wise_enum::range<WiseEnumType>) \
      { \
        REQUIRE(ProtoEnumType##_IsValid(static_cast<int>(val.value))); \
      } \
    } \
\
    SECTION("name strings match") \
    { \
      /* proto side is required to name enum value 0 with _UNSPECIFIED */ \
      auto value_ptr = descriptor->FindValueByNumber(0); \
      REQUIRE(value_ptr != nullptr); \
      REQUIRE(value_ptr->name().ends_with("_UNSPECIFIED")); \
      /* check that the proto name contains as a suffix the UPPER_CASE of the C++ name */ \
      /* the proto linter will catch if the ENUM_NAME prefix is incorrectly named */ \
      for (auto val : wise_enum::range<WiseEnumType>) \
      { \
        auto enum_name = std::string(wise_enum::to_string<WiseEnumType>(val.value)); \
        auto proto_name = ProtoEnumType##_Name(static_cast<int>(val.value)); \
        std::transform(enum_name.begin(), enum_name.end(), enum_name.begin(), ::toupper); \
        if (!proto_name.ends_with(enum_name)) \
        { /* trick to get the actual names displayed to the user to speed debuggging */ \
          REQUIRE(enum_name == proto_name); \
        } \
      } \
    } \
  }
