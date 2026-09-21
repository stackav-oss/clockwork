// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

namespace jewels::tap
{

/// Fixed-size bitset with a stable byte-oriented Tachyon representation.
/// @tparam size_p Number of logical bits in the bitset.
template <size_t size_p>
class Bitset
{
public:
  /// Number of logical bits in this bitset.
  static constexpr size_t bit_size{size_p};
  /// Number of bits in a storage byte.
  static constexpr size_t bits_per_byte{8U};
  static_assert(std::numeric_limits<unsigned char>::digits == bits_per_byte);
  /// Number of bytes in the Tachyon representation.
  static constexpr size_t byte_size{(bit_size / bits_per_byte) + ((bit_size % bits_per_byte) == 0U ? 0U : 1U)};

  static_assert(bit_size > 0U);

  constexpr Bitset() noexcept = default;
  constexpr Bitset(const Bitset&) noexcept = default;
  constexpr Bitset(Bitset&&) noexcept = default;
  constexpr ~Bitset() noexcept = default;

  constexpr Bitset& operator=(const Bitset&) noexcept = default;
  constexpr Bitset& operator=(Bitset&&) noexcept = default;

  /// @return The number of logical bits in the bitset.
  [[nodiscard]] static constexpr size_t size() noexcept;

  /// Test a bit through the callsig-safe API.
  /// @param value_out Receives the bit value on success.
  /// @param index Index of the bit to test.
  /// @return success when index is valid, failure otherwise.
  jewels::BinaryOutcome test(jewels::Out<bool> value_out, size_t index) const noexcept;

  /// Test a bit through the STL-style API.
  /// This is only for STL algorithm compatibility; prefer the BinaryOutcome overload.
  /// @param index Index of the bit to test.
  /// @return The bit value.
  /// @throws std::out_of_range when index is invalid.
  [[nodiscard]] constexpr bool test(size_t index) const;

  /// Set a bit without throwing.
  /// @param index Index of the bit to set.
  /// @return Success when index is valid, failure otherwise.
  constexpr jewels::BinaryOutcome try_set(size_t index) noexcept;

  /// Set or clear a bit without throwing.
  /// @param index Index of the bit to update.
  /// @param value New bit value.
  /// @return Success when index is valid, failure otherwise.
  constexpr jewels::BinaryOutcome try_set(size_t index, bool value) noexcept;

  /// Set a bit.
  /// This is only for STL algorithm compatibility; prefer try_set.
  /// @param index Index of the bit to set.
  /// @throws std::out_of_range when index is invalid.
  constexpr Bitset& set(size_t index);

  /// Set or clear a bit.
  /// This is only for STL algorithm compatibility; prefer try_set.
  /// @param index Index of the bit to update.
  /// @param value New bit value.
  /// @throws std::out_of_range when index is invalid.
  constexpr Bitset& set(size_t index, bool value);

  /// Clear a bit without throwing.
  /// @param index Index of the bit to clear.
  /// @return Success when index is valid, failure otherwise.
  constexpr jewels::BinaryOutcome try_reset(size_t index) noexcept;

  /// Clear a bit.
  /// This is only for STL algorithm compatibility; prefer try_reset.
  /// @param index Index of the bit to clear.
  /// @throws std::out_of_range when index is invalid.
  constexpr Bitset& reset(size_t index);

  /// Clear every bit.
  constexpr Bitset& reset() noexcept;

  /// Flip a bit without throwing.
  /// @param index Index of the bit to flip.
  /// @return Success when index is valid, failure otherwise.
  constexpr jewels::BinaryOutcome try_flip(size_t index) noexcept;

  /// Flip a bit.
  /// This is only for STL algorithm compatibility; prefer try_flip.
  /// @param index Index of the bit to flip.
  /// @throws std::out_of_range when index is invalid.
  constexpr Bitset& flip(size_t index);

  /// Flip every bit.
  constexpr Bitset& flip() noexcept;

  /// @return True when every logical bit is set.
  [[nodiscard]] constexpr bool all() const noexcept;

  /// @return True when at least one logical bit is set.
  [[nodiscard]] constexpr bool any() const noexcept;

  /// @return True when no logical bits are set.
  [[nodiscard]] constexpr bool none() const noexcept;

  /// @return Number of set logical bits.
  [[nodiscard]] constexpr size_t count() const noexcept;

  /// @return A read-only view of the Tachyon byte representation.
  [[nodiscard]] constexpr std::span<const std::byte, byte_size> bytes() const noexcept;

  /// Compare two bitsets by their logical bits.
  [[nodiscard]] constexpr bool operator==(const Bitset& other) const noexcept;

private:
  /// Return true when an index is valid.
  [[nodiscard]] static constexpr bool is_valid_index(size_t index) noexcept;

  /// Return the byte containing a bit.
  [[nodiscard]] static constexpr size_t byte_index(size_t index) noexcept;

  /// Return the mask for a bit within its byte.
  [[nodiscard]] static constexpr unsigned char bit_mask(size_t index) noexcept;

  /// Mask with every bit in a byte set.
  static constexpr unsigned char full_byte_mask{0xFFU};

  /// Return the mask of valid bits in the final byte.
  [[nodiscard]] static constexpr unsigned char last_byte_mask() noexcept;

  /// Return a storage byte as an unsigned integer.
  [[nodiscard]] constexpr unsigned char byte_at(size_t index) const noexcept;

  /// Clear unused high bits in the final byte.
  constexpr void clear_unused_bits() noexcept;

  /// Byte-oriented Tachyon representation.
  std::array<std::byte, byte_size> storage_{};
};

} // namespace jewels::tap

#include "jewels/container/tap/bitset.inl"
