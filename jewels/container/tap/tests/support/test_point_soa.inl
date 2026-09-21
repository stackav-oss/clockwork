// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
// IWYU pragma: private, include "jewels/container/tap/tests/support/test_point_soa.hh"

#pragma once

#include "jewels/container/tap/tests/support/test_point_soa.hh"

#include "jewels/container/tap/tests/support/test_point.hh"
#include "jewels/memory/aligned_storage.hh"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace jewels::tap::testing
{

// TestPointSoaElementRef implementations
template <size_t max_size>
TestPointSoaElementRef<max_size>::TestPointSoaElementRef(TestPointSoa<max_size>* soa, size_t index)
  : soa_{soa}, index_{index}
{
}

template <size_t max_size>
float& TestPointSoaElementRef<max_size>::x()
{
  return jewels::memory::ObjectPolicy<float>::get(soa_->x_[index_]);
}

template <size_t max_size>
const float& TestPointSoaElementRef<max_size>::x() const
{
  return jewels::memory::ObjectPolicy<float>::get(soa_->x_[index_]);
}

template <size_t max_size>
float& TestPointSoaElementRef<max_size>::y()
{
  return jewels::memory::ObjectPolicy<float>::get(soa_->y_[index_]);
}

template <size_t max_size>
const float& TestPointSoaElementRef<max_size>::y() const
{
  return jewels::memory::ObjectPolicy<float>::get(soa_->y_[index_]);
}

template <size_t max_size>
float& TestPointSoaElementRef<max_size>::z()
{
  return jewels::memory::ObjectPolicy<float>::get(soa_->z_[index_]);
}

template <size_t max_size>
const float& TestPointSoaElementRef<max_size>::z() const
{
  return jewels::memory::ObjectPolicy<float>::get(soa_->z_[index_]);
}

template <size_t max_size>
uint8_t& TestPointSoaElementRef<max_size>::id()
{
  return jewels::memory::ObjectPolicy<uint8_t>::get(soa_->id_[index_]);
}

template <size_t max_size>
const uint8_t& TestPointSoaElementRef<max_size>::id() const
{
  return jewels::memory::ObjectPolicy<uint8_t>::get(soa_->id_[index_]);
}

template <size_t max_size>
TestPointSoaElementRef<max_size>& TestPointSoaElementRef<max_size>::operator=(const TestPoint& point)
{
  x() = point.x;
  y() = point.y;
  z() = point.z;
  id() = point.id;
  return *this;
}

template <size_t max_size>
TestPointSoaElementRef<max_size>& TestPointSoaElementRef<max_size>::operator=(const TestPointSoaElementRef& other)
{
  if (this != &other)
  {
    x() = other.x();
    y() = other.y();
    z() = other.z();
    id() = other.id();
  }
  return *this;
}

template <size_t max_size>
TestPointSoaElementRef<max_size>& TestPointSoaElementRef<max_size>::operator=(const clockwork::TapInit<TestPoint>& init)
{
  x() = init.x;
  y() = init.y;
  z() = init.z;
  id() = init.id;
  return *this;
}

template <size_t max_size>
TestPointSoaElementRef<max_size>::operator TestPoint() const
{
  return TestPoint{x(), y(), z(), id()};
}

template <size_t max_size>
bool TestPointSoaElementRef<max_size>::operator==(const TestPointSoaElementRef& other) const
{
  return x() == other.x() && y() == other.y() && z() == other.z() && id() == other.id();
}

// TestPointSoaElementConstRef implementations
template <size_t max_size>
TestPointSoaElementConstRef<max_size>::TestPointSoaElementConstRef(const TestPointSoa<max_size>* soa, size_t index)
  : soa_{soa}, index_{index}
{
}

template <size_t max_size>
TestPointSoaElementConstRef<max_size>::TestPointSoaElementConstRef(const TestPointSoaElementRef<max_size>& ref)
  : soa_{ref.soa_}, index_{ref.index_}
{
}

template <size_t max_size>
const float& TestPointSoaElementConstRef<max_size>::x() const
{
  return jewels::memory::ObjectPolicy<float>::get(soa_->x_[index_]);
}

template <size_t max_size>
const float& TestPointSoaElementConstRef<max_size>::y() const
{
  return jewels::memory::ObjectPolicy<float>::get(soa_->y_[index_]);
}

template <size_t max_size>
const float& TestPointSoaElementConstRef<max_size>::z() const
{
  return jewels::memory::ObjectPolicy<float>::get(soa_->z_[index_]);
}

template <size_t max_size>
const uint8_t& TestPointSoaElementConstRef<max_size>::id() const
{
  return jewels::memory::ObjectPolicy<uint8_t>::get(soa_->id_[index_]);
}

template <size_t max_size>
TestPointSoaElementConstRef<max_size>::operator TestPoint() const
{
  return TestPoint{x(), y(), z(), id()};
}

template <size_t max_size>
bool TestPointSoaElementConstRef<max_size>::operator==(const TestPointSoaElementConstRef& other) const
{
  return x() == other.x() && y() == other.y() && z() == other.z() && id() == other.id();
}

// TestPointSoa implementations
template <size_t max_size>
bool TestPointSoa<max_size>::compare_fields(const TestPointSoa& other, size_t count) const
{
  if (count == 0)
  {
    return true;
  }
  auto x_this = std::span{jewels::memory::ObjectPolicy<float>::ptr(x_[0]), count};
  auto x_other = std::span{jewels::memory::ObjectPolicy<float>::ptr(other.x_[0]), count};
  auto y_this = std::span{jewels::memory::ObjectPolicy<float>::ptr(y_[0]), count};
  auto y_other = std::span{jewels::memory::ObjectPolicy<float>::ptr(other.y_[0]), count};
  auto z_this = std::span{jewels::memory::ObjectPolicy<float>::ptr(z_[0]), count};
  auto z_other = std::span{jewels::memory::ObjectPolicy<float>::ptr(other.z_[0]), count};
  auto id_this = std::span{jewels::memory::ObjectPolicy<uint8_t>::ptr(id_[0]), count};
  auto id_other = std::span{jewels::memory::ObjectPolicy<uint8_t>::ptr(other.id_[0]), count};

  return std::equal(x_this.begin(), x_this.end(), x_other.begin()) &&
         std::equal(y_this.begin(), y_this.end(), y_other.begin()) &&
         std::equal(z_this.begin(), z_this.end(), z_other.begin()) &&
         std::equal(id_this.begin(), id_this.end(), id_other.begin());
}

template <size_t max_size>
void TestPointSoa<max_size>::wipe_range(size_t begin, size_t end)
{
  if (begin >= end)
  {
    return;
  }
  // Use byte-level zeroing like VarArray for efficiency
  auto x_bytes = std::as_writable_bytes(std::span{x_});
  auto y_bytes = std::as_writable_bytes(std::span{y_});
  auto z_bytes = std::as_writable_bytes(std::span{z_});
  auto id_bytes = std::as_writable_bytes(std::span{id_});

  std::fill(
    x_bytes.subspan(begin * sizeof(float), (end - begin) * sizeof(float)).begin(),
    x_bytes.subspan(begin * sizeof(float), (end - begin) * sizeof(float)).end(),
    std::byte{0});
  std::fill(
    y_bytes.subspan(begin * sizeof(float), (end - begin) * sizeof(float)).begin(),
    y_bytes.subspan(begin * sizeof(float), (end - begin) * sizeof(float)).end(),
    std::byte{0});
  std::fill(
    z_bytes.subspan(begin * sizeof(float), (end - begin) * sizeof(float)).begin(),
    z_bytes.subspan(begin * sizeof(float), (end - begin) * sizeof(float)).end(),
    std::byte{0});
  std::fill(
    id_bytes.subspan(begin * sizeof(uint8_t), (end - begin) * sizeof(uint8_t)).begin(),
    id_bytes.subspan(begin * sizeof(uint8_t), (end - begin) * sizeof(uint8_t)).end(),
    std::byte{0});
}

template <size_t max_size>
void TestPointSoa<max_size>::construct_range(size_t begin, size_t end)
{
  // Construct each field element in the range using ObjectPolicy
  // This properly handles both scalar and class types via placement new
  for (size_t i = begin; i < end; ++i)
  {
    jewels::memory::ObjectPolicy<float>::construct(x_[i]);
    jewels::memory::ObjectPolicy<float>::construct(y_[i]);
    jewels::memory::ObjectPolicy<float>::construct(z_[i]);
    jewels::memory::ObjectPolicy<uint8_t>::construct(id_[i]);
  }
}

// Default construction
template <size_t max_size>
void TestPointSoa<max_size>::construct_element(size_t index)
{
  jewels::memory::ObjectPolicy<float>::construct(x_[index]);
  jewels::memory::ObjectPolicy<float>::construct(y_[index]);
  jewels::memory::ObjectPolicy<float>::construct(z_[index]);
  jewels::memory::ObjectPolicy<uint8_t>::construct(id_[index]);
}

// Construct from TestPoint
template <size_t max_size>
void TestPointSoa<max_size>::construct_element(size_t index, const TestPoint& point)
{
  jewels::memory::ObjectPolicy<float>::construct(x_[index], point.x);
  jewels::memory::ObjectPolicy<float>::construct(y_[index], point.y);
  jewels::memory::ObjectPolicy<float>::construct(z_[index], point.z);
  jewels::memory::ObjectPolicy<uint8_t>::construct(id_[index], point.id);
}

// Construct from ElementRef
template <size_t max_size>
void TestPointSoa<max_size>::construct_element(size_t index, const ElementRef& ref)
{
  jewels::memory::ObjectPolicy<float>::construct(x_[index], ref.x());
  jewels::memory::ObjectPolicy<float>::construct(y_[index], ref.y());
  jewels::memory::ObjectPolicy<float>::construct(z_[index], ref.z());
  jewels::memory::ObjectPolicy<uint8_t>::construct(id_[index], ref.id());
}

// Construct from TapInit - directly access fields
template <size_t max_size>
void TestPointSoa<max_size>::construct_element(size_t index, const clockwork::TapInit<TestPoint>& init)
{
  jewels::memory::ObjectPolicy<float>::construct(x_[index], init.x);
  jewels::memory::ObjectPolicy<float>::construct(y_[index], init.y);
  jewels::memory::ObjectPolicy<float>::construct(z_[index], init.z);
  jewels::memory::ObjectPolicy<uint8_t>::construct(id_[index], init.id);
}

} // namespace jewels::tap::testing

// Non-member equality operator implementation
template <size_t max_size>
inline bool jewels::tap::testing::operator==(
  const jewels::tap::testing::TestPointSoa<max_size>& lhs, const jewels::tap::testing::TestPointSoa<max_size>& rhs)
{
  return lhs.operator==(rhs);
}

// std::swap specialization implementation
// Specializing std::swap for user-defined types is explicitly allowed by the C++ standard
namespace std // NOLINT(cert-dcl58-cpp)
{
template <size_t max_size>
void swap( // NOLINT(cert-dcl58-cpp)
  jewels::tap::testing::TestPointSoaElementRef<max_size>& ref_a,
  jewels::tap::testing::TestPointSoaElementRef<max_size>& ref_b) noexcept
{
  // Call the friend swap via ADL
  swap(ref_a, ref_b);
}
} // namespace std
