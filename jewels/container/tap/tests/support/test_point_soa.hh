// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

/// Test SoA class representing what would be code-generated for a real Clockwork schema to create SoA containers.

#pragma once

#include "jewels/container/tap/soa.hh"
#include "jewels/container/tap/tests/support/test_point.hh"
#include "jewels/memory/aligned_storage.hh"
#include "jewels/meta/concepts.hh"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace jewels::tap::testing
{

// Forward declaration
template <size_t max_size>
class TestPointSoa;

/// Proxy reference to access a single TestPoint element in a TestPointSoa
template <size_t max_size>
class TestPointSoaElementRef
{
public:
  TestPointSoaElementRef(TestPointSoa<max_size>* soa, size_t index);
  TestPointSoaElementRef(const TestPointSoaElementRef&) = default;
  TestPointSoaElementRef(TestPointSoaElementRef&&) = default;
  ~TestPointSoaElementRef() = default;

  // Field accessors
  float& x();
  [[nodiscard]] const float& x() const;
  float& y();
  [[nodiscard]] const float& y() const;
  float& z();
  [[nodiscard]] const float& z() const;
  uint8_t& id();
  [[nodiscard]] const uint8_t& id() const;

  // Assignment operators
  TestPointSoaElementRef& operator=(const TestPoint& point);
  TestPointSoaElementRef& operator=(const TestPointSoaElementRef& other);
  TestPointSoaElementRef& operator=(TestPointSoaElementRef&&) = default;
  TestPointSoaElementRef& operator=(const clockwork::TapInit<TestPoint>& init);

  // Conversion to TestPoint
  explicit operator TestPoint() const;

  bool operator==(const TestPointSoaElementRef& other) const;

  // Swap for lvalue references (called by std::swap)
  friend void swap(TestPointSoaElementRef& ref_a, TestPointSoaElementRef& ref_b) noexcept
  {
    const TestPoint copy_a{ref_a.x(), ref_a.y(), ref_a.z(), ref_a.id()};
    ref_a = ref_b;
    ref_b = copy_a;
  }

  // Swap for rvalue references (called by std::iter_swap via *it)
  friend void swap(
    TestPointSoaElementRef&& ref_a,          // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
    TestPointSoaElementRef&& ref_b) noexcept // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
  {
    const TestPoint copy_a{ref_a.x(), ref_a.y(), ref_a.z(), ref_a.id()};
    ref_a = ref_b;
    ref_b = copy_a;
  }

private:
  TestPointSoa<max_size>* soa_;
  size_t index_;
};

/// Proxy const reference to access a single TestPoint element in a TestPointSoa
template <size_t max_size>
class TestPointSoaElementConstRef
{
public:
  TestPointSoaElementConstRef(const TestPointSoa<max_size>* soa, size_t index);
  explicit TestPointSoaElementConstRef(const TestPointSoaElementRef<max_size>& ref);
  TestPointSoaElementConstRef(const TestPointSoaElementConstRef&) = default;
  TestPointSoaElementConstRef(TestPointSoaElementConstRef&&) = default;
  ~TestPointSoaElementConstRef() = default;

  // Field accessors (const only)
  [[nodiscard]] const float& x() const;
  [[nodiscard]] const float& y() const;
  [[nodiscard]] const float& z() const;
  [[nodiscard]] const uint8_t& id() const;

  // Assignment operators deleted
  void operator=(const TestPoint&) = delete;
  void operator=(const TestPointSoaElementRef<max_size>&) = delete;
  void operator=(const TestPointSoaElementConstRef&) = delete;
  void operator=(TestPointSoaElementConstRef&&) = delete;
  void operator=(const clockwork::TapInit<TestPoint>&) = delete;

  // Conversion to TestPoint
  explicit operator TestPoint() const;

  bool operator==(const TestPointSoaElementConstRef& other) const;

private:
  const TestPointSoa<max_size>* soa_;
  size_t index_;
};

} // namespace jewels::tap::testing

// Specialize SoaTraits in jewels::tap namespace to break CRTP circular dependency
namespace jewels::tap
{

template <size_t max_size>
struct SoaTraits<testing::TestPointSoa<max_size>>
{
  using ElementRef = testing::TestPointSoaElementRef<max_size>;
  using ElementConstRef = testing::TestPointSoaElementConstRef<max_size>;
};

} // namespace jewels::tap

namespace jewels::tap::testing
{

/// Concrete SoA implementation for TestPoint, mimicking what would be code-generated
template <size_t max_size>
class TestPointSoa : public jewels::tap::SoaInterface<TestPointSoa<max_size>, TestPoint, max_size, true>
{
public:
  using Base = jewels::tap::SoaInterface<TestPointSoa<max_size>, TestPoint, max_size, true>;
  using ElementRef = TestPointSoaElementRef<max_size>;
  using ElementConstRef = TestPointSoaElementConstRef<max_size>;
  friend Base;
  friend TestPointSoaElementRef<max_size>;
  friend TestPointSoaElementConstRef<max_size>;

  TestPointSoa() = default;

  // Span accessors for bulk operations on each field
  std::span<float> view_x()
  {
    return std::span{jewels::memory::ObjectPolicy<float>::ptr(x_[0]), max_size};
  }
  [[nodiscard]] std::span<const float> view_x() const
  {
    return std::span{jewels::memory::ObjectPolicy<float>::ptr(x_[0]), max_size};
  }

  std::span<float> view_y()
  {
    return std::span{jewels::memory::ObjectPolicy<float>::ptr(y_[0]), max_size};
  }
  [[nodiscard]] std::span<const float> view_y() const
  {
    return std::span{jewels::memory::ObjectPolicy<float>::ptr(y_[0]), max_size};
  }

  std::span<float> view_z()
  {
    return std::span{jewels::memory::ObjectPolicy<float>::ptr(z_[0]), max_size};
  }
  [[nodiscard]] std::span<const float> view_z() const
  {
    return std::span{jewels::memory::ObjectPolicy<float>::ptr(z_[0]), max_size};
  }

  std::span<uint8_t> view_id()
  {
    return std::span{jewels::memory::ObjectPolicy<uint8_t>::ptr(id_[0]), max_size};
  }
  [[nodiscard]] std::span<const uint8_t> view_id() const
  {
    return std::span{jewels::memory::ObjectPolicy<uint8_t>::ptr(id_[0]), max_size};
  }

private:
  // Required by SoaInterface CRTP base
  [[nodiscard]] bool compare_fields(const TestPointSoa& other, size_t count) const;

  void wipe_range(size_t begin, size_t end);

  void construct_range(size_t begin, size_t end);

  // Helper to construct a single element at index
  // This is called by emplace_back and similar operations
  // Four overloads for the four ways to construct a Clockwork schema element
  void construct_element(size_t index);
  void construct_element(size_t index, const TestPoint& point);
  void construct_element(size_t index, const ElementRef& ref);
  void construct_element(size_t index, const clockwork::TapInit<TestPoint>& init);

  // SoA data layout: separate AlignedStorage arrays for each field
  // Using AlignedStorage ensures we control object lifetime explicitly
  std::array<jewels::memory::AlignedStorage<float>, max_size> x_;
  std::array<jewels::memory::AlignedStorage<float>, max_size> y_;
  std::array<jewels::memory::AlignedStorage<float>, max_size> z_;
  std::array<jewels::memory::AlignedStorage<uint8_t>, max_size> id_;

  // Size field required by VarSoa (variable-size SoA)
  jewels::tap::compact_size_t<max_size> size_{};
};

// Verify that TestPointSoa satisfies the ImplicitLifetimeType concept
template <size_t max_size>
inline constexpr bool test_point_soa_is_implicit_lifetime = jewels::meta::ImplicitLifetimeType<TestPointSoa<max_size>>;
static_assert(test_point_soa_is_implicit_lifetime<10>, "TestPointSoa must be an implicit-lifetime type");

/// Non-member equality operator for Catch2 compatibility
/// Catch2's expression decomposition requires a non-member operator== that works with non-const references
template <size_t max_size>
inline bool operator==(const TestPointSoa<max_size>& lhs, const TestPointSoa<max_size>& rhs);

} // namespace jewels::tap::testing

#include "jewels/container/tap/tests/support/test_point_soa.inl"

// Specialize std::swap for TestPointSoaElementRef to make std::swap(ref1, ref2) work
namespace std // NOLINT(cert-dcl58-cpp)
{
template <size_t max_size>
void swap( // NOLINT(readability-redundant-declaration, cert-dcl58-cpp)
  jewels::tap::testing::TestPointSoaElementRef<max_size>& ref_a,
  jewels::tap::testing::TestPointSoaElementRef<max_size>& ref_b) noexcept;
} // namespace std
