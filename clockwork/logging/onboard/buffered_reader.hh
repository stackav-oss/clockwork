// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/onboard/buffered_reader_base.hh"
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
class BufferedReader : public BufferedReaderBase<BufferedReader<Policy>, Policy>
{
public:
  /// Filesystem library type
  using FilesystemType = typename Policy::FilesystemType;

  /// Minimum size of reads when recovering from I/O error
  static constexpr size_t min_io_error_recover_read_size = Policy::min_io_error_recover_read_size;

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

  /// Test whether the reader is open
  /// @return True if the reader is open
  [[nodiscard]] bool is_open() const noexcept;

  /// Get the filesystem used by this instance, used for unit tests
  /// @return Filesystem reference
  [[nodiscard]] FilesystemType& get_filesystem();

  /// List the log files under a log directory
  /// @param[in] log_path Log directory path
  /// @return List of log files sorted by file name or LogError on failure
  [[nodiscard]] LogExpected<std::pmr::list<std::pmr::string>> list_log_files(std::string_view log_path);

  /// Read the trailer at the end of the log file
  /// @param[in] file_name File name
  /// @param[in] buffer_span File trailer buffer span
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> read_log_file_trailer(std::string_view file_name, std::span<std::byte> buffer_span);

  /// Grow the read window by reading a buffer from the file
  /// @return Log error on failure
  [[nodiscard]] LogExpected<void> grow_window();

private:
  /// Read a buffer zero filling any regions that return I/O errors
  /// @param[in] offset File offset
  /// @param[out] data Output data span
  /// @return Number of bytes read or LogError on failure
  [[nodiscard]] LogExpected<size_t> read_with_io_errors(size_t offset, std::span<std::byte> data);

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Filesystem library
  FilesystemType kits_fs_;

  /// Current file descriptor
  jewels::filesystem::FileDescriptor file_desc_;
};

/// BufferedReader is a disk buffered reader type
template <typename Policy>
struct IsDiskBufferedReader<BufferedReader<Policy>> : public std::true_type
{
};

/// BufferedReader is an onboard buffered reader type
template <typename Policy>
struct IsOnboardBufferedReader<BufferedReader<Policy>> : public std::true_type
{
};

} // namespace clockwork_logging::onboard

#include "clockwork/logging/onboard/buffered_reader.inl"
