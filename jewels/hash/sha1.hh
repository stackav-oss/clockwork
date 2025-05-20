// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace jewels::hash
{

class Sha1
{
public:
  static constexpr size_t block_bytes = 64;
  static constexpr size_t digest_words = 5;

  using Digest = std::array<uint32_t, digest_words>;

  ///
  /// Default construct an 'empty' hash
  ///
  Sha1();

  ///
  /// Create a Sha1 and add the provided data to it.
  /// This allows a uusage like `Sha1(data).get()`
  ///
  template <typename... Source>
  explicit Sha1(const Source&... data);

  ///
  /// Add bytes to the hash
  ///
  ///@{
  void update(std::span<const std::byte> data);
  inline void update(std::string_view data);
  template <typename T, size_t n>
  void update(const std::array<T, n>& data);
  template <typename T, size_t extent>
    requires(!std::is_same_v<T, std::byte>)
  void update(std::span<const T, extent> data);
  ///@}

  ///
  /// Returns the final digest.
  /// This must do additional transformations to finalize the hash but doesn't modify the underlying object
  ///
  [[nodiscard]] Digest get() const;

private:
  ///
  /// Provides the initial hash state using RVO to prevent needing to zero and then set it
  ///
  static Digest init_digest();

  ///
  /// Processes a complete block of data and reset the buffer state
  ///
  void process(std::span<const std::byte, block_bytes> block);

  /// Counts the number of blocks processed
  uint64_t blocks_;
  /// Tracks the number of bytes currently used in `buffer_`
  uint64_t pending_;
  /// Holds up incomplete byte sequences until enough data is available to process
  /// Market mutable so that const get() method can modify the space after pending_ and avoid copying
  mutable std::array<std::byte, block_bytes> buffer_{};
  /// Holds the current partial digest
  Digest digest_{};
};

} // namespace jewels::hash
#include "jewels/hash/sha1.inl"
