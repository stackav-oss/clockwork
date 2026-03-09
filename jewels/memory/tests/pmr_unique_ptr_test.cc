// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/monitor_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
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

  std::int32_t value;
};

struct Base
{
  Base() = default;

  virtual ~Base() = default;
  Base(const Base&) = default;
  Base& operator=(const Base&) = default;
  Base(Base&&) = default;
  Base& operator=(Base&&) = default;
};

struct alignas(alignof(std::max_align_t)) Derived final : public Base
{
  Derived()
  {
    ++active_instance_count_;
  }
  ~Derived() override
  {
    --active_instance_count_;
  }
  Derived(const Derived&) = default;
  Derived& operator=(const Derived&) = default;
  Derived(Derived&&) = default;
  Derived& operator=(Derived&&) = default;

  static size_t get_active_instance_count()
  {
    return active_instance_count_;
  }

  bool derived_value{false};

private:
  static size_t active_instance_count_;
};

size_t Derived::active_instance_count_ = 0;

// Properties must hold for the test to be valid
static_assert(sizeof(Base) < sizeof(Derived));
static_assert(alignof(Base) != alignof(Derived));

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

  SECTION("polymorphic conversion")
  {
    SECTION("Move construction")
    {
      auto derived_ptr = make_pmr_unique<Derived, true>(resource);
      REQUIRE(derived_ptr);
      CHECK(Derived::get_active_instance_count() == 1);

      jewels::memory::pmr_unique_ptr<Base, true> base_ptr = std::move(derived_ptr);
      REQUIRE(base_ptr);
      CHECK(Derived::get_active_instance_count() == 1);

      base_ptr = {};
      REQUIRE_FALSE(base_ptr);
      CHECK(Derived::get_active_instance_count() == 0);
    }

    SECTION("Move assignment")
    {
      auto derived_ptr = make_pmr_unique<Derived, true>(resource);
      REQUIRE(derived_ptr);
      CHECK(Derived::get_active_instance_count() == 1);

      jewels::memory::pmr_unique_ptr<Base, true> base_ptr;
      REQUIRE_FALSE(base_ptr);

      base_ptr = std::move(derived_ptr);
      REQUIRE(base_ptr);
      CHECK(Derived::get_active_instance_count() == 1);

      base_ptr = {};
      REQUIRE_FALSE(base_ptr);
      CHECK(Derived::get_active_instance_count() == 0);
    }
  }

  CHECK(memory.used() == 0);
}

} // namespace jewels::memory::testing
