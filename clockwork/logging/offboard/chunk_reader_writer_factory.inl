// IWYU pragma: private, include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#pragma once

#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <google/protobuf/text_format.h>

#include <cstddef>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

template <typename S3UtilsType, typename FilesystemType>
ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::ChunkReaderWriterFactory(
  jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)), file_factory_(memory_resource_)
{
}

template <typename S3UtilsType, typename FilesystemType>
[[nodiscard]] FilesystemType& ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::get_filesystem()
{
  return file_factory_.get_filesystem();
}

template <typename S3UtilsType, typename FilesystemType>
[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkReader>>
ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::make_chunk_reader(std::string_view uri_str)
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
}

template <typename S3UtilsType, typename FilesystemType>
[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkWriter>>
ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::make_chunk_writer(std::string_view uri_str)
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
}

template <typename S3UtilsType, typename FilesystemType>
[[nodiscard]] LogExpected<bool> ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::exists(std::string_view uri_str)
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
}

template <typename S3UtilsType, typename FilesystemType>
[[nodiscard]] LogExpected<size_t>
ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::get_size(std::string_view uri_str)
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
    return file_factory_.get_size(log_uri);
  case LogUriScheme::s3:
    if (!maybe_s3_factory_)
    {
      maybe_s3_factory_.emplace(memory_resource_);
    }
    return maybe_s3_factory_->get_size(log_uri);
  }
}

template <typename S3UtilsType, typename FilesystemType>
[[nodiscard]] LogExpected<void>
ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::create_directories(std::string_view uri_str)
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
}

template <typename S3UtilsType, typename FilesystemType>
[[nodiscard]] LogExpected<std::pmr::vector<LogUri>>
ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::list_log_files(std::string_view uri_str, std::string_view suffix)
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
    return file_factory_.list_log_files(log_uri, suffix);
  case LogUriScheme::s3:
    if (!maybe_s3_factory_)
    {
      maybe_s3_factory_.emplace(memory_resource_);
    }
    return maybe_s3_factory_->list_log_files(log_uri, suffix);
  }
}

template <typename S3UtilsType, typename FilesystemType>
[[nodiscard]] LogExpected<std::pmr::vector<LogUri>>
ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::list_subdirs(std::string_view uri_str)
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
    return file_factory_.list_subdirs(log_uri);
  case LogUriScheme::s3:
    if (!maybe_s3_factory_)
    {
      maybe_s3_factory_.emplace(memory_resource_);
    }
    return maybe_s3_factory_->list_subdirs(log_uri);
  }
}

template <typename S3UtilsType, typename FilesystemType>
[[nodiscard]] LogExpected<void> ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::write_log_file(
  std::string_view uri_str, std::span<const std::byte> data)
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
}

template <typename S3UtilsType, typename FilesystemType>
[[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::read_log_file(std::string_view uri_str)
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
}

template <typename S3UtilsType, typename FilesystemType>
[[nodiscard]] LogExpected<std::span<std::byte>> ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::read_log_file(
  std::string_view uri_str, size_t offset, std::span<std::byte> buffer_span)
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
    return file_factory_.read_log_file(log_uri, offset, buffer_span);
  case LogUriScheme::s3:
    if (!maybe_s3_factory_)
    {
      maybe_s3_factory_.emplace(memory_resource_);
    }
    return maybe_s3_factory_->read_log_file(log_uri, offset, buffer_span);
  }
}

template <typename S3UtilsType, typename FilesystemType>
template <typename ProtobufType>
[[nodiscard]] LogExpected<void> ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::write_text_proto(
  std::string_view uri_str, std::string_view header, const ProtobufType& protobuf)
{
  std::string header_str{header};
  std::string proto_str;
  if (!google::protobuf::TextFormat::PrintToString(protobuf, &proto_str))
  {
    jewels::log_cerr_error("Failed to serialize proto for {}", uri_str);
    return jewels::unexpected(LogError::invalid_log_file);
  }
  header_str.append(proto_str);
  return write_log_file(uri_str, std::as_bytes(std::span{header_str}));
}

template <typename S3UtilsType, typename FilesystemType>
template <typename ProtobufType>
[[nodiscard]] LogExpected<ProtobufType> ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::read_text_proto(
  std::string_view uri_str, ProtobufReadMode read_mode)
{
  const auto& read_result = read_log_file(uri_str);
  if (!read_result)
  {
    return jewels::unexpected(read_result.error());
  }
  if (read_mode == ProtobufReadMode::fail_if_empty && read_result.value().empty())
  {
    return jewels::unexpected(LogError::empty_metadata_file);
  }
  const std::string proto_str{nolint_helper::byte_span_to_string_view(read_result.value())};
  ProtobufType protobuf;
  if (!google::protobuf::TextFormat::ParseFromString(proto_str, &protobuf))
  {
    jewels::log_cerr_error("Failed to parse proto from {}", uri_str);
    return jewels::unexpected(LogError::invalid_log_file);
  }
  return {std::move(protobuf)};
}

template <typename S3UtilsType, typename FilesystemType>
template <typename ProtobufType>
[[nodiscard]] LogExpected<void> ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::write_binary_proto(
  std::string_view uri_str, const ProtobufType& protobuf)
{
  std::string proto_str;
  if (!protobuf.SerializeToString(&proto_str))
  {
    jewels::log_cerr_error("Failed to serialize proto for {}", uri_str);
    return jewels::unexpected(LogError::invalid_log_file);
  }
  return write_log_file(uri_str, std::as_bytes(std::span{proto_str}));
}

template <typename S3UtilsType, typename FilesystemType>
template <typename ProtobufType>
[[nodiscard]] LogExpected<ProtobufType> ChunkReaderWriterFactory<S3UtilsType, FilesystemType>::read_binary_proto(
  std::string_view uri_str, ProtobufReadMode read_mode)
{
  const auto& read_result = read_log_file(uri_str);
  if (!read_result)
  {
    return jewels::unexpected(read_result.error());
  }
  if (read_mode == ProtobufReadMode::fail_if_empty && read_result.value().empty())
  {
    return jewels::unexpected(LogError::empty_metadata_file);
  }
  const std::string proto_str{nolint_helper::byte_span_to_string_view(read_result.value())};
  ProtobufType protobuf;
  if (!protobuf.ParseFromString(proto_str))
  {
    jewels::log_cerr_error("Failed to parse proto from {}", uri_str);
    return jewels::unexpected(LogError::invalid_log_file);
  }
  return {std::move(protobuf)};
}

} // namespace clockwork_logging::offboard
