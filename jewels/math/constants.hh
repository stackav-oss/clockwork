// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <numbers>
#include <type_traits>

namespace jewels::math::constants
{

/// Mathematical constant @c tau
template <class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
inline constexpr auto tau = static_cast<T>(2.0 * std::numbers::pi_v<T>);

/// Bytes per kilobyte (10^3)
template <
  class T,
  typename = std::enable_if_t<std::is_floating_point_v<T> || (std::is_integral_v<T> && sizeof(T) >= 2U)>>
inline constexpr auto bytes_per_kb = static_cast<T>(1000);

/// Bytes per kibibyte (2^10)
template <
  class T,
  typename = std::enable_if_t<std::is_floating_point_v<T> || (std::is_integral_v<T> && sizeof(T) >= 2U)>>
inline constexpr auto bytes_per_kib = static_cast<T>(1024);

/// Bytes per megabyte (10^6)
template <
  class T,
  typename = std::enable_if_t<std::is_floating_point_v<T> || (std::is_integral_v<T> && sizeof(T) >= 4U)>>
inline constexpr auto bytes_per_mb = bytes_per_kb<T> * bytes_per_kb<T>;

/// Bytes per mebibyte (2^20)
template <
  class T,
  typename = std::enable_if_t<std::is_floating_point_v<T> || (std::is_integral_v<T> && sizeof(T) >= 4U)>>
inline constexpr auto bytes_per_mib = bytes_per_kib<T> * bytes_per_kib<T>;

/// Bytes per gigabyte (10^9)
template <
  class T,
  typename = std::enable_if_t<std::is_floating_point_v<T> || (std::is_integral_v<T> && sizeof(T) >= 4U)>>
inline constexpr auto bytes_per_gb = bytes_per_mb<T> * bytes_per_kb<T>;

/// Bytes per gibibyte (2^30)
template <
  class T,
  typename = std::enable_if_t<std::is_floating_point_v<T> || (std::is_integral_v<T> && sizeof(T) >= 4U)>>
inline constexpr auto bytes_per_gib = bytes_per_mib<T> * bytes_per_kib<T>;

} // namespace jewels::math::constants
