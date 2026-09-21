// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"

// TODO(OI-3141): Update MD5 code to use non-deprecated OpenSSL API
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic push
#include <openssl/md5.h>
#pragma clang diagnostic pop

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace jewels::hash
{

static constexpr size_t md5_byte_length = MD5_DIGEST_LENGTH;

/// MD5 hash value
using MD5HashValue = std::array<std::byte, MD5_DIGEST_LENGTH>;

/// MD5 hash context
using MD5HashContext = MD5_CTX;

/// Compute MD5 of a string_view
inline std::array<std::byte, md5_byte_length> md5(std::string_view data) noexcept;

/// Compute MD5 of a byte span
std::array<std::byte, md5_byte_length> md5(std::span<const std::byte> data) noexcept;

struct Md5Hash
{
  inline std::size_t operator()(const std::array<std::byte, md5_byte_length>& md5data) const noexcept;
};

/// Initialize a context to compute a hash incrementally
/// @param[out] ctx Context to initialize
/// @returns failure if initialize failed
jewels::BinaryOutcome md5_initialize(jewels::Out<MD5HashContext> ctx);

/// Update the computation with a span of bytes
/// @param[in,out] ctx Context to update
/// @param[in] data Data to hash
/// @returns failure if update failed
jewels::BinaryOutcome md5_update(MD5HashContext& ctx, std::span<const std::byte> data);

/// Finalize the hash computation and return the result
/// @param[out] hash Hash value
/// @param[in] ctx Context to finalize
/// @returns failure if finalize failed
jewels::BinaryOutcome md5_finalize(jewels::Out<MD5HashValue> hash, MD5HashContext& ctx);

} // namespace jewels::hash

#include "jewels/hash/md5.inl"
