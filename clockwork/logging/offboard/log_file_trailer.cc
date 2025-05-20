// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/log_file_trailer.hh"

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

[[nodiscard]] LogExpected<void> write_log_file_trailer(
  jewels::memory::MemoryResource memory_resource,
  const ChunkLocation& metadata_chunk_location,
  const ChunkLocation& metrics_chunk_location,
  const ChunkLocation& index_chunk_location,
  ChunkWriter& chunk_writer,
  const ChunkCompressor& chunk_compressor)
{
  std::pmr::vector<std::byte> chunk(log_file_trailer_chunk_size, memory_resource);
  LogFileTrailerChunk trailer{
    .index_chunk_location = index_chunk_location,
    .metrics_chunk_location = metrics_chunk_location,
    .metadata_chunk_location = metadata_chunk_location,
  };
  std::memcpy(chunk.data(), &trailer, log_file_trailer_chunk_size);
  auto compress_result = chunk_compressor.compress_chunk(std::move(chunk), CompressionType::none);
  if (!compress_result)
  {
    jewels::log_cerr_error("Failed to compress log file trailer chunk for {}", chunk_writer.file_uri().string());
    return jewels::unexpected(compress_result.error());
  }
  if (const auto write_result = chunk_writer.write_chunk(std::move(compress_result).value()); !write_result)
  {
    jewels::log_cerr_error("Failed to write log file trailer chunk for {}", chunk_writer.file_uri().string());
    return jewels::unexpected(write_result.error());
  }
  return {};
}

[[nodiscard]] LogExpected<reader::LogFileTrailerInfo> read_log_file_trailer(
  const jewels::memory::NonNullSharedPtr<ChunkReader>& chunk_reader_ptr,
  const jewels::memory::NonNullSharedPtr<ChunkCompressor>& chunk_compressor_ptr)
{
  const auto size_result = chunk_reader_ptr->file_size();
  if (!size_result)
  {
    if (size_result.error() != LogError::s3_access_denied)
    {
      jewels::log_cerr_error("Failed to get file size for {}", chunk_reader_ptr->file_uri().string());
    }
    return jewels::unexpected(size_result.error());
  }
  const auto file_size = size_result.value();
  if (file_size < log_file_trailer_chunk_size)
  {
    jewels::log_cerr_error("Invalid file size ({}) for {}", file_size, chunk_reader_ptr->file_uri().string());
    return jewels::unexpected(LogError::invalid_file_chunk);
  }
  auto read_result = chunk_reader_ptr->read_chunk(file_size - log_file_trailer_chunk_size, log_file_trailer_chunk_size);
  if (!read_result)
  {
    jewels::log_cerr_error("Failed to read log file trailer for {}", chunk_reader_ptr->file_uri().string());
    return jewels::unexpected(read_result.error());
  }
  const auto decompress_result =
    chunk_compressor_ptr->decompress_chunk(std::move(read_result).value(), CompressionType::none);
  if (!decompress_result)
  {
    jewels::log_cerr_error("Failed to decompress log file trailer chunk for {}", chunk_reader_ptr->file_uri().string());
    return jewels::unexpected(decompress_result.error());
  }
  const auto& chunk = decompress_result.value();
  const auto trailer_result =
    nolint_helper::byte_span_to_value_ptr<LogFileTrailerChunk>(std::span{chunk.data(), log_file_trailer_chunk_size});
  if (!trailer_result)
  {
    return jewels::unexpected(trailer_result.error());
  }
  const auto* trailer_ptr = trailer_result.value();
  if (trailer_ptr->magic_number != log_file_magic_number)
  {
    jewels::log_cerr_error(
      "Invalid log file magic number: {} is not a log file", chunk_reader_ptr->file_uri().string());
    return jewels::unexpected(LogError::not_a_log);
  }
  return reader::LogFileTrailerInfo{
    .metadata_chunk_handle =
      reader::ChunkHandle{
        .compression_type = CompressionType::zstd,
        .location = trailer_ptr->metadata_chunk_location,
        .chunk_reader_ptr = chunk_reader_ptr.get(),
        .chunk_compressor_ptr = chunk_compressor_ptr.get(),
      },
    .metrics_chunk_handle =
      reader::ChunkHandle{
        .compression_type = CompressionType::zstd,
        .location = trailer_ptr->metrics_chunk_location,
        .chunk_reader_ptr = chunk_reader_ptr.get(),
        .chunk_compressor_ptr = chunk_compressor_ptr.get(),
      },
    .index_chunk_handle =
      reader::ChunkHandle{
        .compression_type = CompressionType::zstd,
        .location = trailer_ptr->index_chunk_location,
        .chunk_reader_ptr = chunk_reader_ptr.get(),
        .chunk_compressor_ptr = chunk_compressor_ptr.get(),
      },
  };
}

} // namespace clockwork_logging::offboard
