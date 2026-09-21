// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/serialization/tests/support/soa_test_schema_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/tap/soa.hh"
#include "jewels/container/tap/var_array.hh"

#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
#include <stdexcept>

namespace
{

// Helper to create a test Position
auto make_position(double x_val, double y_val, double z_val)
  -> clockwork::Tap<clockwork::Tachyon<clockwork::test::Position>>
{
  using clockwork::Tachyon;
  using clockwork::Tap;
  using clockwork::TapInit;
  using clockwork::test::Position;
  return Tap<Tachyon<Position>>{TapInit<Tachyon<Position>>{.x = x_val, .y = y_val, .z = z_val}};
}

// Helper to create a test SoaTestSchema
auto make_test_schema(uint32_t identifier, double x_val, double y_val, double z_val)
  -> clockwork::Tap<clockwork::Tachyon<clockwork::test::SoaTestSchema>>
{
  // Initialize with default VarArray (empty)
  using clockwork::Tachyon;
  using clockwork::Tap;
  using clockwork::TapInit;
  using clockwork::test::SoaTestSchema;
  clockwork::TapInit<clockwork::Tachyon<clockwork::test::SoaTestSchema>> init{
    .id = identifier,
    .velocities = jewels::tap::VarArray<float, 16U>{},
    .position = make_position(x_val, y_val, z_val),
    .seen_for = std::chrono::nanoseconds{1000},
  };
  return clockwork::Tap<clockwork::Tachyon<clockwork::test::SoaTestSchema>>{init};
}
} // namespace

TEST_CASE("FixedSoa basic properties", "[soa]")
{
  constexpr size_t size_val = 5;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  SECTION("size returns fixed size")
  {
    REQUIRE(soa.size() == size_val);
  }

  SECTION("capacity returns fixed size")
  {
    REQUIRE(soa.capacity() == size_val);
  }

  SECTION("empty returns false for non-zero size")
  {
    REQUIRE_FALSE(soa.empty());
  }
}

TEST_CASE("FixedSoa with size 0 is empty", "[soa]")
{
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, 0> soa;
  REQUIRE(soa.empty());
  REQUIRE(soa.size() == 0);
  REQUIRE(soa.capacity() == 0);
}

TEST_CASE("VarSoa basic properties", "[soa]")
{
  constexpr size_t max_size_val = 10;
  clockwork::VarSoa<clockwork::test::SoaTestSchema, max_size_val> soa;

  SECTION("initial size is 0")
  {
    REQUIRE(soa.size() == 0);
    REQUIRE(soa.empty());
  }

  SECTION("capacity returns max_size")
  {
    REQUIRE(soa.capacity() == max_size_val);
  }
}

TEST_CASE("ElementRef field accessors", "[soa][element_ref]")
{
  constexpr size_t size_val = 3;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  auto elem = soa[0];

  SECTION("can set and get primitive field (id)")
  {
    elem.set_id(42);
    REQUIRE(elem.get_id() == 42);

    uint32_t& id_ref = elem.get_mutable_id();
    id_ref = 99;
    REQUIRE(elem.get_id() == 99);
  }

  SECTION("can set and get strong primitive field (seen_for)")
  {
    elem.set_seen_for(std::chrono::nanoseconds{5000});
    REQUIRE(elem.get_seen_for() == std::chrono::nanoseconds{5000});

    auto& seen_for_ref = elem.get_mutable_seen_for();
    seen_for_ref = std::chrono::nanoseconds{7000};
    REQUIRE(elem.get_seen_for() == std::chrono::nanoseconds{7000});
  }

  SECTION("can set and get subschema field (position)")
  {
    auto pos = make_position(1.0, 2.0, 3.0);
    elem.set_position(pos);

    const auto& retrieved = elem.get_position();
    REQUIRE(retrieved.get_x() == 1.0);
    REQUIRE(retrieved.get_y() == 2.0);
    REQUIRE(retrieved.get_z() == 3.0);

    auto& pos_ref = elem.get_mutable_position();
    pos_ref.set_x(10.0);
    REQUIRE(elem.get_position().get_x() == 10.0);
  }
  SECTION("can access VarArray field (velocities)")
  {
    // Set via underlying VarArray
    [[maybe_unused]] auto result = elem.get_underlying_velocities().try_set(std::span<const float>({1.5f, 2.5f, 3.5f}));

    // Get as span
    auto vel_span = elem.get_velocities();
    REQUIRE(vel_span.size() == 3);
    REQUIRE_THAT(vel_span[0], Catch::Matchers::WithinRel(1.5f));
    REQUIRE_THAT(vel_span[1], Catch::Matchers::WithinRel(2.5f));
    REQUIRE_THAT(vel_span[2], Catch::Matchers::WithinRel(3.5f));

    // Try_set with too many elements should fail
    std::array<float, 20> too_many{};
    REQUIRE(jewels::fails(elem.try_set_velocities(std::span{too_many})));
  }
}

TEST_CASE("ElementConstRef is read-only", "[soa][element_ref]")
{
  constexpr size_t size_val = 2;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  auto elem = soa[0];
  elem.set_id(123);
  elem.set_seen_for(std::chrono::nanoseconds{4000});

  const auto& const_soa = soa;
  auto const_elem = const_soa[0];

  SECTION("can read fields")
  {
    REQUIRE(const_elem.get_id() == 123);
    REQUIRE(const_elem.get_seen_for() == std::chrono::nanoseconds{4000});
  }

  SECTION("ElementConstRef can be constructed from ElementRef")
  {
    clockwork::test::SoaTestSchema_FixedSoaElementConstRef<clockwork::test::SoaTestSchema, size_val> const_ref(elem);
    REQUIRE(const_ref.get_id() == 123);
  }
}

TEST_CASE("ElementRef assignment from Tap", "[soa][element_ref]")
{
  constexpr size_t size_val = 2;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  auto tap = make_test_schema(42, 1.0, 2.0, 3.0);
  auto elem = soa[0];
  elem = tap;

  REQUIRE(elem.get_id() == 42);
  REQUIRE(elem.get_position().get_x() == 1.0);
  REQUIRE(elem.get_position().get_y() == 2.0);
  REQUIRE(elem.get_position().get_z() == 3.0);
}

TEST_CASE("ElementRef assignment from TapInit", "[soa][element_ref]")
{
  constexpr size_t size_val = 2;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  clockwork::TapInit<clockwork::Tachyon<clockwork::test::SoaTestSchema>> init{
    .id = 777,
    .velocities = jewels::tap::VarArray<float, 16U>{},
    .position = make_position(10.0, 20.0, 30.0),
    .seen_for = std::chrono::nanoseconds{9999},
  };

  auto elem = soa[0];
  elem = init;

  REQUIRE(elem.get_id() == 777);
  REQUIRE(elem.get_position().get_x() == 10.0);
  REQUIRE(elem.get_seen_for() == std::chrono::nanoseconds{9999});
}

TEST_CASE("ElementRef assignment from another ElementRef", "[soa][element_ref]")
{
  constexpr size_t size_val = 3;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  soa[0].set_id(100);
  soa[0].set_position(make_position(1.0, 2.0, 3.0));

  soa[1] = soa[0];

  REQUIRE(soa[1].get_id() == 100);
  REQUIRE(soa[1].get_position().get_x() == 1.0);
}

TEST_CASE("ElementRef conversion to Tap", "[soa][element_ref]")
{
  constexpr size_t size_val = 2;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  auto elem = soa[0];
  elem.set_id(555);
  elem.set_position(make_position(7.0, 8.0, 9.0));
  elem.set_seen_for(std::chrono::nanoseconds{3333});

  auto tap = static_cast<clockwork::Tap<clockwork::Tachyon<clockwork::test::SoaTestSchema>>>(elem);

  REQUIRE(tap.get_id() == 555);
  REQUIRE(tap.get_position().get_x() == 7.0);
  REQUIRE(tap.get_position().get_y() == 8.0);
  REQUIRE(tap.get_position().get_z() == 9.0);
  REQUIRE(tap.get_seen_for() == std::chrono::nanoseconds{3333});
}

TEST_CASE("ElementRef equality comparison", "[soa][element_ref]")
{
  constexpr size_t size_val = 3;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  soa[0].set_id(10);
  soa[0].set_position(make_position(1.0, 2.0, 3.0));

  soa[1].set_id(10);
  soa[1].set_position(make_position(1.0, 2.0, 3.0));

  soa[2].set_id(20);
  soa[2].set_position(make_position(1.0, 2.0, 3.0));

  REQUIRE(soa[0] == soa[1]);
  REQUIRE_FALSE(soa[0] == soa[2]);
}

TEST_CASE("swap ElementRef lvalue references", "[soa][swap]")
{
  constexpr size_t size_val = 3;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  soa[0].set_id(10);
  soa[0].set_position(make_position(1.0, 2.0, 3.0));

  soa[1].set_id(20);
  soa[1].set_position(make_position(4.0, 5.0, 6.0));

  auto ref0 = soa[0];
  auto ref1 = soa[1];
  swap(ref0, ref1);

  REQUIRE(soa[0].get_id() == 20);
  REQUIRE(soa[0].get_position().get_x() == 4.0);
  REQUIRE(soa[1].get_id() == 10);
  REQUIRE(soa[1].get_position().get_x() == 1.0);
}

TEST_CASE("swap ElementRef rvalue references", "[soa][swap]")
{
  constexpr size_t size_val = 3;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  soa[0].set_id(100);
  soa[1].set_id(200);

  swap(soa[0], soa[1]);

  REQUIRE(soa[0].get_id() == 200);
  REQUIRE(soa[1].get_id() == 100);
}

TEST_CASE("std::swap with ElementRef", "[soa][swap]")
{
  constexpr size_t size_val = 3;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  soa[0].set_id(111);
  soa[0].set_position(make_position(10.0, 20.0, 30.0));

  soa[1].set_id(222);
  soa[1].set_position(make_position(40.0, 50.0, 60.0));

  auto ref0 = soa[0];
  auto ref1 = soa[1];
  std::swap(ref0, ref1);

  REQUIRE(soa[0].get_id() == 222);
  REQUIRE(soa[0].get_position().get_x() == 40.0);
  REQUIRE(soa[1].get_id() == 111);
  REQUIRE(soa[1].get_position().get_x() == 10.0);
}

TEST_CASE("operator[] unchecked access", "[soa][access]")
{
  constexpr size_t size_val = 5;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  SECTION("non-const access")
  {
    soa[2].set_id(42);
    REQUIRE(soa[2].get_id() == 42);
  }

  SECTION("const access")
  {
    soa[3].set_id(99);
    const auto& const_soa = soa;
    REQUIRE(const_soa[3].get_id() == 99);
  }
}

TEST_CASE("at() with BinaryOutcome", "[soa][access]")
{
  constexpr size_t size_val = 3;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  SECTION("valid index returns success")
  {
    jewels::FactoryResult<decltype(soa[0])> elem;
    REQUIRE(jewels::ok(soa.at(jewels::Out{elem}, 1)));
    elem->set_id(55);
    REQUIRE(soa[1].get_id() == 55);
  }

  SECTION("invalid index returns failure")
  {
    jewels::FactoryResult<decltype(soa[0])> elem;
    REQUIRE(jewels::fails(soa.at(jewels::Out{elem}, 10)));
  }

  SECTION("const version")
  {
    soa[0].set_id(77);
    const auto& const_soa = soa;
    jewels::FactoryResult<decltype(const_soa[0])> elem;
    REQUIRE(jewels::ok(const_soa.at(jewels::Out{elem}, 0)));
    REQUIRE(elem->get_id() == 77);
  }
}

TEST_CASE("at() with exceptions", "[soa][access]")
{
  constexpr size_t size_val = 3;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  SECTION("valid index succeeds")
  {
    auto elem = soa.at(1);
    elem.set_id(123);
    REQUIRE(soa[1].get_id() == 123);
  }

  SECTION("invalid index throws")
  {
    REQUIRE_THROWS_AS(soa.at(10), std::out_of_range);
  }

  SECTION("const version")
  {
    soa[0].set_id(88);
    const auto& const_soa = soa;
    auto elem = const_soa.at(0);
    REQUIRE(elem.get_id() == 88);
  }
}

TEST_CASE("VarSoa resize and try_resize", "[soa][varsoa]")
{
  constexpr size_t max_size_val = 10;
  clockwork::VarSoa<clockwork::test::SoaTestSchema, max_size_val> soa;

  SECTION("resize within capacity succeeds")
  {
    soa.resize(5);
    REQUIRE(soa.size() == 5);
    REQUIRE_FALSE(soa.empty());
  }

  SECTION("resize beyond capacity throws")
  {
    REQUIRE_THROWS_AS(soa.resize(20), std::length_error);
  }

  SECTION("try_resize within capacity succeeds")
  {
    REQUIRE(jewels::ok(soa.try_resize(7)));
    REQUIRE(soa.size() == 7);
  }

  SECTION("try_resize beyond capacity fails")
  {
    REQUIRE(jewels::fails(soa.try_resize(15)));
    REQUIRE(soa.size() == 0); // Size unchanged
  }

  SECTION("resize to 0 empties the container")
  {
    soa.resize(5);
    soa.resize(0);
    REQUIRE(soa.empty());
  }
}

TEST_CASE("VarSoa clear", "[soa][varsoa]")
{
  constexpr size_t max_size_val = 10;
  clockwork::VarSoa<clockwork::test::SoaTestSchema, max_size_val> soa;

  soa.resize(5);
  REQUIRE(soa.size() == 5);

  soa.clear();
  REQUIRE(soa.empty());
  REQUIRE(soa.size() == 0);
}

TEST_CASE("VarSoa emplace_back variants", "[soa][varsoa]")
{
  constexpr size_t max_size_val = 5;
  clockwork::VarSoa<clockwork::test::SoaTestSchema, max_size_val> soa;

  SECTION("emplace_back default constructs")
  {
    auto elem = soa.emplace_back();
    REQUIRE(soa.size() == 1);
    elem.set_id(42);
    REQUIRE(soa[0].get_id() == 42);
  }

  SECTION("emplace_back from TapInit")
  {
    // Create a Tap instead of TapInit since emplace_back doesn't take TapInit directly
    auto tap = make_test_schema(99, 1.0, 2.0, 3.0);

    auto elem = soa.emplace_back();
    elem = tap;
    REQUIRE(soa.size() == 1);
    REQUIRE(elem.get_id() == 99);
    REQUIRE(soa[0].get_id() == 99);
  }
  SECTION("emplace_back beyond capacity throws")
  {
    for (size_t i = 0; i < max_size_val; ++i)
    {
      soa.emplace_back();
    }
    REQUIRE_THROWS_AS(soa.emplace_back(), std::length_error);
  }
}

TEST_CASE("VarSoa try_emplace_back variants", "[soa][varsoa]")
{
  constexpr size_t max_size_val = 3;
  clockwork::VarSoa<clockwork::test::SoaTestSchema, max_size_val> soa;

  SECTION("try_emplace_back succeeds within capacity")
  {
    jewels::FactoryResult<decltype(soa[0])> elem;
    REQUIRE(jewels::ok(soa.try_emplace_back(jewels::OptionalOut{elem})));
    REQUIRE(soa.size() == 1);
    elem->set_id(123);
    REQUIRE(soa[0].get_id() == 123);
  }

  SECTION("try_emplace_back without output parameter")
  {
    REQUIRE(jewels::ok(soa.try_emplace_back()));
    REQUIRE(soa.size() == 1);
  }

  SECTION("try_emplace_back from Tap")
  {
    auto tap = make_test_schema(888, 5.0, 6.0, 7.0);

    jewels::FactoryResult<decltype(soa[0])> elem;
    REQUIRE(jewels::ok(soa.try_emplace_back(jewels::OptionalOut{elem})));
    (*elem) = tap;
    REQUIRE(soa.size() == 1);
    REQUIRE(soa[0].get_id() == 888);
  }
  SECTION("try_emplace_back fails beyond capacity")
  {
    for (size_t i = 0; i < max_size_val; ++i)
    {
      REQUIRE(jewels::ok(soa.try_emplace_back()));
    }
    REQUIRE(jewels::fails(soa.try_emplace_back()));
    REQUIRE(soa.size() == max_size_val);
  }
}

TEST_CASE("view methods return correct spans", "[soa][views]")
{
  constexpr size_t size_val = 3;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  for (size_t i = 0; i < size_val; ++i)
  {
    soa[i].set_id(static_cast<uint32_t>(i * 10));
  }

  SECTION("non-const view")
  {
    auto id_view = soa.view_id();
    REQUIRE(id_view.size() == size_val);
    REQUIRE(id_view[0] == 0);
    REQUIRE(id_view[1] == 10);
    REQUIRE(id_view[2] == 20);

    // Modify through view
    id_view[1] = 99;
    REQUIRE(soa[1].get_id() == 99);
  }

  SECTION("const view")
  {
    const auto& const_soa = soa;
    auto id_view = const_soa.view_id();
    REQUIRE(id_view.size() == size_val);
    REQUIRE(id_view[0] == 0);
  }
}

TEST_CASE("iterators basic operations", "[soa][iterators]")
{
  constexpr size_t size_val = 5;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  for (size_t i = 0; i < size_val; ++i)
  {
    soa[i].set_id(static_cast<uint32_t>(i));
  }

  SECTION("begin and end")
  {
    auto iter = soa.begin();
    auto end = soa.end();
    REQUIRE(iter != end);
    REQUIRE(std::distance(iter, end) == size_val);
  }

  SECTION("const begin and end")
  {
    const auto& const_soa = soa;
    auto iter = const_soa.begin();
    auto end = const_soa.end();
    REQUIRE(std::distance(iter, end) == size_val);
  }

  SECTION("cbegin and cend")
  {
    auto iter = soa.cbegin();
    auto end = soa.cend();
    REQUIRE(std::distance(iter, end) == size_val);
  }
}

TEST_CASE("iterator dereference and increment", "[soa][iterators]")
{
  constexpr size_t size_val = 3;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  soa[0].set_id(10);
  soa[1].set_id(20);
  soa[2].set_id(30);

  auto iter = soa.begin();

  SECTION("dereference returns ElementRef")
  {
    auto elem = *iter;
    REQUIRE(elem.get_id() == 10);
  }

  SECTION("increment moves to next element")
  {
    ++iter;
    REQUIRE((*iter).get_id() == 20);
    ++iter;
    REQUIRE((*iter).get_id() == 30);
  }

  SECTION("post-increment")
  {
    auto old_it = iter++;
    REQUIRE((*old_it).get_id() == 10);
    REQUIRE((*iter).get_id() == 20);
  }
}

TEST_CASE("iterator random access", "[soa][iterators]")
{
  constexpr size_t size_val = 5;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  for (size_t i = 0; i < size_val; ++i)
  {
    soa[i].set_id(static_cast<uint32_t>(i * 10));
  }

  auto iter = soa.begin();

  SECTION("advance by n")
  {
    iter += 2;
    REQUIRE((*iter).get_id() == 20);
  }

  SECTION("subscript operator")
  {
    REQUIRE(iter[0].get_id() == 0);
    REQUIRE(iter[2].get_id() == 20);
    REQUIRE(iter[4].get_id() == 40);
  }

  SECTION("difference between iterators")
  {
    auto it2 = iter + 3;
    REQUIRE(it2 - iter == 3);
  }
}

TEST_CASE("reverse iterators", "[soa][iterators]")
{
  constexpr size_t size_val = 4;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  for (size_t i = 0; i < size_val; ++i)
  {
    soa[i].set_id(static_cast<uint32_t>(i));
  }

  SECTION("rbegin and rend")
  {
    auto iter = soa.rbegin();
    REQUIRE((*iter).get_id() == 3);
    ++iter;
    REQUIRE((*iter).get_id() == 2);
  }

  SECTION("const reverse iterators")
  {
    const auto& const_soa = soa;
    auto iter = const_soa.rbegin();
    REQUIRE((*iter).get_id() == 3);
  }

  SECTION("crbegin and crend")
  {
    auto iter = soa.crbegin();
    auto end = soa.crend();
    REQUIRE(std::distance(iter, end) == size_val);
  }
}

TEST_CASE("std::iter_swap with iterators", "[soa][iterators][swap]")
{
  constexpr size_t size_val = 3;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  soa[0].set_id(100);
  soa[0].set_position(make_position(1.0, 2.0, 3.0));

  soa[1].set_id(200);
  soa[1].set_position(make_position(4.0, 5.0, 6.0));

  auto it0 = soa.begin();
  auto it1 = it0 + 1;

  std::iter_swap(it0, it1);

  REQUIRE(soa[0].get_id() == 200);
  REQUIRE(soa[0].get_position().get_x() == 4.0);
  REQUIRE(soa[1].get_id() == 100);
  REQUIRE(soa[1].get_position().get_x() == 1.0);
}

TEST_CASE("SoA equality comparison", "[soa][comparison]")
{
  constexpr size_t size_val = 2;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa1;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa2;

  soa1[0].set_id(10);
  soa1[1].set_id(20);

  soa2[0].set_id(10);
  soa2[1].set_id(20);

  SECTION("equal SoAs compare equal")
  {
    bool are_equal = (soa1 == soa2);
    REQUIRE(are_equal);
  }

  SECTION("different SoAs compare not equal")
  {
    soa2[1].set_id(99);
    bool are_equal = (soa1 == soa2);
    REQUIRE_FALSE(are_equal);
  }
}

TEST_CASE("VarSoa equality only compares filled elements", "[soa][comparison]")
{
  constexpr size_t max_size_val = 5;
  clockwork::VarSoa<clockwork::test::SoaTestSchema, max_size_val> soa1;
  clockwork::VarSoa<clockwork::test::SoaTestSchema, max_size_val> soa2;

  soa1.resize(2);
  soa2.resize(2);

  soa1[0].set_id(10);
  soa1[1].set_id(20);

  soa2[0].set_id(10);
  soa2[1].set_id(20);

  bool are_equal = (soa1 == soa2);
  REQUIRE(are_equal);

  SECTION("different sizes are not equal")
  {
    soa2.resize(3);
    are_equal = (soa1 == soa2);
    REQUIRE_FALSE(are_equal);
  }
}

TEST_CASE("layout verification", "[soa][layout]")
{
  SECTION("FixedSoa layout verification")
  {
    // verify_layout is constexpr and only valid for size == 1
    using SoaType = clockwork::FixedSoa<clockwork::test::SoaTestSchema, 1>;
    SoaType::verify_layout();

    // Layout verification happens at compile time via verify_layout()
    REQUIRE(true);
  }

  SECTION("VarSoa layout verification")
  {
    // verify_layout is constexpr and only valid for max_size == 1
    using SoaType = clockwork::VarSoa<clockwork::test::SoaTestSchema, 1>;
    SoaType::verify_layout();

    // Layout verification happens at compile time via verify_layout()
    REQUIRE(true);
  }
}

TEST_CASE("const correctness throughout", "[soa][const]")
{
  constexpr size_t size_val = 2;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  soa[0].set_id(42);

  const auto& const_soa = soa;

  SECTION("const SoA provides const access")
  {
    auto const_elem = const_soa[0];
    REQUIRE(const_elem.get_id() == 42);

    // These should work (reading)
    [[maybe_unused]] auto identifier = const_elem.get_id();
    [[maybe_unused]] auto pos = const_elem.get_position();
  }

  SECTION("const iterators")
  {
    auto iter = const_soa.begin();
    REQUIRE((*iter).get_id() == 42);
  }

  SECTION("cbegin/cend provide const access")
  {
    auto iter = soa.cbegin();
    REQUIRE((*iter).get_id() == 42);
  }
}

TEST_CASE("algorithm compatibility", "[soa][algorithms]")
{
  constexpr size_t size_val = 5;
  clockwork::FixedSoa<clockwork::test::SoaTestSchema, size_val> soa;

  for (size_t i = 0; i < size_val; ++i)
  {
    soa[i].set_id(static_cast<uint32_t>(i));
  }

  SECTION("std::find_if")
  {
    auto iter = std::find_if(soa.begin(), soa.end(), [](auto elem) { return elem.get_id() == 3; });
    REQUIRE(iter != soa.end());
    REQUIRE((*iter).get_id() == 3);
  }

  SECTION("std::count_if")
  {
    auto count = std::count_if(soa.begin(), soa.end(), [](auto elem) { return elem.get_id() < 3; });
    REQUIRE(count == 3);
  }

  SECTION("std::for_each")
  {
    std::for_each(soa.begin(), soa.end(), [](auto elem) { elem.set_id(elem.get_id() * 10); });

    REQUIRE(soa[0].get_id() == 0);
    REQUIRE(soa[1].get_id() == 10);
    REQUIRE(soa[2].get_id() == 20);
  }
}

TEST_CASE("VarSoa schema clear", "[soa][varsoa]")
{
  const clockwork::Tappy<clockwork::test::SoaTestContainer> empty{};
  clockwork::Tappy<clockwork::test::SoaTestContainer> test{};

  // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) this also checks the 'unused' area
  REQUIRE(std::memcmp(&empty, &test, sizeof(empty)) == 0);

  test.clear();
  // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) this also checks the 'unused' area
  CHECK(std::memcmp(&empty, &test, sizeof(empty)) == 0);

  test.get_mutable_items().resize(2);
  test.get_mutable_items().at(0) = make_test_schema(1, 11., 12., 13.);
  test.get_mutable_items().at(1) = make_test_schema(2, 21., 22., 23.);
  // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) this also checks the 'unused' area
  CHECK(std::memcmp(&empty, &test, sizeof(empty)) != 0);

  test.clear();
  // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) this also checks the 'unused' area
  CHECK(std::memcmp(&empty, &test, sizeof(empty)) == 0);
}
