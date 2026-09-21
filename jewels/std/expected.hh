// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <tl/expected.hpp> // IWYU pragma: export

#include <cstddef>
#include <functional>

namespace jewels
{

/// MonoError defines an error type for use with jewels::expected when
/// a function or method has a binary pass/fail return state. e.g.
///   `jewels::expected<MyReturnType, MonoError> some_func();`
///
/// The implementation is a copy of std::monostate simply replacing the struct name
/// with tweaks to pass clang-tidy errors

struct MonoError
{
};

constexpr bool operator==(MonoError /*lhs*/, MonoError /*rhs*/) noexcept
{
  return true;
}
constexpr bool operator!=(MonoError /*lhs*/, MonoError /*rhs*/) noexcept
{
  return false;
}
constexpr bool operator<(MonoError /*lhs*/, MonoError /*rhs*/) noexcept
{
  return false;
}
constexpr bool operator>(MonoError /*lhs*/, MonoError /*rhs*/) noexcept
{
  return false;
}
constexpr bool operator<=(MonoError /*lhs*/, MonoError /*rhs*/) noexcept
{
  return true;
}
constexpr bool operator>=(MonoError /*lhs*/, MonoError /*rhs*/) noexcept
{
  return true;
}
} // namespace jewels

namespace std
{
template <>
struct hash<::jewels::MonoError>
{
  size_t operator()(const ::jewels::MonoError& /*val*/) const noexcept
  {
    // 8888 is a randomly chosen value as all MonoError should hash to the same result
    constexpr size_t magic_mono_error_hash = 8888;
    return magic_mono_error_hash;
  }
};

} // namespace std
