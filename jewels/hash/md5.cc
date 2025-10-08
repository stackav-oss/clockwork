// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/hash/md5.hh"

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

jewels::BinaryOutcome md5_initialize(jewels::Out<MD5HashContext> ctx)
{
  if (MD5_Init(ctx.get()) == 0)
  {
    return jewels::failure;
  }
  return jewels::success;
}

jewels::BinaryOutcome md5_update(MD5HashContext& ctx, std::span<const std::byte> data)
{
  if (MD5_Update(&ctx, data.data(), data.size()) == 0)
  {
    return jewels::failure;
  }
  return jewels::success;
}

jewels::BinaryOutcome md5_finalize(jewels::Out<MD5HashValue> hash, MD5HashContext& ctx)
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Converting to match the MD5 interface
  if (MD5_Final(reinterpret_cast<uint8_t*>(hash->data()), &ctx) == 0)
  {
    return jewels::failure;
  }
  return jewels::success;
}

} // namespace jewels::hash
