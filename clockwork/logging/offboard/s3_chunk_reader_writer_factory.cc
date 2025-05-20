// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/s3_chunk_reader_writer_factory.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/init_aws_api.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/s3_chunk_reader.hh"
#include "clockwork/logging/offboard/s3_chunk_writer.hh"
#include "clockwork/logging/offboard/s3_retry_strategy.hh"
#include "clockwork/logging/offboard/s3_utils.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <aws/core/client/ClientConfiguration.h>

#include <deque>
#include <memory_resource>
#include <string_view>
#include <utility>

namespace clockwork_logging::offboard
{

namespace
{

/// AWS endpoint URL environment variable
constexpr auto aws_endpoint_url_env_var = "AWS_ENDPOINT_URL";

/// AWS CA bundle environment variable
constexpr auto aws_ca_bundle_env_var = "AWS_CA_BUNDLE";

} // namespace

S3ChunkReaderWriterFactory::S3ChunkReaderWriterFactory(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource))
{
  init_aws_api();

  aws_client_config_ptr_ = std::allocate_shared<
    Aws::Client::ClientConfiguration,
    std::pmr::polymorphic_allocator<Aws::Client::ClientConfiguration>>(memory_resource_);

  aws_client_config_ptr_->retryStrategy =
    std::allocate_shared<S3RetryStrategy, std::pmr::polymorphic_allocator<S3RetryStrategy>>(
      memory_resource_, memory_resource_);

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

[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkReader>>
S3ChunkReaderWriterFactory::make_chunk_reader(const LogUri& file_uri)
{
  auto make_result = S3ChunkReader::make_shared(memory_resource_, file_uri.string(), get_s3_client_ptr());
  if (!make_result)
  {
    return jewels::unexpected(make_result.error());
  }
  return {std::move(make_result).value()};
}

[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkWriter>>
S3ChunkReaderWriterFactory::make_chunk_writer(const LogUri& file_uri)
{
  auto make_result = S3ChunkWriter::make_shared(memory_resource_, file_uri.string(), get_s3_client_ptr());
  if (!make_result)
  {
    return jewels::unexpected(make_result.error());
  }
  return {std::move(make_result).value()};
}

[[nodiscard]] LogExpected<bool> S3ChunkReaderWriterFactory::exists(const LogUri& file_uri)
{
  const auto list_result = s3_list_objects_v2(memory_resource_, *get_s3_client_ptr(), file_uri);
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

[[nodiscard]] LogExpected<std::pmr::vector<std::pmr::string>>
S3ChunkReaderWriterFactory::list_log_files(const LogUri& file_uri)
{
  auto log_uri = file_uri;
  if (!log_uri.path().ends_with("/"))
  {
    log_uri += "/";
  }
  const auto list_result = s3_list_objects_v2(memory_resource_, *get_s3_client_ptr(), log_uri);
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

[[nodiscard]] LogExpected<void>
S3ChunkReaderWriterFactory::write_log_file(const LogUri& file_uri, std::span<const std::byte> data)
{
  if (file_uri.scheme() != LogUriScheme::s3)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  std::pmr::vector<std::pmr::vector<std::byte>> buffers(
    {std::pmr::vector<std::byte>{data.begin(), data.end(), memory_resource_}}, memory_resource_);
  return s3_put_object(*get_s3_client_ptr(), file_uri, std::move(buffers));
}

[[nodiscard]] LogExpected<std::pmr::vector<std::byte>> S3ChunkReaderWriterFactory::read_log_file(const LogUri& file_uri)
{
  if (file_uri.scheme() != LogUriScheme::s3)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto s3_client_ptr = get_s3_client_ptr();
  const auto size_result = s3_get_object_size(*s3_client_ptr, file_uri);
  if (!size_result)
  {
    return jewels::unexpected(size_result.error());
  }
  return s3_get_object(memory_resource_, *s3_client_ptr, file_uri, 0U, size_result.value());
}

[[nodiscard]] jewels::memory::NonNullSharedPtr<Aws::S3::S3Client> S3ChunkReaderWriterFactory::get_s3_client_ptr()
{
  if (!s3_client_ptr_)
  {
    s3_client_ptr_ = std::allocate_shared<Aws::S3::S3Client, std::pmr::polymorphic_allocator<Aws::S3::S3Client>>(
      memory_resource_, *aws_client_config_ptr_);
  }
  return jewels::memory::NonNullSharedPtr<Aws::S3::S3Client>{s3_client_ptr_};
}

} // namespace clockwork_logging::offboard
