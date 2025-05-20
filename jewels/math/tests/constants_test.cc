// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/math/constants.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace jewels::math::constants
{

TEST_CASE("Smoke test")
{
  static_assert(bytes_per_kb<int16_t> == 1000);
  static_assert(bytes_per_kib<int16_t> == 1024);
  static_assert(bytes_per_kb<uint16_t> == 1000U);
  static_assert(bytes_per_kib<uint16_t> == 1024U);
  static_assert(bytes_per_kb<float> == 1000.0f);
  static_assert(bytes_per_kib<float> == 1024.0f);
  static_assert(bytes_per_mb<int32_t> == 1000000);
  static_assert(bytes_per_mib<int32_t> == 1048576);
  static_assert(bytes_per_mb<uint32_t> == 1000000U);
  static_assert(bytes_per_mib<uint32_t> == 1048576U);
  static_assert(bytes_per_mb<double> == 1000000.0);
  static_assert(bytes_per_mib<double> == 1048576.0);
  static_assert(bytes_per_gb<int64_t> == 1000000000);
  static_assert(bytes_per_gib<int64_t> == 1073741824);
  static_assert(bytes_per_gb<uint64_t> == 1000000000U);
  static_assert(bytes_per_gib<uint64_t> == 1073741824U);
  static_assert(bytes_per_gb<double> == 1000000000.0);
  static_assert(bytes_per_gib<double> == 1073741824.0);
}

} // namespace jewels::math::constants
