// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/std/utility.hh"
#include "jewels/utility/enum_flags.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace
{
using ::jewels::to_underlying;

enum class Enum8 : uint8_t
{
  zero = 0x00,
  a = 0x01,
  b = 0x02,
  c = 0x040,
};

JEWELS_ENABLE_ENUM_FLAGS(Enum8)

TEST_CASE("enum_flags test | basic")
{
  SECTION("binary ops")
  {
    CHECK(to_underlying(Enum8::a | Enum8::b | Enum8::c) == 0x43);
    CHECK(to_underlying((Enum8::a | Enum8::b | Enum8::c) & Enum8::b) == 0x02);
    CHECK(to_underlying(Enum8::a ^ Enum8::b) == 0x03);
    CHECK((Enum8::a - Enum8::b) == Enum8::a);
    CHECK((Enum8::a + Enum8::b) == (Enum8::a | Enum8::b));
    CHECK((Enum8::a + Enum8::b - Enum8::b) == Enum8::a);
    CHECK(((Enum8::a | Enum8::b) & Enum8::b) == Enum8::b);
    CHECK(((Enum8::a | Enum8::b) ^ Enum8::a) == Enum8::b);
  }
  SECTION("assignment ops")
  {
    Enum8 value_v = Enum8::zero;
    value_v |= Enum8::a;
    CHECK(value_v == Enum8::a);
    value_v ^= Enum8::a | Enum8::b;
    CHECK(value_v == Enum8::b);
    value_v ^= Enum8::a + Enum8::b;
    CHECK(value_v == Enum8::a);
    value_v &= Enum8::a | Enum8::b;
    CHECK(value_v == Enum8::a);
    value_v &= Enum8::c;
    CHECK(value_v == Enum8::zero);
    value_v += Enum8::a;
    CHECK(value_v == Enum8::a);
    value_v += Enum8::b;
    CHECK(value_v == (Enum8::a | Enum8::b));
    value_v -= Enum8::a;
    CHECK(value_v == Enum8::b);
  }
  SECTION("test ops")
  {
    CHECK(Enum8::b);
    CHECK_FALSE(Enum8::zero);
    CHECK(static_cast<bool>(Enum8::b));
    CHECK_FALSE(static_cast<bool>(Enum8::zero));
    CHECK(!Enum8::zero);
    CHECK(!!Enum8::b);
    CHECK_FALSE(!Enum8::b);
  }
}

namespace ns1
{

enum class Enum : uint8_t
{
  a = 0x01,
  b = 0x02,
};

JEWELS_ENABLE_ENUM_FLAGS(Enum)

} // namespace ns1

namespace ns2
{

TEST_CASE("enum_flags test | in namespace")
{
  // This is just a sanity check that operators are usable even when the enum is not defined in the same namespace
  CHECK(ns1::Enum::a - ns1::Enum::b == ns1::Enum::a);
}

} // namespace ns2

namespace ns3
{
struct NotEnum
{
  int x;
};

template <typename Other>
inline NotEnum operator+(NotEnum value_a, Other /*b*/)
{
  return value_a;
}

template <typename TypeA, typename TypeB>
inline TypeA operator-(TypeA value_a, TypeB /*b*/)
{
  return value_a;
}

TEST_CASE("enum_flags test | don't catch other types")
{
  // Ideally we would exhaustively test all non-compiling cases, but that isn't practical so instead we use
  // generic operator overloads to confirm that they are being used rather than the flags operators.
  CHECK((NotEnum{84} + NotEnum{2}).x == 84);
  CHECK((NotEnum{84} + 2).x == 84);
  CHECK((NotEnum{84} + ns1::Enum::a).x == 84);
  CHECK((NotEnum{84} - NotEnum{2}).x == 84);
  CHECK((NotEnum{84} - 2).x == 84);
  CHECK((NotEnum{84} - ns1::Enum::a).x == 84);
}
} // namespace ns3

} // namespace
