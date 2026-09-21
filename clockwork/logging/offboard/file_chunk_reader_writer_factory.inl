// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// IWYU pragma: private, include "clockwork/logging/offboard/file_chunk_reader_writer_factory.hh"

#pragma once

#include "clockwork/logging/offboard/file_chunk_reader_writer_factory.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/file_chunk_reader.hh"
#include "clockwork/logging/offboard/file_chunk_writer.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <cstddef>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

template <typename FilesystemType>
FileChunkReaderWriterFactory<FilesystemType>::FileChunkReaderWriterFactory(
  jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)), filesystem_(memory_resource_)
{
  filesystem_.set_verbosity(jewels::filesystem::Filesystem::ErrorVerbosity::off);
}

template <typename FilesystemType>
[[nodiscard]] FilesystemType& FileChunkReaderWriterFactory<FilesystemType>::get_filesystem()
{
  return filesystem_;
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkReader>>
FileChunkReaderWriterFactory<FilesystemType>::make_chunk_reader(const LogUri& file_uri) const
{
  auto make_result = FileChunkReader<FilesystemType>::make_shared(file_uri.string(), memory_resource_);
  if (!make_result)
  {
    return jewels::unexpected(make_result.error());
  }
  return {std::move(make_result.value())};
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkWriter>>
FileChunkReaderWriterFactory<FilesystemType>::make_chunk_writer(const LogUri& file_uri) const
{
  auto make_result = FileChunkWriter<>::make_shared(file_uri.string(), memory_resource_);
  if (!make_result)
  {
    return jewels::unexpected(make_result.error());
  }
  return {std::move(make_result.value())};
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<bool> FileChunkReaderWriterFactory<FilesystemType>::exists(const LogUri& file_uri)
{
  if (file_uri.scheme() != LogUriScheme::file)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto exists_result = filesystem_.exists(file_uri.path());
  if (!exists_result)
  {
    return jewels::unexpected(to_log_error(exists_result.error()));
  }
  return {exists_result.value()};
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<size_t> FileChunkReaderWriterFactory<FilesystemType>::get_size(const LogUri& file_uri)
{
  if (file_uri.scheme() != LogUriScheme::file)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto size_result = filesystem_.get_size(file_uri.path());
  if (!size_result)
  {
    return jewels::unexpected(to_log_error(size_result.error()));
  }
  return {size_result.value()};
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<void> FileChunkReaderWriterFactory<FilesystemType>::create_directories(const LogUri& file_uri)
{
  if (file_uri.scheme() != LogUriScheme::file)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto create_result = filesystem_.create_directories(file_uri.path());
  if (!create_result)
  {
    return jewels::unexpected(to_log_error(create_result.error()));
  }
  return {};
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<std::pmr::vector<LogUri>>
FileChunkReaderWriterFactory<FilesystemType>::list_log_files(const LogUri& file_uri, std::string_view suffix)
{
  if (file_uri.scheme() != LogUriScheme::file)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto readdir_result = filesystem_.read_directory(
    file_uri.path(),
    [this, &file_uri, suffix](const auto& dent)
    {
      const std::string_view file_name{&dent.d_name[0U]};
      if (!file_name.ends_with(suffix))
      {
        return false;
      }
      bool is_reg = dent.d_type == DT_REG;
      if (dent.d_type == DT_UNKNOWN || dent.d_type == DT_LNK)
      {
        const auto is_reg_result = filesystem_.is_regular_file((file_uri / file_name).path());
        is_reg = is_reg_result && is_reg_result.value();
      }
      return is_reg;
    });
  if (!readdir_result)
  {
    return jewels::unexpected(to_log_error(readdir_result.error()));
  }
  std::pmr::vector<LogUri> log_files{memory_resource_};
  log_files.reserve(readdir_result.value().size());
  for (const auto& filename : readdir_result.value())
  {
    auto uri_result = LogUri::try_make((file_uri / filename).string(), memory_resource_);
    if (uri_result)
    {
      log_files.emplace_back(std::move(uri_result).value());
    }
  }
  std::ranges::sort(log_files);
  return {std::move(log_files)};
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<std::pmr::vector<LogUri>>
FileChunkReaderWriterFactory<FilesystemType>::list_subdirs(const LogUri& file_uri)
{
  if (file_uri.scheme() != LogUriScheme::file)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto readdir_result = filesystem_.read_directory(
    file_uri.path(),
    [this, &file_uri](const auto& dent)
    {
      const std::string_view dir_name{&dent.d_name[0U]};
      if (dir_name == "." || dir_name == "..")
      {
        return false;
      }
      bool is_dir = dent.d_type == DT_DIR;
      if (dent.d_type == DT_UNKNOWN || dent.d_type == DT_LNK)
      {
        const auto is_dir_result = filesystem_.is_directory((file_uri / dir_name).path());
        is_dir = is_dir_result && is_dir_result.value();
      }
      return is_dir;
    });
  if (!readdir_result)
  {
    return jewels::unexpected(to_log_error(readdir_result.error()));
  }
  std::pmr::vector<LogUri> subdirs{memory_resource_};
  subdirs.reserve(readdir_result.value().size());
  for (const auto& dir_name : readdir_result.value())
  {
    auto uri_result = LogUri::try_make((file_uri / dir_name).string(), memory_resource_);
    if (uri_result)
    {
      subdirs.emplace_back(std::move(uri_result).value());
    }
  }
  std::ranges::sort(subdirs);
  return {std::move(subdirs)};
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<void>
FileChunkReaderWriterFactory<FilesystemType>::write_log_file(const LogUri& file_uri, std::span<const std::byte> data)
{
  if (file_uri.scheme() != LogUriScheme::file)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  auto open_result = filesystem_.open(file_uri.path(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!open_result)
  {
    return jewels::unexpected(to_log_error(open_result.error()));
  }
  auto& file_desc = open_result.value();
  if (const auto write_result = filesystem_.write(file_desc, data); !write_result)
  {
    return jewels::unexpected(to_log_error(write_result.error()));
  }
  if (const auto close_result = file_desc.close(); !close_result)
  {
    return jewels::unexpected(to_log_error(close_result.error()));
  }
  return {};
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
FileChunkReaderWriterFactory<FilesystemType>::read_log_file(const LogUri& file_uri)
{
  if (file_uri.scheme() != LogUriScheme::file)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto open_result = filesystem_.open(file_uri.path(), O_RDONLY);
  if (!open_result)
  {
    return jewels::unexpected(to_log_error(open_result.error()));
  }
  const auto& file_desc = open_result.value();
  const auto size_result = filesystem_.get_size(file_desc);
  if (!size_result)
  {
    return jewels::unexpected(to_log_error(size_result.error()));
  }
  std::pmr::vector<std::byte> buffer(size_result.value(), std::byte{}, memory_resource_);
  if (const auto read_result = filesystem_.read(file_desc, buffer); !read_result)
  {
    return jewels::unexpected(to_log_error(read_result.error()));
  }
  return {std::move(buffer)};
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<std::span<std::byte>> FileChunkReaderWriterFactory<FilesystemType>::read_log_file(
  const LogUri& file_uri, size_t offset, std::span<std::byte> buffer_span)
{
  if (file_uri.scheme() != LogUriScheme::file)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto open_result = filesystem_.open(file_uri.path(), O_RDONLY);
  if (!open_result)
  {
    return jewels::unexpected(to_log_error(open_result.error()));
  }
  const auto& file_desc = open_result.value();
  const auto read_result = filesystem_.read(file_desc, offset, buffer_span);
  if (!read_result)
  {
    return jewels::unexpected(to_log_error(read_result.error()));
  }
  return buffer_span.first(read_result.value());
}

} // namespace clockwork_logging::offboard
