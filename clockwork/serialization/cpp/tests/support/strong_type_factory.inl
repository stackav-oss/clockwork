// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
// IWYU pragma: private, include "clockwork/serialization/cpp/tests/support/strong_type_factory.hh"
#pragma once

#include "clockwork/serialization/cpp/tests/support/strong_type_factory.hh"

#include <cstddef>
#include <cstdint>

namespace clockwork::tests
{

template <typename T>
StrongType<T>::StrongType(T value)
  : value_(value)
{
}

template <typename T>
[[nodiscard]] T StrongType<T>::get() const
{
  return value_;
}

[[nodiscard]] StrongType<int32_t> make_strong_int32(int32_t value)
{
  return StrongType<int32_t>{value};
}

[[nodiscard]] StrongType<int64_t> make_strong_int64(int64_t value)
{
  return StrongType<int64_t>{value};
}

[[nodiscard]] StrongType<float> make_strong_float32(float value)
{
  return StrongType<float>{value};
}

[[nodiscard]] StrongType<double> make_strong_float64(double value)
{
  return StrongType<double>{value};
}

[[nodiscard]] StrongType<std::byte> make_strong_byte(std::byte value)
{
  return StrongType<std::byte>{value};
}

} // namespace clockwork::tests
