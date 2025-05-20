// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/file_chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/s3_chunk_reader_writer_factory.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging::offboard
{

/// Protobuf file read mode
WISE_ENUM_CLASS(
  (ProtobufReadMode, uint8_t),
  // Don't fail if the protobuf file is empty
  dont_fail_if_empty,
  // Fail if the protobuf file is empty
  fail_if_empty)

/// Factory to create chunk readers and writers
///
/// This class also implements some backing store independent implementations of utility
/// functions needed to read and write logs.
class ChunkReaderWriterFactory
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit ChunkReaderWriterFactory(jewels::memory::MemoryResource memory_resource);

  ~ChunkReaderWriterFactory() = default;

  ChunkReaderWriterFactory(const ChunkReaderWriterFactory& other) = delete;
  ChunkReaderWriterFactory& operator=(const ChunkReaderWriterFactory& other) = delete;
  ChunkReaderWriterFactory(ChunkReaderWriterFactory&&) noexcept = default;
  ChunkReaderWriterFactory& operator=(ChunkReaderWriterFactory&&) noexcept = default;

  /// Create a shared pointer to a chunk reader
  /// @param[in] uri_str Log URI
  /// Pointer to the chunk reader or LogError on failure
  [[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkReader>> make_chunk_reader(std::string_view uri_str);

  /// Create a shared pointer to a chunk writer
  /// @param[in] uri_str Log URI
  /// Pointer to the chunk reader or LogError on failure
  [[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkWriter>> make_chunk_writer(std::string_view uri_str);

  /// Test whether a log URI exists
  /// @param[in] uri_str Log URI
  /// @return True iff an object exists at the log URI or LogError on failure
  [[nodiscard]] LogExpected<bool> exists(std::string_view uri_str);

  /// Create the directories for a log URI
  /// @param[in] uri_str Log URI
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> create_directories(std::string_view uri_str);

  /// Get the log files found under a log URI
  /// @param[in] uri_str Log URI
  /// @return Vector of log file paths or LogError on failure
  [[nodiscard]] LogExpected<std::pmr::vector<std::pmr::string>> list_log_files(std::string_view uri_str);

  /// Write a file to a log
  /// @param[in] uri_str Log file URI
  /// @param[in] data Data to write
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> write_log_file(std::string_view uri_str, std::span<const std::byte> data);

  /// Read a file from a log
  /// @param[in] uri_str Log file URI
  /// @return File data or LogError on failure
  [[nodiscard]] LogExpected<std::pmr::vector<std::byte>> read_log_file(std::string_view uri_str);

  /// Write a string serialized protobuf to a log
  /// @tparam ProtobufType Protobuf type
  /// @param[in] uri_str Text proto URI
  /// @param[in] header Protobuf file header
  /// @param[in] protobuf Protobuf to write
  /// @return LogError on failure
  template <typename ProtobufType>
  [[nodiscard]] LogExpected<void>
  write_text_proto(std::string_view uri_str, std::string_view header, const ProtobufType& protobuf);

  /// Read a string serialized protobuf from a log
  /// @tparam ProtobufType Protobuf type
  /// @param[in] uri_str Text proto URI
  /// @param[in] read_mode Protobuf file read mode
  /// @return ProtLogError on failure
  template <typename ProtobufType>
  [[nodiscard]] LogExpected<ProtobufType>
  read_text_proto(std::string_view uri_str, ProtobufReadMode read_mode = ProtobufReadMode::dont_fail_if_empty);

  /// Write a binary serialized protobuf to a log
  /// @tparam ProtobufType Protobuf type
  /// @param[in] uri_str Binary proto URI
  /// @param[in] protobuf Protobuf to write
  /// @return LogError on failure
  template <typename ProtobufType>
  [[nodiscard]] LogExpected<void> write_binary_proto(std::string_view uri_str, const ProtobufType& protobuf);

  /// Read a binary serialized protobuf from a log
  /// @tparam ProtobufType Protobuf type
  /// @param[in] uri_str Binary proto URI
  /// @param[in] read_mode Protobuf file read mode
  /// @return ProtLogError on failure
  template <typename ProtobufType>
  [[nodiscard]] LogExpected<ProtobufType>
  read_binary_proto(std::string_view uri_str, ProtobufReadMode read_mode = ProtobufReadMode::dont_fail_if_empty);

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// File chunk reader/writer factory
  FileChunkReaderWriterFactory file_factory_;

  /// S3 chunk reader/writer factory, initialized when required
  std::optional<S3ChunkReaderWriterFactory> maybe_s3_factory_;
};

} // namespace clockwork_logging::offboard

#include "clockwork/logging/offboard/chunk_reader_writer_factory.inl"
