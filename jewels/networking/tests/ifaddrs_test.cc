// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/networking/ifaddrs.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

namespace jewels::networking
{

TEST_CASE("Interface Lookup")
{
  const std::string_view bad_addr{"8.8.8.8"};
  REQUIRE_FALSE(lookup_interface_name(bad_addr));
  const std::string_view not_addr{"not an ip"};
  REQUIRE_FALSE(lookup_interface_name(not_addr));

  const std::string_view localhost{"127.0.0.1"};
  const auto iface = lookup_interface_name(localhost);
  REQUIRE(iface);
  REQUIRE(*iface == "lo");
}

} // namespace jewels::networking
