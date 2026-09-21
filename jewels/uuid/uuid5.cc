// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/uuid/uuid5.hh"

#include "jewels/hash/sha1.hh"

#include <cstddef>

namespace jewels
{

std::array<uint8_t, uuid_size_bytes>
uuid5_raw(const std::array<uint8_t, uuid_size_bytes>& uuid_ns, std::string_view name)
{
  auto digest = jewels::hash::Sha1(uuid_ns, name).get();

  constexpr uint32_t shift_byte_0 = 24;
  constexpr uint32_t shift_byte_1 = 16;
  constexpr uint32_t shift_byte_2 = 8;
  constexpr uint32_t shift_byte_3 = 0;
  constexpr uint32_t shift_byte_byte_mask = 0xFF;
  std::array<uint8_t, uuid_size_bytes> result{};
  for (size_t i_r = 0, i_d = 0; i_r < uuid_size_bytes; i_r += 4, i_d += 1)
  {
    result.at(i_r + 0) = (digest.at(i_d) >> shift_byte_0) & shift_byte_byte_mask;
    result.at(i_r + 1) = (digest.at(i_d) >> shift_byte_1) & shift_byte_byte_mask;
    result.at(i_r + 2) = (digest.at(i_d) >> shift_byte_2) & shift_byte_byte_mask;
    result.at(i_r + 3) = (digest.at(i_d) >> shift_byte_3) & shift_byte_byte_mask;
  }
  // variant = 2, version = 5
  constexpr uint32_t uuid_variant2_byte_position = 8;
  constexpr uint32_t uuid_variant2_mask = 0x3FU;
  constexpr uint32_t uuid_variant2_value = 2;
  constexpr uint32_t uuid_variant2_shift = 6;
  constexpr uint32_t uuid_version_byte_position = 6;
  constexpr uint32_t uuid_version_mask = 0x0FU;
  constexpr uint32_t uuid_version_value = 5;
  constexpr uint32_t uuid_version_shift = 4;
  result[uuid_variant2_byte_position] =
    (result[uuid_variant2_byte_position] & uuid_variant2_mask) | (uuid_variant2_value << uuid_variant2_shift);
  result[uuid_version_byte_position] =
    (result[uuid_version_byte_position] & uuid_version_mask) | (uuid_version_value << uuid_version_shift);
  return result;
}

} // namespace jewels
