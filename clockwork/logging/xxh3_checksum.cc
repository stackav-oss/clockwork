// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/xxh3_checksum.hh"

#include <xxh3.h>

#include <algorithm>
#include <cstddef>
#include <span>

namespace clockwork_logging
{

[[nodiscard]] uint64_t compute_xxh3_checksum(std::span<const std::span<const std::byte>> data_spans) noexcept
{
  XXH3_state_t state{};
  XXH3_INITSTATE(&state);
  XXH3_64bits_reset(&state);
  std::ranges::for_each(
    data_spans,
    [&state](const auto data)
    {
      if (!data.empty())
      {
        XXH3_64bits_update(&state, data.data(), data.size());
      }
    });
  return XXH3_64bits_digest(&state);
}

[[nodiscard]] uint64_t compute_xxh3_checksum(std::span<const std::byte> data) noexcept
{
  return XXH3_64bits(data.data(), data.size());
}

[[nodiscard]] XXH3_state_t init_xxh3_checksum() noexcept
{
  XXH3_state_t state{};
  XXH3_INITSTATE(&state);
  XXH3_64bits_reset(&state);
  return state;
}

void update_xxh3_checksum(XXH3_state_t& state, std::span<const std::byte> data) noexcept
{
  if (!data.empty())
  {
    XXH3_64bits_update(&state, data.data(), data.size());
  }
}

void update_xxh3_checksum(XXH3_state_t& state, std::span<const std::span<const std::byte>> data_spans) noexcept
{
  std::ranges::for_each(
    data_spans,
    [&state](const auto data)
    {
      if (!data.empty())
      {
        XXH3_64bits_update(&state, data.data(), data.size());
      }
    });
}

[[nodiscard]] uint64_t digest_xxh3_checksum(XXH3_state_t& state) noexcept
{
  return XXH3_64bits_digest(&state);
}

} // namespace clockwork_logging
