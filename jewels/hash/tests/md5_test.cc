// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/hash/md5.hh"

#include <catch2/catch_test_macros.hpp>

namespace jewels::hash
{

TEST_CASE("md5")
{
  REQUIRE(
    md5(std::string_view{"abcd"}) == std::array<std::byte, md5_byte_length>{
                                       std::byte{226},
                                       std::byte{252},
                                       std::byte{113},
                                       std::byte{76},
                                       std::byte{71},
                                       std::byte{39},
                                       std::byte{238},
                                       std::byte{147},
                                       std::byte{149},
                                       std::byte{243},
                                       std::byte{36},
                                       std::byte{205},
                                       std::byte{46},
                                       std::byte{127},
                                       std::byte{51},
                                       std::byte{31}});
}

} // namespace jewels::hash
