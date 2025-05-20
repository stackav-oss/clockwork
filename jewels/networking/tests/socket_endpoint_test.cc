// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/networking/socket_endpoint.hh"

#include <catch2/catch_test_macros.hpp>
#include <fmt10/format.h>

#include <string>

namespace jewels::networking
{

TEST_CASE("fmt")
{
  REQUIRE(fmt::to_string(SocketEndpoint{"1.2.3.4", 5678U}) == "1.2.3.4:5678");
}

} // namespace jewels::networking
