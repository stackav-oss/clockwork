// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <xxh3.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace clockwork_logging
{

/// Compute the record checksum from the record data
/// @param[in] data_spans record data spans
/// @return Record checksum
[[nodiscard]] uint64_t compute_xxh3_checksum(std::span<const std::span<const std::byte>> data_spans) noexcept;

/// Compute the record checksum from the record data
/// @param[in] data record data
/// @return Record checksum
[[nodiscard]] uint64_t compute_xxh3_checksum(std::span<const std::byte> data) noexcept;

/// Initialize to compute a record checksum
/// @return Initialized XXH state
[[nodiscard]] XXH3_state_t init_xxh3_checksum() noexcept;

/// Update the checksum with a span of spans
/// @param[in] state XXH3 checksum state
/// @param[in] data_spans Data spans
void update_xxh3_checksum(XXH3_state_t& state, std::span<const std::span<const std::byte>> data_spans) noexcept;

/// Update the checksum from the record data
/// @param[in] data Data
void update_xxh3_checksum(XXH3_state_t& state, std::span<const std::byte> data) noexcept;

/// Finalize a record checksum
[[nodiscard]] uint64_t digest_xxh3_checksum(XXH3_state_t& state) noexcept;

} // namespace clockwork_logging
