// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"

#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <memory>
#include <memory_resource>
#include <string_view>
#include <utility>

namespace clockwork_logging::offboard
{

ChunkReaderWriterFactory::ChunkReaderWriterFactory(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)), file_factory_(memory_resource_)
{
}

[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkReader>>
ChunkReaderWriterFactory::make_chunk_reader(std::string_view uri_str)
{
  const auto make_result = LogUri::try_make(uri_str, memory_resource_);
  if (!make_result)
  {
    jewels::log_cerr_error("Invalid log URI: {}", uri_str);
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto& log_uri = make_result.value();
  switch (log_uri.scheme())
  {
  case LogUriScheme::file:
    return file_factory_.make_chunk_reader(log_uri);
  case LogUriScheme::s3:
    if (!maybe_s3_factory_)
    {
      maybe_s3_factory_.emplace(memory_resource_);
    }
    return maybe_s3_factory_->make_chunk_reader(log_uri);
  }
  __builtin_unreachable();
}

[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkWriter>>
ChunkReaderWriterFactory::make_chunk_writer(std::string_view uri_str)
{
  const auto make_result = LogUri::try_make(uri_str, memory_resource_);
  if (!make_result)
  {
    jewels::log_cerr_error("Invalid log URI: {}", uri_str);
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto& log_uri = make_result.value();
  switch (log_uri.scheme())
  {
  case LogUriScheme::file:
    return file_factory_.make_chunk_writer(log_uri);
  case LogUriScheme::s3:
    if (!maybe_s3_factory_)
    {
      maybe_s3_factory_.emplace(memory_resource_);
    }
    return maybe_s3_factory_->make_chunk_writer(log_uri);
  }
  __builtin_unreachable();
}

[[nodiscard]] LogExpected<bool> ChunkReaderWriterFactory::exists(std::string_view uri_str)
{
  const auto make_result = LogUri::try_make(uri_str, memory_resource_);
  if (!make_result)
  {
    jewels::log_cerr_error("Invalid log URI: {}", uri_str);
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  auto log_uri = make_result.value();
  while (!log_uri.path().empty() && log_uri.path() != "/" && log_uri.path().back() == '/')
  {
    log_uri = log_uri.parent_uri();
  }
  switch (log_uri.scheme())
  {
  case LogUriScheme::file:
    return file_factory_.exists(log_uri);
  case LogUriScheme::s3:
    if (!maybe_s3_factory_)
    {
      maybe_s3_factory_.emplace(memory_resource_);
    }
    return maybe_s3_factory_->exists(log_uri);
  }
  __builtin_unreachable();
}

[[nodiscard]] LogExpected<void> ChunkReaderWriterFactory::create_directories(std::string_view uri_str)
{
  const auto make_result = LogUri::try_make(uri_str, memory_resource_);
  if (!make_result)
  {
    jewels::log_cerr_error("Invalid log URI: {}", uri_str);
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto& log_uri = make_result.value();
  switch (log_uri.scheme())
  {
  case LogUriScheme::file:
    return file_factory_.create_directories(log_uri);
  case LogUriScheme::s3:
    // Create directories is a noop on S3
    return {};
  }
  __builtin_unreachable();
}

[[nodiscard]] LogExpected<std::pmr::vector<std::pmr::string>>
ChunkReaderWriterFactory::list_log_files(std::string_view uri_str)
{
  const auto make_result = LogUri::try_make(uri_str, memory_resource_);
  if (!make_result)
  {
    jewels::log_cerr_error("Invalid log URI: {}", uri_str);
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto& log_uri = make_result.value();
  switch (log_uri.scheme())
  {
  case LogUriScheme::file:
    return file_factory_.list_log_files(log_uri);
  case LogUriScheme::s3:
    if (!maybe_s3_factory_)
    {
      maybe_s3_factory_.emplace(memory_resource_);
    }
    return maybe_s3_factory_->list_log_files(log_uri);
  }
  __builtin_unreachable();
}

[[nodiscard]] LogExpected<void>
ChunkReaderWriterFactory::write_log_file(std::string_view uri_str, std::span<const std::byte> data)
{
  const auto make_result = LogUri::try_make(uri_str, memory_resource_);
  if (!make_result)
  {
    jewels::log_cerr_error("Invalid log URI: {}", uri_str);
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto& log_uri = make_result.value();
  switch (log_uri.scheme())
  {
  case LogUriScheme::file:
    return file_factory_.write_log_file(log_uri, data);
  case LogUriScheme::s3:
    if (!maybe_s3_factory_)
    {
      maybe_s3_factory_.emplace(memory_resource_);
    }
    return maybe_s3_factory_->write_log_file(log_uri, data);
  }
  __builtin_unreachable();
}

[[nodiscard]] LogExpected<std::pmr::vector<std::byte>> ChunkReaderWriterFactory::read_log_file(std::string_view uri_str)
{
  const auto make_result = LogUri::try_make(uri_str, memory_resource_);
  if (!make_result)
  {
    jewels::log_cerr_error("Invalid log URI: {}", uri_str);
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto& log_uri = make_result.value();
  switch (log_uri.scheme())
  {
  case LogUriScheme::file:
    return file_factory_.read_log_file(log_uri);
  case LogUriScheme::s3:
    if (!maybe_s3_factory_)
    {
      maybe_s3_factory_.emplace(memory_resource_);
    }
    return maybe_s3_factory_->read_log_file(log_uri);
  }
  __builtin_unreachable();
}

} // namespace clockwork_logging::offboard
