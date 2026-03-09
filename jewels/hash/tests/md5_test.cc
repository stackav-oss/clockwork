// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/hash/md5.hh"

#include <catch2/catch_test_macros.hpp>

namespace jewels::hash
{
namespace
{

constexpr auto expected_hash = std::array{
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
  std::byte{31}};

constexpr auto test_data = std::string_view{"abcd"};

TEST_CASE("md5")
{
  REQUIRE(md5(test_data) == expected_hash);
}

TEST_CASE("Incremental md5")
{
  const auto test_data_span = std::as_bytes(std::span{test_data.data(), test_data.size()});

  SECTION("Hash all at once")
  {
    MD5_CTX ctx{};
    REQUIRE(md5_initialize(jewels::Out{ctx}).ok());
    REQUIRE(md5_update(ctx, test_data_span).ok());
    MD5HashValue hash{};
    REQUIRE(md5_finalize(jewels::Out{hash}, ctx).ok());
    REQUIRE(hash == expected_hash);
  }

  SECTION("Hash incrementally")
  {
    MD5_CTX ctx{};
    REQUIRE(md5_initialize(jewels::Out{ctx}).ok());
    for (const auto datum : test_data_span)
    {
      REQUIRE(md5_update(ctx, std::span{&datum, 1U}).ok());
    }
    MD5HashValue hash{};
    REQUIRE(md5_finalize(jewels::Out{hash}, ctx).ok());
    REQUIRE(hash == expected_hash);
  }
}

} // namespace
} // namespace jewels::hash
