// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/memory_resource.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <type_traits>

namespace jewels::memory
{
namespace
{
class WrapperResource : public std::pmr::memory_resource
{
public:
  explicit WrapperResource()
    : base_(std::pmr::get_default_resource())
  {
  }

private:
  std::pmr::memory_resource* base_;

  [[nodiscard]] void* do_allocate(std::size_t bytes, std::size_t alignment) override
  {
    return base_->allocate(bytes, alignment);
  }
  void do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment) override
  {
    base_->deallocate(ptr, bytes, alignment);
  }
  [[nodiscard]] bool do_is_equal(const memory_resource& other) const noexcept override
  {
    return other.is_equal(*base_);
  }
};
} // namespace

TEST_CASE("Constructors")
{
  // MemoryResource should not be default constructible
  STATIC_CHECK(!std::is_default_constructible_v<MemoryResource>);
  // MemoryResource should be constructible from a memory resource pointer
  STATIC_CHECK(std::is_nothrow_constructible_v<MemoryResource, std::pmr::memory_resource*>);

  // MemoryResource should be moveable and copyable
  STATIC_CHECK(std::is_nothrow_copy_constructible_v<MemoryResource>);
  STATIC_CHECK(std::is_nothrow_move_constructible_v<MemoryResource>);

  // Allow construction from objects (lvalues), e.g. a resource on the stack
  STATIC_CHECK(std::is_nothrow_constructible_v<MemoryResource, WrapperResource&>);
  STATIC_CHECK(!std::is_nothrow_constructible_v<MemoryResource, const WrapperResource&>);
}

TEST_CASE("MemoryResource conversions")
{
  // MemoryResource should be implicitly convertible to a polymorphic allocator
  STATIC_CHECK(std::is_nothrow_convertible_v<MemoryResource, std::pmr::polymorphic_allocator<int32_t>>);
  // const MemoryResource should be implicitly convertible to a const polymorphic allocator
  STATIC_CHECK(std::is_nothrow_convertible_v<const MemoryResource, const std::pmr::polymorphic_allocator<int32_t>>);
  // MemoryResource should be convertible to a memory resource
  STATIC_CHECK(std::is_nothrow_constructible_v<std::pmr::memory_resource*, MemoryResource>);
  // const MemoryResource should be convertible to a const memory resource
  STATIC_CHECK(std::is_nothrow_constructible_v<const std::pmr::memory_resource*, const MemoryResource>);
}

TEST_CASE("MemoryResource equality")
{
  WrapperResource wrapper1;
  WrapperResource wrapper2;
  const MemoryResource resource1(wrapper1);
  const MemoryResource resource2(wrapper2);
  CHECK(resource1 == resource1);
  CHECK(resource2 == resource2);
  CHECK(wrapper1 == wrapper2);
  CHECK(resource1 == resource2);
}

} // namespace jewels::memory
