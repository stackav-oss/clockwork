// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/tap/soa.hh"
#include "jewels/container/tap/tests/support/test_point.hh"
#include "jewels/container/tap/tests/support/test_point_soa.hh"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iterator>
#include <span>
#include <stdexcept>

using jewels::tap::testing::TestPoint;
using jewels::tap::testing::TestPointSoa;

TEST_CASE("TestPointSoa - Basic construction and size", "[soa]")
{
  TestPointSoa<10> soa;

  REQUIRE(soa.size() == 0);
  REQUIRE(soa.capacity() == 10);
  REQUIRE(soa.empty());
}

TEST_CASE("TestPointSoa - Element access via operator[]", "[soa]")
{
  TestPointSoa<10> soa;
  soa.resize(3);

  REQUIRE(soa.size() == 3);
  REQUIRE_FALSE(soa.empty());

  // Set values via operator[]
  soa[0].x() = 1.0f;
  soa[0].y() = 2.0f;
  soa[0].z() = 3.0f;
  soa[0].id() = 42;

  soa[1].x() = 4.0f;
  soa[1].y() = 5.0f;
  soa[1].z() = 6.0f;
  soa[1].id() = 43;

  // Verify values
  REQUIRE(soa[0].x() == 1.0f);
  REQUIRE(soa[0].y() == 2.0f);
  REQUIRE(soa[0].z() == 3.0f);
  REQUIRE(soa[0].id() == 42);

  REQUIRE(soa[1].x() == 4.0f);
  REQUIRE(soa[1].y() == 5.0f);
  REQUIRE(soa[1].z() == 6.0f);
  REQUIRE(soa[1].id() == 43);
}

TEST_CASE("TestPointSoa - ElementRef assignment from TapInit", "[soa]")
{
  TestPointSoa<10> soa;
  soa.resize(2);

  // Initialize first element
  soa[0].x() = 1.0f;
  soa[0].y() = 2.0f;
  soa[0].z() = 3.0f;
  soa[0].id() = 10;

  // Assign TapInit to second element via ElementRef
  soa[1] = clockwork::TapInit<TestPoint>{.x = 7.5f, .y = 8.5f, .z = 9.5f, .id = 99};

  // Verify assignment worked
  REQUIRE(soa[1].x() == 7.5f);
  REQUIRE(soa[1].y() == 8.5f);
  REQUIRE(soa[1].z() == 9.5f);
  REQUIRE(soa[1].id() == 99);

  // Verify first element unchanged
  REQUIRE(soa[0].x() == 1.0f);
  REQUIRE(soa[0].y() == 2.0f);
  REQUIRE(soa[0].z() == 3.0f);
  REQUIRE(soa[0].id() == 10);
}

TEST_CASE("TestPointSoa - at() with output parameter", "[soa]")
{
  TestPointSoa<10> soa;
  soa.resize(2);

  soa[0].x() = 10.0f;
  soa[1].y() = 20.0f;

  SECTION("Valid index")
  {
    jewels::FactoryResult<TestPointSoa<10>::ElementRef> element;
    auto result = soa.at(jewels::Out{element}, 0);
    REQUIRE(jewels::ok(result));
    REQUIRE(element->x() == 10.0f);
  }

  SECTION("Invalid index")
  {
    jewels::FactoryResult<TestPointSoa<10>::ElementRef> element;
    auto result = soa.at(jewels::Out{element}, 5);
    REQUIRE(jewels::fails(result));
  }

  SECTION("Const version")
  {
    const auto& const_soa = soa;
    jewels::FactoryResult<TestPointSoa<10>::ElementConstRef> element;
    auto result = const_soa.at(jewels::Out{element}, 1);
    REQUIRE(jewels::ok(result));
    REQUIRE(element->y() == 20.0f);
  }
}

TEST_CASE("TestPointSoa - resize and try_resize", "[soa]")
{
  TestPointSoa<5> soa;

  SECTION("Resize within capacity")
  {
    soa.resize(3);
    REQUIRE(soa.size() == 3);

    soa.resize(5);
    REQUIRE(soa.size() == 5);

    soa.resize(2);
    REQUIRE(soa.size() == 2);
  }

  SECTION("Resize beyond capacity throws")
  {
    REQUIRE_THROWS_AS(soa.resize(6), std::length_error);
  }

  SECTION("try_resize within capacity")
  {
    auto result = soa.try_resize(3);
    REQUIRE(jewels::ok(result));
    REQUIRE(soa.size() == 3);
  }

  SECTION("try_resize beyond capacity returns failure")
  {
    auto result = soa.try_resize(10);
    REQUIRE(jewels::fails(result));
    REQUIRE(soa.size() == 0); // Size unchanged
  }
}

TEST_CASE("TestPointSoa - clear", "[soa]")
{
  TestPointSoa<10> soa;
  soa.resize(5);
  soa[0].x() = 100.0f;

  REQUIRE(soa.size() == 5);

  soa.clear();

  REQUIRE(soa.size() == 0);
  REQUIRE(soa.empty());
}

TEST_CASE("TestPointSoa - emplace_back default construction", "[soa]")
{
  TestPointSoa<10> soa;

  SECTION("Successful emplace")
  {
    auto ref = soa.emplace_back();
    REQUIRE(soa.size() == 1);
    REQUIRE(ref.x() == 0.0f);
    REQUIRE(ref.y() == 0.0f);
    REQUIRE(ref.z() == 0.0f);
    REQUIRE(ref.id() == 0);
  }

  SECTION("Emplace at capacity throws")
  {
    soa.resize(10);
    REQUIRE_THROWS_AS(soa.emplace_back(), std::length_error);
  }
}

TEST_CASE("TestPointSoa - emplace_back from TestPoint", "[soa]")
{
  TestPointSoa<10> soa;
  TestPoint point{.x = 1.5f, .y = 2.5f, .z = 3.5f, .id = 99};

  auto ref = soa.emplace_back(point);

  REQUIRE(soa.size() == 1);
  REQUIRE(ref.x() == 1.5f);
  REQUIRE(ref.y() == 2.5f);
  REQUIRE(ref.z() == 3.5f);
  REQUIRE(ref.id() == 99);
}

TEST_CASE("TestPointSoa - emplace_back from ElementRef", "[soa]")
{
  TestPointSoa<10> soa;
  soa.resize(1);
  soa[0].x() = 7.0f;
  soa[0].y() = 8.0f;
  soa[0].z() = 9.0f;
  soa[0].id() = 77;

  auto ref = soa.emplace_back(soa[0]);

  REQUIRE(soa.size() == 2);
  REQUIRE(ref.x() == 7.0f);
  REQUIRE(ref.y() == 8.0f);
  REQUIRE(ref.z() == 9.0f);
  REQUIRE(ref.id() == 77);
}

TEST_CASE("TestPointSoa - try_emplace_back default construction", "[soa]")
{
  TestPointSoa<5> soa;

  SECTION("Successful try_emplace")
  {
    jewels::FactoryResult<TestPointSoa<5>::ElementRef> element;
    auto result = soa.try_emplace_back(jewels::OptionalOut(element));
    REQUIRE(jewels::ok(result));
    REQUIRE(soa.size() == 1);
    REQUIRE(element->x() == 0.0f);
  }

  SECTION("try_emplace at capacity returns failure")
  {
    soa.resize(5);
    jewels::FactoryResult<TestPointSoa<5>::ElementRef> element;
    auto result = soa.try_emplace_back(jewels::OptionalOut(element));
    REQUIRE(jewels::fails(result));
    REQUIRE(soa.size() == 5);
  }
}

TEST_CASE("TestPointSoa - try_emplace_back from TestPoint", "[soa]")
{
  TestPointSoa<10> soa;
  TestPoint point{.x = 1.5f, .y = 2.5f, .z = 3.5f, .id = 99};

  jewels::FactoryResult<TestPointSoa<10>::ElementRef> element;
  auto result = soa.try_emplace_back(point, jewels::OptionalOut(element));

  REQUIRE(jewels::ok(result));
  REQUIRE(soa.size() == 1);
  REQUIRE(element->x() == 1.5f);
  REQUIRE(element->y() == 2.5f);
  REQUIRE(element->z() == 3.5f);
  REQUIRE(element->id() == 99);
}

TEST_CASE("TestPointSoa - try_emplace_back from TapInit", "[soa]")
{
  TestPointSoa<10> soa;

  jewels::FactoryResult<TestPointSoa<10>::ElementRef> element;
  auto result = soa.try_emplace_back(
    clockwork::TapInit<TestPoint>{.x = 4.5f, .y = 5.5f, .z = 6.5f, .id = 42}, jewels::OptionalOut(element));

  REQUIRE(jewels::ok(result));
  REQUIRE(soa.size() == 1);
  REQUIRE(element->x() == 4.5f);
  REQUIRE(element->y() == 5.5f);
  REQUIRE(element->z() == 6.5f);
  REQUIRE(element->id() == 42);
}

TEST_CASE("TestPointSoa - try_emplace_back from ElementRef", "[soa]")
{
  TestPointSoa<10> soa;
  soa.resize(1);
  soa[0].x() = 7.0f;
  soa[0].y() = 8.0f;
  soa[0].z() = 9.0f;
  soa[0].id() = 77;

  jewels::FactoryResult<TestPointSoa<10>::ElementRef> element;
  auto result = soa.try_emplace_back(soa[0], jewels::OptionalOut(element));

  REQUIRE(jewels::ok(result));
  REQUIRE(soa.size() == 2);
  REQUIRE(element->x() == 7.0f);
  REQUIRE(element->y() == 8.0f);
  REQUIRE(element->z() == 9.0f);
  REQUIRE(element->id() == 77);
}

TEST_CASE("TestPointSoa - Equality comparison", "[soa]")
{
  TestPointSoa<10> soa1;
  TestPointSoa<10> soa2;

  SECTION("Empty SOAs are equal")
  {
    REQUIRE(soa1 == soa2);
  }

  SECTION("SOAs with same data are equal")
  {
    soa1.resize(2);
    soa2.resize(2);
    soa1[0].x() = 1.0f;
    soa1[0].y() = 2.0f;
    soa2[0].x() = 1.0f;
    soa2[0].y() = 2.0f;

    REQUIRE(soa1 == soa2);
  }

  SECTION("SOAs with different sizes are not equal")
  {
    soa1.resize(2);
    soa2.resize(3);

    REQUIRE_FALSE(soa1 == soa2);
  }

  SECTION("SOAs with different data are not equal")
  {
    soa1.resize(1);
    soa2.resize(1);
    soa1[0].x() = 1.0f;
    soa2[0].x() = 2.0f;

    REQUIRE_FALSE(soa1 == soa2);
  }
}

TEST_CASE("TestPointSoa - Direct array data access", "[soa]")
{
  TestPointSoa<10> soa;
  soa.resize(3);

  // Direct array access for bulk operations
  auto x_array = soa.view_x();
  x_array[0] = 100.0f;
  x_array[1] = 200.0f;
  x_array[2] = 300.0f;

  REQUIRE(soa[0].x() == 100.0f);
  REQUIRE(soa[1].x() == 200.0f);
  REQUIRE(soa[2].x() == 300.0f);
}

TEST_CASE("TestPointSoa - ElementRef swap", "[soa]")
{
  TestPointSoa<10> soa;
  soa.resize(2);

  soa[0].x() = 1.0f;
  soa[0].y() = 2.0f;
  soa[0].z() = 3.0f;
  soa[0].id() = 10;

  soa[1].x() = 10.0f;
  soa[1].y() = 20.0f;
  soa[1].z() = 30.0f;
  soa[1].id() = 100;

  SECTION("swap via ADL")
  {
    auto ref0 = soa[0];
    auto ref1 = soa[1];

    swap(ref0, ref1);

    REQUIRE(soa[0].x() == 10.0f);
    REQUIRE(soa[0].y() == 20.0f);
    REQUIRE(soa[0].z() == 30.0f);
    REQUIRE(soa[0].id() == 100);

    REQUIRE(soa[1].x() == 1.0f);
    REQUIRE(soa[1].y() == 2.0f);
    REQUIRE(soa[1].z() == 3.0f);
    REQUIRE(soa[1].id() == 10);
  }

  SECTION("swap via using std::swap with ADL")
  {
    using std::swap;
    auto ref0 = soa[0];
    auto ref1 = soa[1];

    swap(ref0, ref1);

    REQUIRE(soa[0].x() == 10.0f);
    REQUIRE(soa[0].y() == 20.0f);
    REQUIRE(soa[0].z() == 30.0f);
    REQUIRE(soa[0].id() == 100);

    REQUIRE(soa[1].x() == 1.0f);
    REQUIRE(soa[1].y() == 2.0f);
    REQUIRE(soa[1].z() == 3.0f);
    REQUIRE(soa[1].id() == 10);
  }

  SECTION("swap via std::swap qualified call")
  {
    auto ref0 = soa[0];
    auto ref1 = soa[1];

    std::swap(ref0, ref1);

    REQUIRE(soa[0].x() == 10.0f);
    REQUIRE(soa[0].y() == 20.0f);
    REQUIRE(soa[0].z() == 30.0f);
    REQUIRE(soa[0].id() == 100);

    REQUIRE(soa[1].x() == 1.0f);
    REQUIRE(soa[1].y() == 2.0f);
    REQUIRE(soa[1].z() == 3.0f);
    REQUIRE(soa[1].id() == 10);
  }

  SECTION("swap via iterator with iter_swap")
  {
    auto it0 = soa.begin();
    auto it1 = std::next(it0);

    std::iter_swap(it0, it1);

    REQUIRE(soa[0].x() == 10.0f);
    REQUIRE(soa[0].y() == 20.0f);
    REQUIRE(soa[0].z() == 30.0f);
    REQUIRE(soa[0].id() == 100);

    REQUIRE(soa[1].x() == 1.0f);
    REQUIRE(soa[1].y() == 2.0f);
    REQUIRE(soa[1].z() == 3.0f);
    REQUIRE(soa[1].id() == 10);
  }

  SECTION("swap via iterator dereference with iter_swap")
  {
    auto it0 = soa.begin();
    auto it1 = std::next(it0);

    std::iter_swap(it0, it1);

    REQUIRE(soa[0].x() == 10.0f);
    REQUIRE(soa[0].y() == 20.0f);
    REQUIRE(soa[0].z() == 30.0f);
    REQUIRE(soa[0].id() == 100);

    REQUIRE(soa[1].x() == 1.0f);
    REQUIRE(soa[1].y() == 2.0f);
    REQUIRE(soa[1].z() == 3.0f);
    REQUIRE(soa[1].id() == 10);
  }
}
