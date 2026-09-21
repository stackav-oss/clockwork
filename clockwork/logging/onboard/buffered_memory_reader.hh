// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/onboard/types.hh"
#include "jewels/memory/memory_resource.hh"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace clockwork_logging::onboard
{

/// Reads from a memory buffer
class BufferedMemoryReader
{
public:
  /// Number of spans of spans returned from zero copy out
  static constexpr size_t zero_copy_array_size = 3U;

  /// Array of spans of spans returned from zero_copy_out
  using ZeroCopyArray = std::array<std::span<const std::span<const std::byte>>, zero_copy_array_size>;

  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit BufferedMemoryReader(jewels::memory::MemoryResource memory_resource);

  ~BufferedMemoryReader() noexcept = default;

  BufferedMemoryReader(const BufferedMemoryReader&) = delete;
  BufferedMemoryReader& operator=(const BufferedMemoryReader&) = delete;
  BufferedMemoryReader(BufferedMemoryReader&&) noexcept = default;
  BufferedMemoryReader& operator=(BufferedMemoryReader&&) noexcept = default;

  /// Open a memory buffer for reading
  /// @param[in] buffer_span Memory buffer span
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> open(std::span<const std::byte> buffer_span);

  /// Close the current read buffer if it is open
  void close();

  /// Test whether the reader is open and has not reached the end of the log
  /// @return True if the reader is open and not at end of log
  [[nodiscard]] explicit operator bool() const noexcept;

  /// Get the number of bytes remaining in the read buffer
  /// @return Bytes remaining or zero if the reader is not open
  [[nodiscard]] size_t get_bytes_remaining() const noexcept;

  /// Get the current buffer offset
  /// @return Current buffer offset or zero if the reader is not open
  [[nodiscard]] size_t get_current_offset() const noexcept;

  /// Copy data out of the read buffer
  /// @param[in] offset Offset from the current read offset to the first data byte
  /// @param[out] dest Destination data span
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> copy_out(size_t offset, std::span<std::byte> dest);

  /// Zero copy data out of the read buffer
  ///
  /// The returned spans will remain valid until the next call to advance, skip_pad_bytesm,
  /// copy_out or zero_copy_out.  The buffers backing the returned spans will remain valid
  /// until the next call to advance or skip_pad_bytes.
  ///
  /// @param[in] offset Offset from the current read offset to the first data byte
  /// @param[in] length Number of bytes to get from the read buffer
  /// @return Span of spans containing the requested data or LogError on failure
  [[nodiscard]] LogExpected<std::span<const std::span<const std::byte>>> zero_copy_out(size_t offset, size_t length);

  /// Zero copy the message header, message data, and record checksum out of the read buffer
  ///
  /// This method is intended to be used to copy out the message header, message data, and message checksum in a
  /// single call.
  ///
  /// The returned spans will remain valid until the next call to advance, skip_pad_bytesm,
  /// copy_out or zero_copy_out.  The buffers backing the returned spans will remain valid
  /// until the next call to advance or skip_pad_bytes.
  ///
  /// @param[in] offset Offset from the current read offset to the first data byte
  /// @param[in] header_length Message header size
  /// @param[in] data_length Message data size
  /// @param[in] checksum_length Record checksum size
  /// @return Array of Spans of spans containing the requested data or LogError on failure
  [[nodiscard]] LogExpected<ZeroCopyArray>
  zero_copy_out(size_t offset, size_t header_length, size_t data_length, size_t checksum_length);

  /// Advance to the next non-zero byte in the buffer
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> skip_pad_bytes();

  /// Advance the window by the specfied number of bytes
  /// @param[in] count Number of bytes to advance
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> advance(size_t count);

  /// Advance the read offset to the start of the first occurrence of the specified pattern
  ///
  /// The caller is reponsible for ensuring that the pattern does not contain any duplicate characters
  ///
  /// @param[in] pattern Pattern to search for in the buffer
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> advance(std::span<const std::byte> pattern);

private:
  /// Structure used to return results for trying to find a pattern in a span
  struct PatternSearchState
  {
    /// Offset in the span to the offset of the match
    size_t match_offset{0U};

    /// Number of characters in the pattern that were matched. This will be
    /// the pattern size if a match was found. If the span ended on a partial
    /// match this this is the number of characters that were matched before
    /// the end of the span.
    size_t match_count{0U};
  };

  /// Advance the read offset to the start of the first occurrence of the specified pattern in the read buffer
  /// @param[in] pattern Pattern to search for in the buffer
  /// @return True if the pattern is found in the first buffer or LogError on failure
  [[nodiscard]] LogExpected<bool> advance_in_first_buffer(std::span<const std::byte> pattern);

  /// Find the first match of the pattern in a span
  /// @param[in] pattern Pattern to match
  /// @param[in] data Span to search
  /// @return PatternSearchState with the results of the search
  [[nodiscard]] static PatternSearchState
  search_for_pattern_in_span(std::span<const std::byte> pattern, std::span<const std::byte> data);

  /// Clear the previous contents of the zero copy array spans
  void clear_zero_copy_array_spans();

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Error that caused the reader to fail, if any
  std::optional<LogError> maybe_reader_error_;

  /// Memory buffer span
  std::span<const std::byte> buffer_span_{};

  /// Current buffer offset
  size_t current_buffer_offset_{};

  /// Storage for the spans returned from zero copy reads
  std::array<std::pmr::vector<std::span<const std::byte>>, zero_copy_array_size> zero_copy_array_spans_;
};

/// BufferedMemoryReader is a memory buffered reader type
template <>
struct IsMemoryBufferedReader<BufferedMemoryReader> : public std::true_type
{
};

} // namespace clockwork_logging::onboard
