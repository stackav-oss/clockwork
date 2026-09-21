// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/shared_object_pool.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <string>
#include <utility>

namespace jewels::testing
{
namespace
{

/// Number of times the constructor was called
size_t constructor_count = 0U; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables) Test only.

/// Number of times the destructor was called
size_t destructor_count = 0U; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables) Test only.

/// Class used to test object lifecycle
struct TestClass
{
  /// Constructor
  /// @param[in] str_value_in String value
  /// @param[in] int_value Integer value
  TestClass(std::string str_value_in, int32_t int_value_in);

  ~TestClass();

  TestClass(const TestClass&) = delete;
  TestClass& operator=(const TestClass&) = delete;
  TestClass(TestClass&&) = delete;
  TestClass& operator=(TestClass&&) = delete;

  /// String value
  std::string str_value;

  /// Integer value
  int32_t int_value;
};

TestClass::TestClass(std::string str_value_in, int32_t int_value_in)
  : str_value(std::move(str_value_in)), int_value(int_value_in)
{
  ++constructor_count;
}

TestClass::~TestClass()
{
  ++destructor_count;
}

/// Size of the test pool
constexpr size_t test_capacity = 2U;

using SharedInt32PoolType = SharedObjectPool<int32_t>;
using ThreadSafeSharedInt32PoolType = ThreadSafeSharedObjectPool<int32_t>;

TEMPLATE_TEST_CASE("Shared pool of int32_t", "", SharedInt32PoolType, ThreadSafeSharedInt32PoolType)
{
  const memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  TestType test_pool(memory_resource, test_capacity);
  REQUIRE(test_pool.get_capacity() == test_capacity);
  REQUIRE(test_pool.get_avail_count() == test_capacity);
  REQUIRE_FALSE(test_pool.is_empty());

  const int32_t value1 = 12345;

  SECTION("Smoke test")
  {
    auto ref1_result = test_pool.make_shared_object(value1);
    REQUIRE(ref1_result);
    auto& ref1 = ref1_result.value();
    REQUIRE(ref1.get_reference_count() == 1U);
    REQUIRE(test_pool.get_avail_count() == 1U);
    REQUIRE_FALSE(test_pool.is_empty());
    REQUIRE(ref1.is_valid());
    REQUIRE(static_cast<bool>(ref1));
    REQUIRE(ref1.value() == value1);
    REQUIRE(*ref1 == value1);
  }

  SECTION("Exhaust pool and reset references")
  {
    auto ref1_result = test_pool.make_shared_object(value1);
    REQUIRE(ref1_result);
    auto& ref1 = ref1_result.value();

    const int32_t value2 = 23456;
    auto ref2_result = test_pool.make_shared_object(value2);
    REQUIRE(ref2_result);
    auto& ref2 = ref2_result.value();
    REQUIRE(ref2.get_reference_count() == 1U);
    REQUIRE(test_pool.get_avail_count() == 0U);
    REQUIRE(test_pool.is_empty());
    REQUIRE(ref2.is_valid());
    REQUIRE(static_cast<bool>(ref2));
    REQUIRE(ref2.value() == value2);
    REQUIRE(*ref2 == value2);

    const int32_t value3 = 34567;
    REQUIRE_FALSE(test_pool.make_shared_object(value3));

    ref1.reset();
    REQUIRE_FALSE(ref1.is_valid());
    REQUIRE(test_pool.get_avail_count() == 1U);
    REQUIRE_FALSE(test_pool.is_empty());

    ref2.reset();
    REQUIRE_FALSE(ref2.is_valid());
    REQUIRE(test_pool.get_avail_count() == 2U);
    REQUIRE_FALSE(test_pool.is_empty());
  }

  SECTION("SharedReference")
  {
    SECTION("Default constructor")
    {
      const typename TestType::SharedReference ref2;
      REQUIRE_FALSE(ref2.is_valid());
      REQUIRE_FALSE(static_cast<bool>(ref2));
    }

    SECTION("copy constructor")
    {
      auto ref1_result = test_pool.make_shared_object(value1);
      REQUIRE(ref1_result);
      auto& ref1 = ref1_result.value();

      {
        // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) We make a copy on purpose to up the ref count
        typename TestType::SharedReference ref2(ref1);
        REQUIRE(ref2.get_reference_count() == 2U);
        REQUIRE(test_pool.get_avail_count() == test_capacity - 1U);
        REQUIRE(ref2.is_valid());
        REQUIRE(static_cast<bool>(ref2));
        REQUIRE(ref2.value() == value1);
        REQUIRE(*ref2 == value1);

        REQUIRE(ref1.get_reference_count() == 2U);
      }
      REQUIRE(ref1.get_reference_count() == 1U);
    }

    SECTION("copy assignement")
    {
      auto ref1_result = test_pool.make_shared_object(value1);
      REQUIRE(ref1_result);
      auto& ref1 = ref1_result.value();

      const auto ref2 = ref1;
      REQUIRE(ref2.get_reference_count() == 2U);
      REQUIRE(test_pool.get_avail_count() == test_capacity - 1U);
      REQUIRE(ref2.is_valid());
      REQUIRE(static_cast<bool>(ref2));
      REQUIRE(ref2.value() == value1);
      REQUIRE(*ref2 == value1);

      REQUIRE(ref1.is_valid());
      REQUIRE(ref1.get_reference_count() == 2U);
    }

    SECTION("move constructor")
    {
      auto ref1_result = test_pool.make_shared_object(value1);
      REQUIRE(ref1_result);
      auto& ref1 = ref1_result.value();

      typename TestType::SharedReference ref2(std::move(ref1));
      REQUIRE(ref2.get_reference_count() == 1U);
      REQUIRE(test_pool.get_avail_count() == test_capacity - 1U);
      REQUIRE(ref2.is_valid());
      REQUIRE(static_cast<bool>(ref2));
      REQUIRE(ref2.value() == value1);
      REQUIRE(*ref2 == value1);
    }

    SECTION("move assignment")
    {
      auto ref1_result = test_pool.make_shared_object(value1);
      REQUIRE(ref1_result);
      auto& ref1 = ref1_result.value();

      const auto ref2 = std::move(ref1);
      REQUIRE(ref2.get_reference_count() == 1U);
      REQUIRE(test_pool.get_avail_count() == test_capacity - 1U);
      REQUIRE(ref2.is_valid());
      REQUIRE(static_cast<bool>(ref2));
      REQUIRE(ref2.value() == value1);
      REQUIRE(*ref2 == value1);
    }

    SECTION("reset")
    {
      auto ref1_result = test_pool.make_shared_object(value1);
      REQUIRE(ref1_result);
      auto& ref1 = ref1_result.value();

      ref1.reset();
      REQUIRE(ref1.get_reference_count() == 0U);
      REQUIRE(test_pool.get_avail_count() == test_capacity);
      REQUIRE_FALSE(ref1.is_valid());
      REQUIRE_FALSE(static_cast<bool>(ref1));
    }

    SECTION("release")
    {
      auto ref1_result = test_pool.make_shared_object(value1);
      REQUIRE(ref1_result);
      auto& ref1 = ref1_result.value();

      auto* entry_ptr = ref1.release();
      REQUIRE(test_pool.get_avail_count() == test_capacity - 1U);
      REQUIRE_FALSE(ref1.is_valid());
      REQUIRE_FALSE(static_cast<bool>(ref1));

      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      const auto uint_entry_ptr = reinterpret_cast<uintptr_t>(entry_ptr);
      // NOLINTNEXTLINE(performance-no-int-to-ptr, cppcoreguidelines-pro-type-reinterpret-cast)
      auto* entry_ptr2 = reinterpret_cast<typename TestType::PoolEntryType*>(uint_entry_ptr);

      const typename TestType::SharedReference ref2{entry_ptr2};
      REQUIRE(ref2);
      REQUIRE(ref2.get_reference_count() == 1U);
      REQUIRE(test_pool.get_avail_count() == test_capacity - 1U);
      REQUIRE(ref2.is_valid());
      REQUIRE(static_cast<bool>(ref2));
      REQUIRE(ref2.value() == value1);
    }
  }
}

using SharedTestClassPoolType = SharedObjectPool<TestClass>;
using ThreadSafeSharedTestClassPoolType = ThreadSafeSharedObjectPool<TestClass>;

TEMPLATE_TEST_CASE("Shared object lifecycle", "", SharedTestClassPoolType, ThreadSafeSharedTestClassPoolType)
{
  const memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  TestType test_pool(memory_resource, test_capacity);

  SECTION("Smoke test")
  {
    constructor_count = 0U;
    destructor_count = 0U;
    const int32_t value1 = 1;
    auto ref1_result = test_pool.make_shared_object("Test 1", value1);
    REQUIRE(ref1_result);
    auto& ref1 = ref1_result.value();
    REQUIRE(constructor_count == 1U);
    REQUIRE(destructor_count == 0U);
    REQUIRE(ref1.is_valid());
    REQUIRE(ref1.get_reference_count() == 1U);
    REQUIRE(ref1->str_value == "Test 1");
    REQUIRE(ref1->int_value == value1);
    ref1.reset();
    REQUIRE_FALSE(ref1.is_valid());
    REQUIRE(ref1.get_reference_count() == 0U);
    REQUIRE(constructor_count == 1U);
    REQUIRE(destructor_count == 1U);
  }
}

} // namespace
} // namespace jewels::testing
