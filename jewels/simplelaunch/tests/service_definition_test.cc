// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/simplelaunch/service_definition.hh"
#include "jewels/testing/wise_enum_proto_sync.hh"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace jewels::simplelaunch
{

TEST_CASE("Ensure ProcessState is in sync with Protobuf")
{
  REQUIRE_WISE_ENUM_PROTO_SYNC(ProcessState, ::jewels::simplelaunch::v1::ProcessState);
}

} // namespace jewels::simplelaunch
