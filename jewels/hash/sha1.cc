// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/hash/sha1.hh"

#include <sha1.hpp>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <sys/types.h>

namespace jewels::hash
{

Sha1::Sha1()
  : blocks_(0), pending_(0), buffer_(), digest_(init_digest())
{
}

Sha1::Digest Sha1::init_digest()
{
  Digest digest;
  vog_sha1::init_digest(digest);
  return digest;
}

void Sha1::update(std::span<const std::byte> data)
{
  if (pending_ > 0)
  {
    const uint64_t copy = std::min(block_bytes - pending_, data.size());
    std::memcpy(std::next(buffer_.data(), static_cast<ssize_t>(pending_)), data.data(), copy);
    data = data.subspan(copy);
    pending_ += copy;
    if (pending_ == block_bytes)
    {
      process(buffer_);
    }
  }
  // At this point, pending_==0 or data.size()==0
  while (data.size() >= block_bytes)
  {
    process(data.subspan<0, block_bytes>());
    data = data.subspan(block_bytes);
  }
  if (!data.empty())
  {
    std::memcpy(buffer_.data(), data.data(), data.size());
    pending_ = data.size();
  }
}

void Sha1::process(std::span<const std::byte, block_bytes> block)
{
  std::array<uint32_t, vog_sha1::BLOCK_INTS> block32{};
  vog_sha1::buffer_to_block(block, block32);
  vog_sha1::transform(digest_, block32, blocks_);
  pending_ = 0;
}

Sha1::Digest Sha1::get() const
{
  constexpr auto append_1bit_byte = std::byte{0x80U};
  constexpr uint32_t append_1bit_word = 0x80000000U;
  constexpr uint8_t shift_upper_64b_to_lower_32b_number = 32;
  Digest digest = digest_;
  std::array<uint32_t, vog_sha1::BLOCK_INTS> block32{};
  uint64_t blocks = blocks_;
  const uint64_t size = 8 * (block_bytes * blocks + pending_);
  if (pending_ > 0)
  {
    std::memset(std::next(buffer_.data(), static_cast<ssize_t>(pending_)), 0, block_bytes - pending_);
    buffer_.at(pending_) = append_1bit_byte;
    vog_sha1::buffer_to_block(buffer_, block32);
  }
  else
  {
    block32[0] = append_1bit_word;
  }
  if (pending_ + 1 > block_bytes - sizeof(uint64_t))
  {
    vog_sha1::transform(digest, block32, blocks);
    std::memset(block32.data(), 0, block_bytes - sizeof(uint64_t));
  }
  block32[vog_sha1::BLOCK_INTS - 1] = static_cast<uint32_t>(size);
  block32[vog_sha1::BLOCK_INTS - 2] = static_cast<uint32_t>(size >> shift_upper_64b_to_lower_32b_number);
  vog_sha1::transform(digest, block32, blocks);
  return digest;
}

} // namespace jewels::hash
