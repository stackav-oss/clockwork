// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace jewels::hash
{

static constexpr size_t md5_byte_length = 16U;

/// Compute MD5 of a string_view
inline std::array<std::byte, md5_byte_length> md5(std::string_view data) noexcept;

/// Compute MD5 of a byte span
std::array<std::byte, md5_byte_length> md5(std::span<const std::byte> data) noexcept;

struct Md5Hash
{
  inline std::size_t operator()(const std::array<std::byte, md5_byte_length>& md5data) const noexcept;
};

} // namespace jewels::hash

#include "jewels/hash/md5.inl"
