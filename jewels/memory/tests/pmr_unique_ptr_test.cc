// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/callsig/outparam.hh"
#include "jewels/memory/allocator_deleter.hh"
#include "jewels/memory/instrumented_pmr_resource.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/new_delete_memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <type_traits>
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

} // namespace

struct SecondaryBase
{
  SecondaryBase() = default;

  virtual ~SecondaryBase() = default;
  SecondaryBase(const SecondaryBase&) = default;
  SecondaryBase& operator=(const SecondaryBase&) = default;
  SecondaryBase(SecondaryBase&&) = default;
  SecondaryBase& operator=(SecondaryBase&&) = default;
};

struct Base
{
  Base() = default;

  virtual ~Base() = default;
  Base(const Base&) = default;
  Base& operator=(const Base&) = default;
  Base(Base&&) = default;
  Base& operator=(Base&&) = default;

  std::uint64_t prefix_value{};
};

struct NonPolymorphicBase
{
};

struct alignas(alignof(std::max_align_t)) Derived final : public NonPolymorphicBase, public Base, public SecondaryBase
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

using PmrByteAlloc = std::pmr::polymorphic_allocator<std::byte>;
using PmrIntAlloc = std::pmr::polymorphic_allocator<std::int64_t>;
using StdByteAlloc = std::allocator<std::byte>;
using StdIntAlloc = std::allocator<std::int64_t>;

// Alias-template shape checks.
static_assert(std::is_same_v<
              jewels::memory::unique_ptr<std::int64_t>,
              std::unique_ptr<std::int64_t, jewels::memory::AllocatorDeleter<std::int64_t>>>);
static_assert(std::is_same_v<
              jewels::memory::unique_ptr<std::int64_t, true>,
              std::unique_ptr<std::int64_t, jewels::memory::AllocatorDeleter<std::int64_t, true>>>);
static_assert(std::is_same_v<
              jewels::memory::pmr_unique_ptr<std::int64_t>,
              std::unique_ptr<std::int64_t, jewels::memory::PmrDeleter<std::int64_t>>>);
static_assert(std::is_same_v<
              jewels::memory::pmr_unique_ptr<std::int64_t, true>,
              std::unique_ptr<std::int64_t, jewels::memory::PmrDeleter<std::int64_t, true>>>);
static_assert(
  std::is_same_v<jewels::memory::AllocatorDeleter<std::int64_t>, jewels::memory::MonomorphicDeleter<StdIntAlloc>>);
static_assert(std::is_same_v<
              jewels::memory::AllocatorDeleter<std::int64_t, true>,
              jewels::memory::PolymorphicDeleter<StdIntAlloc>>);
static_assert(
  std::is_same_v<jewels::memory::PmrDeleter<std::int64_t>, jewels::memory::MonomorphicDeleter<PmrIntAlloc>>);
static_assert(
  std::is_same_v<jewels::memory::PmrDeleter<std::int64_t, true>, jewels::memory::PolymorphicDeleter<PmrIntAlloc>>);

// Deleter alias must rebind allocator value_type to the pointee type.
static_assert(std::is_same_v<
              jewels::memory::AllocatorDeleter<std::int64_t, false, StdByteAlloc>,
              jewels::memory::MonomorphicDeleter<StdIntAlloc>>);
static_assert(std::is_same_v<
              jewels::memory::AllocatorDeleter<std::int64_t, true, StdByteAlloc>,
              jewels::memory::PolymorphicDeleter<StdIntAlloc>>);
static_assert(std::is_same_v<
              jewels::memory::AllocatorDeleter<std::int64_t, false, PmrByteAlloc>,
              jewels::memory::MonomorphicDeleter<PmrIntAlloc>>);
static_assert(std::is_same_v<
              jewels::memory::AllocatorDeleter<std::int64_t, true, PmrByteAlloc>,
              jewels::memory::PolymorphicDeleter<PmrIntAlloc>>);
static_assert(std::is_same_v<
              jewels::memory::unique_ptr<std::int64_t, false, StdByteAlloc>::deleter_type,
              jewels::memory::AllocatorDeleter<std::int64_t>>);
static_assert(std::is_same_v<
              jewels::memory::unique_ptr<std::int64_t, true, StdByteAlloc>::deleter_type,
              jewels::memory::AllocatorDeleter<std::int64_t, true>>);
static_assert(std::is_same_v<
              jewels::memory::unique_ptr<std::int64_t, false, PmrByteAlloc>::deleter_type,
              jewels::memory::PmrDeleter<std::int64_t>>);
static_assert(std::is_same_v<
              jewels::memory::unique_ptr<std::int64_t, true, PmrByteAlloc>::deleter_type,
              jewels::memory::PmrDeleter<std::int64_t, true>>);

// Function-template return-type checks, including allocator value_type mismatch paths.
static_assert(std::is_same_v<decltype(make_unique<std::int64_t>(6)), jewels::memory::unique_ptr<std::int64_t>>);
static_assert(
  std::is_same_v<decltype(make_unique<std::int64_t, true>(6)), jewels::memory::unique_ptr<std::int64_t, true>>);
static_assert(std::is_same_v<
              decltype(allocate_unique<std::int64_t>(std::declval<StdByteAlloc>(), 6)),
              jewels::memory::unique_ptr<std::int64_t>>);
static_assert(std::is_same_v<
              decltype(allocate_unique<std::int64_t, true>(std::declval<StdByteAlloc>())),
              jewels::memory::unique_ptr<std::int64_t, true>>);
static_assert(std::is_same_v<
              decltype(allocate_unique<std::int64_t>(std::declval<PmrByteAlloc>(), 6)),
              jewels::memory::pmr_unique_ptr<std::int64_t>>);
static_assert(std::is_same_v<
              decltype(allocate_unique<std::int64_t, true>(std::declval<PmrByteAlloc>())),
              jewels::memory::pmr_unique_ptr<std::int64_t, true>>);
static_assert(std::is_same_v<
              decltype(make_pmr_unique<std::int64_t>(std::declval<PmrByteAlloc>(), 6)),
              jewels::memory::pmr_unique_ptr<std::int64_t>>);
static_assert(std::is_same_v<
              decltype(make_polymorphic_pmr_unique<std::int64_t>(std::declval<PmrByteAlloc>())),
              jewels::memory::pmr_unique_ptr<std::int64_t, true>>);

// Default constructibility
static_assert(std::is_default_constructible_v<jewels::memory::unique_ptr<std::int64_t>>);
static_assert(!std::is_default_constructible_v<jewels::memory::unique_ptr<std::int64_t, true>>);
// Ideally this would be false since the polymorphic allocator is stateful, but this is currently a breaking change.
static_assert(std::is_default_constructible_v<jewels::memory::pmr_unique_ptr<std::int64_t>>);
static_assert(!std::is_default_constructible_v<jewels::memory::pmr_unique_ptr<std::int64_t, true>>);

TEST_CASE("unique_pmr_ptr")
{
  CHECK_THROWS(make_pmr_unique<int64_t>(jewels::memory::MemoryResource(std::pmr::null_memory_resource()), 6));

  jewels::memory::NewDeleteMemoryResource memory(0, "pmr_unique_ptr_test_memres");
  const jewels::memory::MemoryResource resource{memory};
  MemoryResourceMetrics metrics{};

  SECTION("int64_t")
  {
    int64_t ref = 6;
    auto ptr = make_pmr_unique<decltype(ref)>(resource, ref);
    memory.get_memory_resource_metrics(jewels::Out{metrics});
    CHECK(metrics.current_allocated == sizeof(ref));
    REQUIRE(ptr);
    CHECK(*ptr == ref);
  }

  SECTION("throwing constructor")
  {
    CHECK_THROWS(make_pmr_unique<Thrower>(jewels::memory::MemoryResource(std::pmr::null_memory_resource())));
    memory.get_memory_resource_metrics(jewels::Out{metrics});
    CHECK(metrics.current_allocated == 0);
  }

  SECTION("throwing constructor with mismatched pmr allocator value_type")
  {
    std::pmr::polymorphic_allocator<std::byte> alloc(resource);

    CHECK_THROWS(allocate_unique<Thrower>(alloc));
    memory.get_memory_resource_metrics(jewels::Out{metrics});
    CHECK(metrics.current_allocated == 0);
  }

  SECTION("move forwarding constructor")
  {
    MoveOnlyInt ref{4}; // NOLINT(misc-const-correctness) definitely can't be declared const
    auto ptr = make_pmr_unique<decltype(ref)>(resource, std::move(ref));
    REQUIRE(ptr);
  }

  SECTION("move assignment")
  {
    int64_t ref = 6;
    jewels::memory::PmrDeleter<int64_t> deleter(resource);
    jewels::memory::pmr_unique_ptr<int64_t> ptr(nullptr, deleter);
    REQUIRE_FALSE(ptr);

    ptr = make_pmr_unique<decltype(ref)>(resource, ref);
    memory.get_memory_resource_metrics(jewels::Out{metrics});
    CHECK(metrics.current_allocated == sizeof(ref));
    REQUIRE(ptr);
    CHECK(*ptr == ref);

    int64_t ref2 = 7;
    ptr = make_pmr_unique<decltype(ref2)>(resource, ref2);
    memory.get_memory_resource_metrics(jewels::Out{metrics});
    CHECK(metrics.current_allocated == sizeof(ref2));
    REQUIRE(ptr);
    CHECK(*ptr == ref2);

    ptr = {};
    REQUIRE_FALSE(ptr);
  }

  SECTION("make_unique uses std allocator")
  {
    auto ptr = make_unique<std::int64_t>(33);
    REQUIRE(ptr);
    CHECK(*ptr == 33);
  }

  SECTION("allocate_unique with std allocator")
  {
    StdIntAlloc alloc;
    auto ptr = allocate_unique<std::int64_t>(alloc, 44);
    REQUIRE(ptr);
    CHECK(*ptr == 44);
  }

  SECTION("allocate_unique with mismatched std allocator value_type")
  {
    std::allocator<double> alloc;
    auto ptr = allocate_unique<std::int64_t>(alloc, 66);
    REQUIRE(ptr);
    CHECK(*ptr == 66);
  }

  SECTION("allocate_unique with pmr allocator")
  {
    std::pmr::polymorphic_allocator<std::int64_t> alloc(resource);

    auto ptr = allocate_unique<std::int64_t>(alloc, 55);
    memory.get_memory_resource_metrics(jewels::Out{metrics});
    CHECK(metrics.current_allocated == sizeof(std::int64_t));
    REQUIRE(ptr);
    CHECK(*ptr == 55);

    ptr = {};
    memory.get_memory_resource_metrics(jewels::Out{metrics});
    CHECK(metrics.current_allocated == 0);
  }

  SECTION("allocate_unique with mismatched pmr allocator value_type")
  {
    std::pmr::polymorphic_allocator<std::byte> alloc(resource);

    auto ptr = allocate_unique<std::int64_t>(alloc, 77);
    memory.get_memory_resource_metrics(jewels::Out{metrics});
    CHECK(metrics.current_allocated == sizeof(std::int64_t));
    REQUIRE(ptr);
    CHECK(*ptr == 77);

    ptr = {};
    memory.get_memory_resource_metrics(jewels::Out{metrics});
    CHECK(metrics.current_allocated == 0);
  }

  SECTION("allocate_unique polymorphic deletion with mismatched pmr allocator value_type")
  {
    std::pmr::polymorphic_allocator<std::byte> alloc(resource);

    auto derived_ptr = allocate_unique<Derived, true>(alloc);
    memory.get_memory_resource_metrics(jewels::Out{metrics});
    CHECK(metrics.current_allocated == sizeof(Derived));
    REQUIRE(derived_ptr);
    CHECK(Derived::get_active_instance_count() == 1);

    jewels::memory::pmr_unique_ptr<Base, true> base_ptr = std::move(derived_ptr);
    REQUIRE(base_ptr);
    CHECK(Derived::get_active_instance_count() == 1);

    base_ptr = {};
    CHECK(Derived::get_active_instance_count() == 0);
    memory.get_memory_resource_metrics(jewels::Out{metrics});
    CHECK(metrics.current_allocated == 0);
  }

  SECTION("move assignment deletes destination object with destination resource")
  {
    jewels::memory::NewDeleteMemoryResource memory_2(0, "pmr_unique_ptr_test_memres_2");
    const jewels::memory::MemoryResource resource_2(&memory_2);
    MemoryResourceMetrics metrics_2{};

    auto ptr_1 = make_pmr_unique<std::int64_t>(resource, 11);
    auto ptr_2 = make_pmr_unique<std::int64_t>(resource_2, 22);

    memory.get_memory_resource_metrics(jewels::Out{metrics});
    memory_2.get_memory_resource_metrics(jewels::Out{metrics_2});
    CHECK(metrics.current_allocated == sizeof(std::int64_t));
    CHECK(metrics_2.current_allocated == sizeof(std::int64_t));

    ptr_1 = std::move(ptr_2);

    REQUIRE(ptr_1);
    CHECK(*ptr_1 == 22);
    REQUIRE_FALSE(ptr_2);
    memory.get_memory_resource_metrics(jewels::Out{metrics});
    memory_2.get_memory_resource_metrics(jewels::Out{metrics_2});
    CHECK(metrics.current_allocated == 0);
    CHECK(metrics_2.current_allocated == sizeof(std::int64_t));

    ptr_1 = {};
    memory.get_memory_resource_metrics(jewels::Out{metrics});
    memory_2.get_memory_resource_metrics(jewels::Out{metrics_2});
    CHECK(metrics.current_allocated == 0);
    CHECK(metrics_2.current_allocated == 0);
  }

  SECTION("deleter self move assignment")
  {
    std::pmr::polymorphic_allocator<std::int64_t> alloc(&memory);
    auto* ptr = alloc.allocate(1);
    std::allocator_traits<decltype(alloc)>::construct(alloc, ptr, 123);

    jewels::memory::PmrDeleter<std::int64_t> deleter(alloc);
// -Wself-move is suppressed here so we can test that the behavior of self-move assignment is still correct, even if it
// is prevented through a compiler warning in contexts like this.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#endif
    deleter = std::move(deleter);
#ifdef __clang__
#pragma clang diagnostic pop
#endif

    deleter(ptr);
    memory.get_memory_resource_metrics(jewels::Out{metrics});
    CHECK(metrics.current_allocated == 0);
  }

  SECTION("polymorphic conversion")
  {
    // upcast conversion disabled on MonomorphicDeleter
    static_assert(not std::is_constructible_v<
                  jewels::memory::MonomorphicDeleter<std::allocator<Base>>,
                  jewels::memory::MonomorphicDeleter<std::allocator<Derived>>>);
    // upcast conversion enabled on PolymorphicDeleter
    static_assert(std::is_constructible_v<
                  jewels::memory::PolymorphicDeleter<std::allocator<Base>>,
                  jewels::memory::PolymorphicDeleter<std::allocator<Derived>>>);
    // conversion between incompatible allocators disabled
    static_assert(not std::is_constructible_v<
                  jewels::memory::PolymorphicDeleter<std::pmr::polymorphic_allocator<Derived>>,
                  jewels::memory::PolymorphicDeleter<std::allocator<Derived>>>);
    // downcast conversion disabled
    static_assert(not std::is_constructible_v<
                  jewels::memory::PolymorphicDeleter<std::allocator<Derived>>,
                  jewels::memory::PolymorphicDeleter<std::allocator<Base>>>);
    // upcast conversion to base without virtual destructor disabled
    static_assert(not std::is_constructible_v<
                  jewels::memory::PolymorphicDeleter<std::allocator<NonPolymorphicBase>>,
                  jewels::memory::PolymorphicDeleter<std::allocator<Derived>>>);
    // upcast conversion requiring pointer adjustment disabled
    static_assert(not std::is_constructible_v<
                  jewels::memory::PolymorphicDeleter<std::allocator<SecondaryBase>>,
                  jewels::memory::PolymorphicDeleter<std::allocator<Derived>>>);

    SECTION("to_polymorphic helper")
    {
      auto mono_derived_ptr = make_pmr_unique<Derived>(resource);
      REQUIRE(mono_derived_ptr);
      CHECK(Derived::get_active_instance_count() == 1);

      auto poly_derived_ptr = to_polymorphic(std::move(mono_derived_ptr));
      REQUIRE_FALSE(mono_derived_ptr);
      REQUIRE(poly_derived_ptr);
      CHECK(Derived::get_active_instance_count() == 1);

      jewels::memory::pmr_unique_ptr<Base, true> base_ptr = std::move(poly_derived_ptr);
      REQUIRE_FALSE(poly_derived_ptr);
      REQUIRE(base_ptr);
      CHECK(Derived::get_active_instance_count() == 1);

      base_ptr = {};
      CHECK(Derived::get_active_instance_count() == 0);
    }

    SECTION("make_polymorphic_pmr_unique convenience wrapper")
    {
      auto derived_ptr = make_polymorphic_pmr_unique<Derived>(resource);
      REQUIRE(derived_ptr);
      CHECK(Derived::get_active_instance_count() == 1);

      jewels::memory::pmr_unique_ptr<Base, true> base_ptr = std::move(derived_ptr);
      REQUIRE_FALSE(derived_ptr);
      REQUIRE(base_ptr);
      CHECK(Derived::get_active_instance_count() == 1);

      base_ptr = {};
      CHECK(Derived::get_active_instance_count() == 0);
    }

    SECTION("Move construction")
    {
      auto derived_ptr = make_pmr_unique<Derived, true>(resource);
      REQUIRE(derived_ptr);
      CHECK(Derived::get_active_instance_count() == 1);

      jewels::memory::pmr_unique_ptr<Base, true> base_ptr = std::move(derived_ptr);
      REQUIRE_FALSE(derived_ptr);
      REQUIRE(base_ptr);
      CHECK(Derived::get_active_instance_count() == 1);

      base_ptr = {};
      CHECK(Derived::get_active_instance_count() == 0);
    }

    SECTION("Move assignment")
    {
      auto derived_ptr = make_pmr_unique<Derived, true>(resource);
      REQUIRE(derived_ptr);
      CHECK(Derived::get_active_instance_count() == 1);

      jewels::memory::PmrDeleter<Base, true> base_deleter(resource);
      jewels::memory::pmr_unique_ptr<Base, true> base_ptr(nullptr, base_deleter);
      REQUIRE_FALSE(base_ptr);

      base_ptr = std::move(derived_ptr);
      REQUIRE_FALSE(derived_ptr);
      REQUIRE(base_ptr);
      CHECK(Derived::get_active_instance_count() == 1);

      base_ptr = {};
      CHECK(Derived::get_active_instance_count() == 0);
    }

    SECTION("Invariant violation via reset")
    {
      auto derived_ptr = make_pmr_unique<Derived, true>(resource);
      REQUIRE(derived_ptr);
      CHECK(Derived::get_active_instance_count() == 1);

      auto base_ptr = make_pmr_unique<Base, true>(resource);

      // Intentionally violate the invariant of base_ptr instead of using move assignment
      auto* raw_ptr = derived_ptr.release();
      base_ptr.reset(raw_ptr);

      REQUIRE_THROWS_AS(base_ptr.get_deleter()(base_ptr.release()), jewels::memory::BadPolymorphicDeletion);

      // Clean up the leaked object.
      REQUIRE_NOTHROW(derived_ptr.get_deleter()(raw_ptr));
    }
  }

  memory.get_memory_resource_metrics(jewels::Out{metrics});
  CHECK(metrics.current_allocated == 0);
}

} // namespace jewels::memory::testing
