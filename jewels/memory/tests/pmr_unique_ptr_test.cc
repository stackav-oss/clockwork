// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/monitor_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <utility>

namespace jewels::memory::testing
{
namespace
{

struct Thrower
{
  Thrower()
  {
    throw std::runtime_error("...");
  }
};

struct MoveOnlyInt
{
  explicit MoveOnlyInt(int input)
    : value(input)
  {
  }

  ~MoveOnlyInt() = default;
  MoveOnlyInt(const MoveOnlyInt&) = delete;
  MoveOnlyInt& operator=(const MoveOnlyInt&) = delete;
  MoveOnlyInt(MoveOnlyInt&&) = default;
  MoveOnlyInt& operator=(MoveOnlyInt&&) = default;

  int value;
};

} // namespace

TEST_CASE("unique_pmr_ptr")
{
  CHECK_THROWS(make_pmr_unique<int64_t>(jewels::memory::MemoryResource(std::pmr::null_memory_resource()), 6));

  jewels::memory::MonitorResource memory;
  const jewels::memory::MemoryResource resource(&memory);
  SECTION("int64_t")
  {
    int64_t ref = 6;
    auto ptr = make_pmr_unique<decltype(ref)>(resource, ref);
    CHECK(memory.used() == sizeof(ref));
    REQUIRE(ptr);
    CHECK(*ptr == ref);
  }

  SECTION("throwing constructor")
  {
    CHECK_THROWS(make_pmr_unique<Thrower>(jewels::memory::MemoryResource(std::pmr::null_memory_resource())));
    CHECK(memory.used() == 0);
  }

  SECTION("move forwarding constructor")
  {
    MoveOnlyInt ref{4}; // NOLINT(misc-const-correctness) definitely can't be declared const
    auto ptr = make_pmr_unique<decltype(ref)>(resource, std::move(ref));
    REQUIRE(ptr);
  }

  SECTION("default ctor")
  {
    const jewels::memory::pmr_unique_ptr<int64_t> null;
    REQUIRE_FALSE(null);
  }

  SECTION("move assignment")
  {
    int64_t ref = 6;
    jewels::memory::pmr_unique_ptr<int64_t> ptr;
    REQUIRE_FALSE(ptr);

    ptr = make_pmr_unique<decltype(ref)>(resource, ref);
    CHECK(memory.used() == sizeof(ref));
    REQUIRE(ptr);
    CHECK(*ptr == ref);

    int64_t ref2 = 7;
    ptr = make_pmr_unique<decltype(ref2)>(resource, ref2);
    CHECK(memory.used() == sizeof(ref2));
    REQUIRE(ptr);
    CHECK(*ptr == ref2);

    ptr = {};
    REQUIRE_FALSE(ptr);
  }

  CHECK(memory.used() == 0);
}

} // namespace jewels::memory::testing
