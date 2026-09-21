// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/error.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <type_traits>
#include <utility>

namespace jewels::memory
{
TEST_CASE("make_non_null_from_ref")
{
  int32_t an_int = 0;
  auto an_int_object_ptr = make_non_null_from_ref(an_int);
  STATIC_CHECK(std::is_same_v<decltype(an_int_object_ptr), ObjectPtr<int32_t>>);
  CHECK(an_int_object_ptr.get() == &an_int);

  const int32_t a_const_int = 1;
  auto a_const_int_object_ptr = make_non_null_from_ref(a_const_int);
  STATIC_CHECK(std::is_same_v<decltype(a_const_int_object_ptr), ObjectPtr<const int32_t>>);
  CHECK(a_const_int_object_ptr.get() == &a_const_int);
}

TEST_CASE("try_make_non_null")
{
  SECTION("returns a pointer to the object")
  {
    int32_t integer = 0;
    auto maybe_result_ptr = try_make_non_null(&integer);
    REQUIRE(maybe_result_ptr.has_value());
    CHECK(*maybe_result_ptr == &integer);
  }

  SECTION("returns an error if the pointer is null")
  {
    int32_t* i_ptr = nullptr;
    auto maybe_result_ptr = try_make_non_null(i_ptr);
    REQUIRE(!maybe_result_ptr.has_value());
    CHECK(maybe_result_ptr.error() == MemoryError::null_pointer_error);
  }

  SECTION("managed pointers - returns a pointer to the object")
  {
    auto i_ptr = std::make_unique<int32_t>(123);
    // clang-analyzer has a false positive when try_make_non_null takes ownership of the unique_ptr.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    auto maybe_result_ptr = try_make_non_null(std::move(i_ptr));
    REQUIRE(maybe_result_ptr.has_value());
    CHECK(*maybe_result_ptr.value() == 123);
  }

  SECTION("managed pointers - returns an error if the pointer is null")
  {
    // clang-analyzer has a false positive when try_make_non_null takes ownership of the unique_ptr.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    auto maybe_result_ptr = try_make_non_null(std::unique_ptr<int32_t>{});
    REQUIRE(!maybe_result_ptr.has_value());
    CHECK(maybe_result_ptr.error() == MemoryError::null_pointer_error);
  }
}

TEST_CASE("make_unique")
{
  const auto i_ptr = make_unique<int32_t>(123);
  REQUIRE(*i_ptr == 123);
}

TEST_CASE("make_shared")
{
  const auto i_ptr = make_shared<int32_t>(123);
  REQUIRE(*i_ptr == 123);
}

TEST_CASE("allocate_shared")
{
  const auto memory_resource_ptr = make_non_null_from_ref(*std::pmr::new_delete_resource());
  const auto i_ptr = allocate_shared<int32_t, std::pmr::polymorphic_allocator<int32_t>>(memory_resource_ptr.get(), 123);
  REQUIRE(*i_ptr == 123);
}

TEST_CASE("to_uintptr_t")
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Have to cast in order to test.
  REQUIRE(to_uintptr_t(reinterpret_cast<void*>(0x123UL)) == 0x123UL);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Have to cast in order to test.
  REQUIRE(to_uintptr_t(reinterpret_cast<std::byte*>(0x1234UL)) == 0x1234UL);
}

} // namespace jewels::memory
