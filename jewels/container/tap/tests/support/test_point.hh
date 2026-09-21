// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

// Forward declaration
namespace clockwork
{
template <typename T>
struct TapInit;
}

namespace jewels::tap::testing
{

/// Simple test schema type mimicking a Point3f
struct TestPoint
{
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  uint8_t id{0};

  bool operator==(const TestPoint& other) const = default;
};

} // namespace jewels::tap::testing

// Explicit specialization of TapInit for TestPoint
// In real code, this would be code-generated
namespace clockwork
{

template <>
struct TapInit<jewels::tap::testing::TestPoint>
{
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  uint8_t id{0};

  // Implicit conversion to TestPoint (intentional for TapInit)
  // NOLINTNEXTLINE(google-explicit-constructor) TapInit requires implicit conversion
  operator jewels::tap::testing::TestPoint() const
  {
    return jewels::tap::testing::TestPoint{.x = x, .y = y, .z = z, .id = id};
  }
};

} // namespace clockwork

#include "jewels/container/tap/tests/support/test_point.inl"
