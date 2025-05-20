// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/hash/md5.hh"

// TODO(OI-3141): Update MD5 code to use non-deprecated OpenSSL API
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic push
#include <openssl/md5.h>
#pragma clang diagnostic pop

#include <cstdint>

namespace jewels::hash
{

// Make sure we're in agreement with OpenSSL on MD5 length
static_assert(MD5_DIGEST_LENGTH == md5_byte_length);

std::array<std::byte, md5_byte_length> md5(std::span<const std::byte> data) noexcept
{
  std::array<std::byte, md5_byte_length> result{};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - Required to interface with OpenSSL MD5
  MD5(reinterpret_cast<const uint8_t*>(data.data()), data.size(), reinterpret_cast<uint8_t*>(result.data()));
  return result;
}

} // namespace jewels::hash
