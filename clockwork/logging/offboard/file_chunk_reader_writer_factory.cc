// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/file_chunk_reader_writer_factory.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/file_chunk_reader.hh"
#include "clockwork/logging/offboard/file_chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <memory_resource>
#include <string_view>
#include <utility>

namespace clockwork_logging::offboard
{

FileChunkReaderWriterFactory::FileChunkReaderWriterFactory(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource))
{
}

[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkReader>>
FileChunkReaderWriterFactory::make_chunk_reader(const LogUri& file_uri) const
{
  auto make_result = FileChunkReader<>::make_shared(file_uri.string(), memory_resource_);
  if (!make_result)
  {
    return jewels::unexpected(make_result.error());
  }
  return {std::move(make_result.value())};
}

[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkWriter>>
FileChunkReaderWriterFactory::make_chunk_writer(const LogUri& file_uri) const
{
  auto make_result = FileChunkWriter<>::make_shared(file_uri.string(), memory_resource_);
  if (!make_result)
  {
    return jewels::unexpected(make_result.error());
  }
  return {std::move(make_result.value())};
}

[[nodiscard]] LogExpected<bool> FileChunkReaderWriterFactory::exists(const LogUri& file_uri) const
{
  if (file_uri.scheme() != LogUriScheme::file)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  jewels::filesystem::Filesystem filesys{memory_resource_};
  filesys.set_verbosity(jewels::filesystem::Filesystem::ErrorVerbosity::off);
  const auto exists_result = filesys.exists(file_uri.path());
  if (!exists_result)
  {
    return jewels::unexpected(to_log_error(exists_result.error()));
  }
  return {exists_result.value()};
}

[[nodiscard]] LogExpected<void> FileChunkReaderWriterFactory::create_directories(const LogUri& file_uri) const
{
  if (file_uri.scheme() != LogUriScheme::file)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  jewels::filesystem::Filesystem filesys{memory_resource_};
  filesys.set_verbosity(jewels::filesystem::Filesystem::ErrorVerbosity::off);
  const auto create_result = filesys.create_directories(file_uri.path());
  if (!create_result)
  {
    return jewels::unexpected(to_log_error(create_result.error()));
  }
  return {};
}

[[nodiscard]] LogExpected<std::pmr::vector<std::pmr::string>>
FileChunkReaderWriterFactory::list_log_files(const LogUri& file_uri) const
{
  if (file_uri.scheme() != LogUriScheme::file)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  jewels::filesystem::Filesystem filesys{memory_resource_};
  filesys.set_verbosity(jewels::filesystem::Filesystem::ErrorVerbosity::off);
  const auto readdir_result = filesys.read_directory(
    file_uri.path(),
    [&file_uri, &filesys](const auto& dent)
    {
      const std::string_view file_name{&dent.d_name[0U]};
      bool is_reg = dent.d_type == DT_REG;
      if (dent.d_type == DT_UNKNOWN || dent.d_type == DT_LNK)
      {
        const auto is_reg_result = filesys.is_regular_file((file_uri / file_name).path());
        is_reg = is_reg_result && is_reg_result.value();
      }
      return is_reg && file_name.ends_with(log_file_suffix);
    });
  if (!readdir_result)
  {
    return jewels::unexpected(to_log_error(readdir_result.error()));
  }
  std::pmr::vector<std::pmr::string> log_files{memory_resource_};
  log_files.reserve(readdir_result.value().size());
  for (const auto& filename : readdir_result.value())
  {
    log_files.push_back((file_uri / filename).string());
  }
  return {std::move(log_files)};
}

[[nodiscard]] LogExpected<void>
FileChunkReaderWriterFactory::write_log_file(const LogUri& file_uri, std::span<const std::byte> data) const
{
  if (file_uri.scheme() != LogUriScheme::file)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  jewels::filesystem::Filesystem filesys{memory_resource_};
  filesys.set_verbosity(jewels::filesystem::Filesystem::ErrorVerbosity::off);
  auto open_result = filesys.open(file_uri.path(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!open_result)
  {
    return jewels::unexpected(to_log_error(open_result.error()));
  }
  auto& file_desc = open_result.value();
  if (const auto write_result = filesys.write(file_desc, data); !write_result)
  {
    return jewels::unexpected(to_log_error(write_result.error()));
  }
  if (const auto close_result = file_desc.close(); !close_result)
  {
    return jewels::unexpected(to_log_error(close_result.error()));
  }
  return {};
}

[[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
FileChunkReaderWriterFactory::read_log_file(const LogUri& file_uri) const
{
  if (file_uri.scheme() != LogUriScheme::file)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  jewels::filesystem::Filesystem filesys{memory_resource_};
  filesys.set_verbosity(jewels::filesystem::Filesystem::ErrorVerbosity::off);
  const auto open_result = filesys.open(file_uri.path(), O_RDONLY);
  if (!open_result)
  {
    return jewels::unexpected(to_log_error(open_result.error()));
  }
  const auto& file_desc = open_result.value();
  const auto size_result = filesys.get_size(file_desc);
  if (!size_result)
  {
    return jewels::unexpected(to_log_error(size_result.error()));
  }
  std::pmr::vector<std::byte> buffer(size_result.value(), std::byte{}, memory_resource_);
  if (const auto read_result = filesys.read(file_desc, buffer); !read_result)
  {
    return jewels::unexpected(to_log_error(read_result.error()));
  }
  return {std::move(buffer)};
}

} // namespace clockwork_logging::offboard
