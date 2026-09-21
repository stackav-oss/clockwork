// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

namespace clockwork::tests
{

/// Test strong type class
/// @tparam T Underlying type
template <typename T>
class StrongType
{
  friend StrongType<int32_t> make_strong_int32(int32_t);
  friend StrongType<int64_t> make_strong_int64(int64_t);
  friend StrongType<float> make_strong_float32(float);
  friend StrongType<double> make_strong_float64(double);
  friend StrongType<std::byte> make_strong_byte(std::byte);

  /// Private constructor, use factory function to create an instance
  explicit StrongType(T value);

public:
  StrongType() = default;

  [[nodiscard]] T get() const;

  [[nodiscard]] friend bool operator==(const StrongType& lhs, const StrongType<T>& other) = default;

private:
  T value_;
};

using StrongInt32 = StrongType<int32_t>;
using StrongInt64 = StrongType<int64_t>;
using StrongFloat32 = StrongType<float>;
using StrongFloat64 = StrongType<double>;
using StrongByte = StrongType<std::byte>;

/// Test strong type factory function
/// @param[in] value Strong type value
[[nodiscard]] inline StrongType<int32_t> make_strong_int32(int32_t value);

/// Test strong type factory function
/// @param[in] value Strong type value
[[nodiscard]] inline StrongType<int64_t> make_strong_int64(int64_t value);

/// Test strong type factory function
/// @param[in] value Strong type value
[[nodiscard]] inline StrongType<float> make_strong_float32(float value);

/// Test strong type factory function
/// @param[in] value Strong type value
[[nodiscard]] inline StrongType<double> make_strong_float64(double value);

/// Test strong type factory function
/// @param[in] value Strong type value
[[nodiscard]] inline StrongType<std::byte> make_strong_byte(std::byte value);

} // namespace clockwork::tests

#include "clockwork/serialization/cpp/tests/support/strong_type_factory.inl"
