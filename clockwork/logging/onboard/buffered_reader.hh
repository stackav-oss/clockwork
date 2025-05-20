// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/onboard/types.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/aligned_storage.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/shared_pool/shared_buffer_pool.hh"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace clockwork_logging::onboard
{

/// Buffered reader
///
/// Reads files through a window of data buffers enabling zero copy reads
///
/// @tparam Policy Buffered reader policy
template <typename Policy>
class BufferedReader
{
public:
  /// File read metrics
  struct ReadMetrics
  {
    /// Bytes read
    size_t byte_count{0U};

    /// Number of reads
    size_t read_count{0U};

    /// Total read latency
    std::chrono::nanoseconds read_latency{0};
  };

  /// Filesystem library type
  using FilesystemType = typename Policy::FilesystemType;

  /// Read buffer size
  static constexpr size_t read_buffer_size = Policy::read_buffer_size;

  /// Maximum read size
  static constexpr size_t max_read_size = Policy::max_read_size;

  /// Number of buffers in the read window
  static constexpr size_t read_window_size = std::max(max_read_size / read_buffer_size, static_cast<size_t>(1U)) + 2U;

  /// Minimum size of reads when recovering from I/O error
  static constexpr size_t min_io_error_recover_read_size = Policy::min_io_error_recover_read_size;

  /// Buffer pool type
  using BufferPoolType = typename jewels::SharedBufferPool<read_buffer_size, 1U>;

  /// Buffer reference type
  using BufferReferenceType = typename BufferPoolType::SharedReference;

  /// Number of spans of spans returned from zero copy out
  static constexpr size_t zero_copy_array_size = 3U;

  /// Array of spans of spans returned from zero_copy_out
  using ZeroCopyArray = std::array<std::span<const std::span<const std::byte>>, zero_copy_array_size>;

  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit BufferedReader(jewels::memory::MemoryResource memory_resource);

  ~BufferedReader() noexcept = default;

  BufferedReader(const BufferedReader&) = delete;
  BufferedReader& operator=(const BufferedReader&) = delete;
  BufferedReader(BufferedReader&&) noexcept = default;
  BufferedReader& operator=(BufferedReader&&) noexcept = default;

  /// Open a log file for reading
  /// @param[in] file_path Path to the file to be read
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> open(std::string_view file_path);

  /// Close the current log file if it is open
  void close();

  /// Test whether the reader is open and has not reached the end of the log
  /// @return True if the reader is open and not at end of log
  [[nodiscard]] explicit operator bool() const noexcept;

  /// Get the number of bytes remaining in the log file
  /// @return Bytes remaining or zero if the file is not open
  [[nodiscard]] size_t get_bytes_remaining() const noexcept;

  /// Get the file offset to the start of the read window
  /// @return File offset or zero if the file is not open
  [[nodiscard]] size_t get_window_offset() const noexcept;

  /// Copy data out of the read window
  /// @param[in] offset Window offset to the first data byte
  /// @param[out] dest Destination data span
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> copy_out(size_t offset, std::span<std::byte> dest);

  /// Zero copy data out of the read window
  ///
  /// The returned spans will remain valid until the next call to advance, skip_pad_bytesm,
  /// copy_out or zero_copy_out.  The buffers backing the returned spans will remain valid
  /// until the next call to advance or skip_pad_bytes.
  ///
  /// @param[in] offset Window offset to the first data byte
  /// @param[in] length Number of bytes to get from the data window
  /// @return Span of spans containing the requested data or LogError on failure
  [[nodiscard]] LogExpected<std::span<const std::span<const std::byte>>> zero_copy_out(size_t offset, size_t length);

  /// Zero copy the message header, message data, and record checksum out of the read window
  ///
  /// This method is intended to be used to copy out the message header, message data, and message checksum in a
  /// single call.
  ///
  /// The returned spans will remain valid until the next call to advance, skip_pad_bytesm,
  /// copy_out or zero_copy_out.  The buffers backing the returned spans will remain valid
  /// until the next call to advance or skip_pad_bytes.
  ///
  /// @param[in] offset Window offset to the first data byte
  /// @param[in] header_length Message header size
  /// @param[in] data_length Message data size
  /// @param[in] checksum_length Record checksum size
  /// @return Array of Spans of spans containing the requested data or LogError on failure
  [[nodiscard]] LogExpected<ZeroCopyArray>
  zero_copy_out(size_t offset, size_t header_length, size_t data_length, size_t checksum_length);

  /// Advance to the next non-zero byte in the window
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> skip_pad_bytes();

  /// Advance the window by the specfied number of bytes
  /// @param[in] count Number of bytes to advance
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> advance(size_t count);

  /// Advance the window to the start of the first occurrence of the specified pattern
  ///
  /// The caller is reponsible for ensuring that the pattern does not contain any duplicate characters
  ///
  /// @param[in] pattern Pattern to search for in the file
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> advance(std::span<const std::byte> pattern);

  /// Get the filesystem used by this instance, used for unit tests
  /// @return Filesystem reference
  [[nodiscard]] FilesystemType& get_filesystem();

  /// Get the read metrics for the log file
  [[nodiscard]] ReadMetrics get_read_metrics() const;

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

  /// Remove the first buffer from the window once it has been consumed
  void remove_first_buffer_from_window();

  /// Grow the read window by reading a buffer from the file
  /// @return Log error on failure
  [[nodiscard]] LogExpected<void> grow_window();

  /// Advance the window by the specfied number of bytes
  /// @param[in] count Number of bytes to advance
  void advance_internal(size_t count);

  /// Advance the window to the start of the first occurrence of the specified pattern in the first window buffer
  /// @param[in] pattern Pattern to search for in the file
  /// @return True if the pattern is found in the first buffer or LogError on failure
  [[nodiscard]] LogExpected<bool> advance_in_first_buffer(std::span<const std::byte> pattern);

  /// Find the first match of the pattern in a span
  /// @param[in] pattern Pattern to match
  /// @param[in] data Span to search
  /// @return PatternSearchState with the results of the search
  [[nodiscard]] PatternSearchState
  search_for_pattern_in_span(std::span<const std::byte> pattern, std::span<const std::byte> data);

  /// Advance the window to the first occurrance of a value
  /// @param[in] value Value to advance to
  [[nodiscard]] LogExpected<void> advance_internal(std::byte value);

  /// Read a buffer zero filling any regions that return I/O errors
  /// @param[in] offset File offset
  /// @param[out] data Output data span
  /// @return Number of bytes read or LogError on failure
  [[nodiscard]] LogExpected<size_t> read_with_io_errors(size_t offset, std::span<std::byte> data);

  /// Clear the previous contents of the zero copy array spans
  void clear_zero_copy_array_spans();

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Filesystem library
  FilesystemType kits_fs_;

  /// Current file descriptor
  jewels::filesystem::FileDescriptor file_desc_;

  /// Error that caused the reader to fail, if any
  std::optional<LogError> maybe_reader_error_;

  /// Current file size in bytes
  size_t file_size_{};

  /// Log file offset to the start of the first buffer in the read window
  size_t read_window_offset_{};

  /// Total window size in bytes
  size_t window_size_{};

  /// Current offset into the first buffer in the read window
  size_t first_buffer_offset_{};

  /// Read buffer pool
  BufferPoolType buffer_pool_;

  /// Storage for the queue of buffer references in the read window
  std::pmr::vector<jewels::memory::AlignedStorage<BufferReferenceType>> read_window_buffers_storage_{};

  /// Circular queue of buffer references in the read window
  jewels::container::CircularBuffer<
    jewels::memory::ObjectPolicy<BufferReferenceType>,
    std::span<jewels::memory::AlignedStorage<BufferReferenceType>, read_window_size>>
    read_window_buffers_;

  /// Storage for the queue of buffer spans in the read window
  std::pmr::vector<jewels::memory::AlignedStorage<std::span<const std::byte>>> read_window_spans_storage_{};

  /// Circular queue of buffer spans in the read window
  jewels::container::CircularBuffer<
    jewels::memory::ObjectPolicy<std::span<const std::byte>>,
    std::span<jewels::memory::AlignedStorage<std::span<const std::byte>>, read_window_size>>
    read_window_spans_;

  /// Storage for the spans returned from zero copy reads
  std::array<std::pmr::vector<std::span<const std::byte>>, zero_copy_array_size> zero_copy_array_spans_;

  /// Read metrics
  ReadMetrics read_metrics_{};
};

/// BufferedReader is a disk buffered reader type
template <typename Policy>
struct IsDiskBufferedReader<BufferedReader<Policy>> : public std::true_type
{
};

} // namespace clockwork_logging::onboard

#include "clockwork/logging/onboard/buffered_reader.inl"
