// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/pointers.hh"
#include "jewels/meta/concepts.hh"

#include <cstddef>
#include <span>
#include <type_traits>

namespace clockwork
{

/// An approximate backport of c++23's start_lifetime_as.  While
/// std::start_lifetime_as needs compiler support, this is the closest
/// alternative without c++23.
/// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p0593r6.html
/// @tparam Type The type to interpret the bytes as.  Must be an implicit lifetime type.
/// @tparam Byte Either std::byte or const std::byte.
/// @param bytes A span of bytes the same size as Type.
/// @return A ptr to Type.
template <jewels::meta::ImplicitLifetimeType Type, jewels::meta::Byte Byte>
  requires(std::is_const_v<Type> || !std::is_const_v<Byte>)
jewels::memory::ObjectPtr<Type> start_lifetime_as(std::span<Byte, sizeof(Type)> bytes) noexcept;

} // namespace clockwork

#include "clockwork/memory/start_lifetime_as.inl"
