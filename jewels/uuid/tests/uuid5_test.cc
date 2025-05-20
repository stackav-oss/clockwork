// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/container/compare.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid5.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

namespace jewels::tests
{
namespace
{

/// Test tag
struct TestTag
{
};

TEST_CASE("uuid5")
{
  // >>> uuid.uuid5(uuid.UUID("98d90354-4efc-41bc-9335-cee8d7615304"), "")
  // UUID('e20ac679-94fe-51fe-82ff-ec33c9eed2ba')
  constexpr auto ns1_uuid = *Uuid<TestTag>::from_string("98d90354-4efc-41bc-9335-cee8d7615304");
  CHECK(uuid5<int>(ns1_uuid, "") == *Uuid<int>::from_string("e20ac679-94fe-51fe-82ff-ec33c9eed2ba"));
  CHECK(uuid5<int>(ns1_uuid, "a") == *Uuid<int>::from_string("2fde7747-066d-5da1-94ea-4d9f7f192b42"));

  constexpr auto ns2_uuid = *Uuid<TestTag>::from_string("8047251a-f8b7-42b3-9aef-3a401df30759");
  CHECK(uuid5<int>(ns2_uuid, "") == *Uuid<int>::from_string("0d5098b5-96b2-5103-a98e-b3b364408bb6"));
  CHECK(uuid5<int>(ns2_uuid, "abcdef") == *Uuid<int>::from_string("dff574f5-4e0e-5d97-855e-618033f98594"));

  CHECK(uuid5<TestTag>(ns2_uuid, "abcdef") == *Uuid<TestTag>::from_string("dff574f5-4e0e-5d97-855e-618033f98594"));
}

} // namespace
} // namespace jewels::tests
