// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/shared_buffer_pool.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <memory_resource>
#include <utility>

namespace jewels::testing
{
namespace
{

/// Size of the test pool
constexpr size_t test_capacity = 2U;

/// Test buffer size
constexpr size_t buffer_size = 4096U;

/// Test buffer alignment
constexpr size_t alignment = 4096U;

using SharedBufferPoolType = SharedBufferPool<buffer_size, alignment>;
using ThreadSafeSharedBufferPoolType = ThreadSafeSharedBufferPool<buffer_size, alignment>;

TEMPLATE_TEST_CASE("Pool of shared buffers", "", SharedBufferPoolType, ThreadSafeSharedBufferPoolType)
{
  const memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  TestType test_pool(memory_resource, test_capacity);
  REQUIRE(test_pool.get_capacity() == test_capacity);
  REQUIRE(test_pool.get_avail_count() == test_capacity);
  REQUIRE_FALSE(test_pool.is_empty());

  SECTION("Smoke test")
  {
    auto ref1_result = test_pool.get_shared_buffer();
    REQUIRE(ref1_result);
    auto& ref1 = ref1_result.value();
    REQUIRE(ref1.get_reference_count() == 1U);
    REQUIRE(test_pool.get_avail_count() == 1U);
    REQUIRE_FALSE(test_pool.is_empty());
    REQUIRE(ref1.is_valid());
    REQUIRE(static_cast<bool>(ref1));
    REQUIRE(ref1.value().size() == buffer_size);
    REQUIRE(memory::to_uintptr_t(ref1.value().data()) % alignment == 0U);
  }

  SECTION("Exhaust pool and reset references")
  {
    auto ref1_result = test_pool.get_shared_buffer();
    REQUIRE(ref1_result);
    auto& ref1 = ref1_result.value();

    auto ref2_result = test_pool.get_shared_buffer();
    REQUIRE(ref2_result);
    auto& ref2 = ref2_result.value();
    REQUIRE(ref2.get_reference_count() == 1U);
    REQUIRE(test_pool.get_avail_count() == 0U);
    REQUIRE(test_pool.is_empty());
    REQUIRE(ref2.is_valid());
    REQUIRE(static_cast<bool>(ref2));
    REQUIRE(ref2.value().size() == buffer_size);
    REQUIRE(memory::to_uintptr_t(ref2.value().data()) % alignment == 0U);

    REQUIRE_FALSE(test_pool.get_shared_buffer());

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
      const SharedBufferPool<buffer_size, alignment>::SharedReference ref2;
      REQUIRE_FALSE(ref2.is_valid());
      REQUIRE_FALSE(static_cast<bool>(ref2));
    }

    SECTION("copy constructor")
    {
      auto ref1_result = test_pool.get_shared_buffer();
      REQUIRE(ref1_result);
      auto& ref1 = ref1_result.value();
      {
        // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) We make a copy on purpose to up the ref count
        typename TestType::SharedReference ref2(ref1);
        REQUIRE(ref2.get_reference_count() == 2U);
        REQUIRE(test_pool.get_avail_count() == test_capacity - 1U);
        REQUIRE(ref2.is_valid());
        REQUIRE(static_cast<bool>(ref2));
        REQUIRE(ref2.value() == ref1.value());

        REQUIRE(ref1.get_reference_count() == 2U);
      }
      REQUIRE(ref1.get_reference_count() == 1U);
    }

    SECTION("copy assignement")
    {
      auto ref1_result = test_pool.get_shared_buffer();
      REQUIRE(ref1_result);
      auto& ref1 = ref1_result.value();

      const auto ref2 = ref1;
      REQUIRE(ref2.get_reference_count() == 2U);
      REQUIRE(test_pool.get_avail_count() == test_capacity - 1U);
      REQUIRE(ref2.is_valid());
      REQUIRE(static_cast<bool>(ref2));
      REQUIRE(ref2.value() == ref1.value());

      REQUIRE(ref1.is_valid());
      REQUIRE(ref1.get_reference_count() == 2U);
    }

    SECTION("move constructor")
    {
      auto ref1_result = test_pool.get_shared_buffer();
      REQUIRE(ref1_result);
      auto& ref1 = ref1_result.value();

      typename TestType::SharedReference ref2(std::move(ref1));
      REQUIRE(ref2.get_reference_count() == 1U);
      REQUIRE(test_pool.get_avail_count() == test_capacity - 1U);
      REQUIRE(ref2.is_valid());
      REQUIRE(static_cast<bool>(ref2));
      REQUIRE(ref2.value().size() == buffer_size);
      REQUIRE(memory::to_uintptr_t(ref2.value().data()) % alignment == 0U);
    }

    SECTION("move assignment")
    {
      auto ref1_result = test_pool.get_shared_buffer();
      REQUIRE(ref1_result);
      auto& ref1 = ref1_result.value();

      const auto ref2 = std::move(ref1);
      REQUIRE(ref2.get_reference_count() == 1U);
      REQUIRE(test_pool.get_avail_count() == test_capacity - 1U);
      REQUIRE(ref2.is_valid());
      REQUIRE(static_cast<bool>(ref2));
      REQUIRE(ref2.value().size() == buffer_size);
      REQUIRE(memory::to_uintptr_t(ref2.value().data()) % alignment == 0U);
    }

    SECTION("reset")
    {
      auto ref1_result = test_pool.get_shared_buffer();
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
      auto ref1_result = test_pool.get_shared_buffer();
      REQUIRE(ref1_result);
      auto& ref1 = ref1_result.value();

      auto* entry_ptr = ref1.release();
      REQUIRE(test_pool.get_avail_count() == test_capacity - 1U);
      REQUIRE_FALSE(ref1.is_valid());
      REQUIRE_FALSE(static_cast<bool>(ref1));

      const typename TestType::SharedReference ref2{entry_ptr};
      REQUIRE(ref2);
      REQUIRE(ref2.get_reference_count() == 1U);
      REQUIRE(test_pool.get_avail_count() == test_capacity - 1U);
      REQUIRE(ref2.is_valid());
      REQUIRE(static_cast<bool>(ref2));
    }
  }
}

} // namespace
} // namespace jewels::testing
