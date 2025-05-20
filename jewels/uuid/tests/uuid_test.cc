// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fmt10/format.h>
#include <gsl/util>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory_resource>
#include <string>
#include <string_view>

namespace jewels::tests
{
namespace
{

/// Test tag
struct TestTag
{
};

TEST_CASE("Size / alignment")
{
  // Size and alignment is required to match the tachyon registry.
  STATIC_REQUIRE(uuid_alignment == 8UL);
  STATIC_REQUIRE(Uuid<TestTag>::uuid_size_bytes == 16UL);
  STATIC_REQUIRE(sizeof(Uuid<TestTag>) == Uuid<TestTag>::uuid_size_bytes);
  STATIC_REQUIRE(alignof(Uuid<TestTag>) == uuid_alignment);
}

TEST_CASE("Constructor")
{
  const Uuid<TestTag> uuid{};
  CHECK(uuid.is_nil());
}

TEST_CASE("Comparison operators")
{
  Uuid<TestTag> uuid1;
  std::memset(&uuid1.uuid, 1, sizeof(uuid1));
  Uuid<TestTag> uuid2;
  std::memset(&uuid2.uuid, 2, sizeof(uuid2));
  CHECK(uuid1 == uuid1);
  CHECK_FALSE(uuid1 != uuid1);
  CHECK_FALSE(uuid1 < uuid1);
  CHECK(uuid2 == uuid2);
  CHECK_FALSE(uuid2 != uuid2);
  CHECK_FALSE(uuid2 < uuid2);
  CHECK_FALSE(uuid1 == uuid2);
  CHECK(uuid1 != uuid2);
  CHECK(uuid1 < uuid2);
  CHECK_FALSE(uuid2 == uuid1);
  CHECK(uuid2 != uuid1);
  CHECK_FALSE(uuid2 < uuid1);
}

TEST_CASE("formatting")
{
  Uuid<TestTag> uuid{};
  const std::array<uint8_t, 16U> uuid_arr{
    0x01U, 0x23U, 0x45U, 0x67U, 0x89U, 0xabU, 0xcdU, 0xefU, 0xfeU, 0xdcU, 0xbaU, 0x98U, 0x76U, 0x54U, 0x32U, 0x10U};
  uuid.uuid = uuid_arr;
  CHECK(fmt::format("{}", uuid) == "01234567-89ab-cdef-fedc-ba9876543210");
  CHECK(fmt::format("{:x}", uuid) == "01234567-89ab-cdef-fedc-ba9876543210");
  CHECK(fmt::format("{:X}", uuid) == "01234567-89AB-CDEF-FEDC-BA9876543210");
}

TEST_CASE("To string")
{
  Uuid<TestTag> uuid{};
  const memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  CHECK(uuid.to_string() == "00000000-0000-0000-0000-000000000000");
  CHECK(uuid.to_string(memory_resource) == "00000000-0000-0000-0000-000000000000");
  const std::array<uint8_t, 16U> uuid_arr{
    0x01U, 0x23U, 0x45U, 0x67U, 0x89U, 0xabU, 0xcdU, 0xefU, 0xfeU, 0xdcU, 0xbaU, 0x98U, 0x76U, 0x54U, 0x32U, 0x10U};
  uuid.uuid = uuid_arr;
  CHECK(uuid.to_string() == "01234567-89ab-cdef-fedc-ba9876543210");
  CHECK(uuid.to_string(memory_resource) == "01234567-89ab-cdef-fedc-ba9876543210");
}

TEST_CASE("From string")
{
  SECTION("Runtime")
  {
    auto expected_uuid = Uuid<TestTag>::from_string("0123456789abcdeffedcba9876543210"); // pragma: allowlist secret
    REQUIRE(expected_uuid);
    CHECK(expected_uuid->to_string() == "01234567-89ab-cdef-fedc-ba9876543210");
    expected_uuid = Uuid<TestTag>::from_string("12345670-9ab8-defc-edcf-a9876543210b");
    REQUIRE(expected_uuid);
    CHECK(expected_uuid->to_string() == "12345670-9ab8-defc-edcf-a9876543210b");
    expected_uuid = Uuid<TestTag>::from_string("{23456701ab89efcddcfe9876543210ba}");
    REQUIRE(expected_uuid);
    CHECK(expected_uuid->to_string() == "23456701-ab89-efcd-dcfe-9876543210ba");
    expected_uuid = Uuid<TestTag>::from_string("{34567012-b89a-fcde-cfed-876543210ba9}");
    REQUIRE(expected_uuid);
    CHECK(expected_uuid->to_string() == "34567012-b89a-fcde-cfed-876543210ba9");
    CHECK_FALSE(Uuid<TestTag>::from_string(""));
    CHECK_FALSE(Uuid<TestTag>::from_string("11111111-11111111-1111-111111111111"));
    CHECK_FALSE(Uuid<TestTag>::from_string("11111111-1111-1111-1111111111111111"));
    CHECK_FALSE(Uuid<TestTag>::from_string("{11111111-11111111-1111-111111111111}"));
    CHECK_FALSE(Uuid<TestTag>::from_string("{11111111-1111-1111-1111111111111111}"));
    CHECK_FALSE(Uuid<TestTag>::from_string("{11111111-1111-1111-1111-1111111111111"));
    CHECK_FALSE(Uuid<TestTag>::from_string("{11111111-1111-1111-1111-1111111111111}"));
    CHECK_FALSE(Uuid<TestTag>::from_string("11111111-1111-1111-1111-1111111111111"));
    CHECK_FALSE(Uuid<TestTag>::from_string("111111X1-1111-1111-1111-1111111111111"));
  }
  SECTION("Constexpr")
  {
    constexpr auto uuid_str = std::string_view{"01234567-89ab-cdef-fedc-ba9876543210"};
    constexpr auto expected_uuid = Uuid<TestTag>::from_string(uuid_str); // pragma: allowlist secret
    STATIC_REQUIRE(expected_uuid);
    constexpr auto uuid = *expected_uuid;
    REQUIRE(uuid.to_string() == uuid_str);

    constexpr auto uuid_lt_str = std::string_view{"01234567-89ab-cdef-fedc-ba9876540000"};
    constexpr auto uuid_lt = *Uuid<TestTag>::from_string(uuid_lt_str);
    STATIC_REQUIRE(uuid == uuid);
    STATIC_REQUIRE(uuid_lt != uuid);
    STATIC_REQUIRE(uuid_lt < uuid);
  }
}

TEST_CASE("Random")
{
  const auto uuid1 = Uuid<TestTag>::random_uuid();
  for (size_t i = 0U; i < 1000U; ++i)
  {
    CAPTURE(i);
    const auto uuid2 = Uuid<TestTag>::random_uuid();
    std::cerr << uuid2 << '\n';
    CHECK(uuid2 != uuid1);
  }
}

} // namespace
} // namespace jewels::tests
