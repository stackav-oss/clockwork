// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// IWYU pragma: private, include "clockwork/logging/offboard/s3_chunk_reader_writer_factory.hh"

#pragma once

#include "clockwork/logging/offboard/s3_chunk_reader_writer_factory.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/init_aws_api.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/s3_chunk_reader.hh"
#include "clockwork/logging/offboard/s3_chunk_writer.hh"
#include "clockwork/logging/offboard/s3_retry_strategy.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <aws/core/client/ClientConfiguration.h>
#include <aws/s3/S3Client.h>

#include <cstddef>
#include <deque>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

template <typename S3UtilsType>
S3ChunkReaderWriterFactory<S3UtilsType>::S3ChunkReaderWriterFactory(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource))
{
  init_aws_api();

  aws_client_config_ptr_ = std::allocate_shared<
    Aws::Client::ClientConfiguration,
    std::pmr::polymorphic_allocator<Aws::Client::ClientConfiguration>>(memory_resource_);

  aws_client_config_ptr_->retryStrategy =
    std::allocate_shared<S3RetryStrategy<S3UtilsType>, std::pmr::polymorphic_allocator<S3RetryStrategy<S3UtilsType>>>(
      memory_resource_, memory_resource_);

  // Use long timeouts to prevent infinite retries when operations take more than a few seconds
  aws_client_config_ptr_->connectTimeoutMs = aws_connect_timeout_ms;
  aws_client_config_ptr_->requestTimeoutMs = aws_request_timeout_ms;
  aws_client_config_ptr_->httpRequestTimeoutMs = aws_http_request_timeout_ms;

  const char* aws_endpoint_url_ptr = nolint_helper::get_environment_variable(aws_endpoint_url_env_var);
  const char* aws_ca_bundle_ptr = nolint_helper::get_environment_variable(aws_ca_bundle_env_var);
  if (aws_endpoint_url_ptr != nullptr)
  {
    aws_client_config_ptr_->endpointOverride = aws_endpoint_url_ptr;
  }
  if (aws_ca_bundle_ptr != nullptr)
  {
    aws_client_config_ptr_->caFile = aws_ca_bundle_ptr;
  }
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkReader>>
S3ChunkReaderWriterFactory<S3UtilsType>::make_chunk_reader(const LogUri& file_uri)
{
  auto make_result = S3ChunkReader::make_shared(memory_resource_, file_uri.string(), get_s3_utils_ptr());
  if (!make_result)
  {
    return jewels::unexpected(make_result.error());
  }
  return {std::move(make_result).value()};
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkWriter>>
S3ChunkReaderWriterFactory<S3UtilsType>::make_chunk_writer(const LogUri& file_uri)
{
  auto make_result = S3ChunkWriter::make_shared(memory_resource_, file_uri.string(), get_s3_utils_ptr());
  if (!make_result)
  {
    return jewels::unexpected(make_result.error());
  }
  return {std::move(make_result).value()};
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<bool> S3ChunkReaderWriterFactory<S3UtilsType>::exists(const LogUri& file_uri)
{
  const auto list_result = get_s3_utils_ptr()->list_objects_v2(file_uri);
  if (!list_result)
  {
    return jewels::unexpected(list_result.error());
  }
  if (!file_uri.has_filename())
  {
    return !list_result.value().objects.empty() || !list_result.value().prefixes.empty();
  }
  const auto file_prefix = file_uri.path().substr(1U);
  for (const auto& entry : list_result.value().objects)
  {
    if (file_prefix == entry)
    {
      return true;
    }
  }
  for (const auto& entry : list_result.value().prefixes)
  {
    if (file_prefix == entry.substr(0U, entry.size() - 1U))
    {
      return true;
    }
  }
  return false;
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<std::pmr::vector<std::pmr::string>>
S3ChunkReaderWriterFactory<S3UtilsType>::list_log_files(const LogUri& file_uri)
{
  auto log_uri = file_uri;
  if (!log_uri.path().ends_with("/"))
  {
    log_uri += "/";
  }
  const auto list_result = get_s3_utils_ptr()->list_objects_v2(log_uri);
  if (!list_result)
  {
    return jewels::unexpected(list_result.error());
  }
  std::pmr::vector<std::pmr::string> log_files{memory_resource_};
  log_files.reserve(list_result.value().objects.size());
  const auto file_prefix = log_uri.path().substr(1U);
  for (const auto& object : list_result.value().objects)
  {
    if (object.ends_with(log_file_suffix))
    {
      const auto object_uri = file_uri / object.substr(file_prefix.size());
      log_files.push_back(object_uri.string());
    }
  }
  return {std::move(log_files)};
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<void>
S3ChunkReaderWriterFactory<S3UtilsType>::write_log_file(const LogUri& file_uri, std::span<const std::byte> data)
{
  if (file_uri.scheme() != LogUriScheme::s3)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  std::pmr::vector<std::pmr::vector<std::byte>> buffers(
    {std::pmr::vector<std::byte>{data.begin(), data.end(), memory_resource_}}, memory_resource_);
  return get_s3_utils_ptr()->put_object(file_uri, std::move(buffers));
}

template <typename S3UtilsType>
[[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
S3ChunkReaderWriterFactory<S3UtilsType>::read_log_file(const LogUri& file_uri)
{
  if (file_uri.scheme() != LogUriScheme::s3)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto s3_utils_ptr = get_s3_utils_ptr();
  const auto size_result = s3_utils_ptr->get_object_size(file_uri);
  if (!size_result)
  {
    return jewels::unexpected(size_result.error());
  }
  return s3_utils_ptr->get_object(file_uri, 0U, size_result.value());
}

template <typename S3UtilsType>
[[nodiscard]] jewels::memory::NonNullSharedPtr<S3UtilsType> S3ChunkReaderWriterFactory<S3UtilsType>::get_s3_utils_ptr()
{
  if (!s3_utils_ptr_)
  {
    auto s3_client_ptr = std::allocate_shared<Aws::S3::S3Client, std::pmr::polymorphic_allocator<Aws::S3::S3Client>>(
      memory_resource_, *aws_client_config_ptr_);
    s3_utils_ptr_ = std::allocate_shared<S3UtilsType, std::pmr::polymorphic_allocator<S3UtilsType>>(
      memory_resource_, memory_resource_, std::move(s3_client_ptr));
  }
  return jewels::memory::NonNullSharedPtr<S3UtilsType>{s3_utils_ptr_};
}

} // namespace clockwork_logging::offboard
