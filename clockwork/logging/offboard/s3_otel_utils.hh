// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/s3_utils_interface.hh"
#include "jewels/memory/memory_resource.hh"

#include <aws/core/client/AWSClient.h>          // IWYU pragma: keep
#include <aws/core/client/AWSErrorMarshaller.h> // IWYU pragma: keep
#include <aws/core/client/CoreErrors.h>
#include <aws/s3/S3Client.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging::offboard
{

/// Utility class that handles the interface to S3
class S3OtelUtils : public S3UtilsInterface
{
public:
  /// Constructor
  /// @param[in] memory_interface Memory interface
  /// s3_client_ptr S3 client pointer
  S3OtelUtils(jewels::memory::MemoryResource memory_resource, std::shared_ptr<Aws::S3::S3Client> s3_client_ptr);

  ~S3OtelUtils() override = default;

  S3OtelUtils(const S3OtelUtils& other) = delete;
  S3OtelUtils& operator=(const S3OtelUtils& other) = delete;
  S3OtelUtils(S3OtelUtils&&) noexcept = delete;
  S3OtelUtils& operator=(S3OtelUtils&&) noexcept = delete;

  /// @see S3UtilsInterface:list_objects
  [[nodiscard]] LogExpected<S3ListObjectsResult> list_objects_v2(const LogUri& s3_uri) const override;

  /// @see S3UtilsInterface:create_multipart_upload
  [[nodiscard]] LogExpected<std::pmr::string> create_multipart_upload(const LogUri& s3_uri) const override;

  /// @see S3UtilsInterface:upload_part
  [[nodiscard]] LogExpected<Aws::S3::Model::CompletedPart> upload_part(
    const LogUri& s3_uri,
    std::string_view upload_id,
    int32_t part_number,
    std::pmr::vector<std::pmr::vector<std::byte>> buffers) const override;

  /// @see S3UtilsInterface:complete_multipart_upload
  [[nodiscard]] LogExpected<void> complete_multipart_upload(
    const LogUri& s3_uri,
    std::string_view upload_id,
    std::span<Aws::S3::Model::CompletedPart> completed_parts) const override;

  /// @see S3UtilsInterface:put_object
  [[nodiscard]] LogExpected<void>
  put_object(const LogUri& s3_uri, std::pmr::vector<std::pmr::vector<std::byte>> buffers) const override;

  /// @see S3UtilsInterface:get_object_size
  [[nodiscard]] LogExpected<size_t> get_object_size(const LogUri& s3_uri) const override;

  /// @see S3UtilsInterface:get_object
  [[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
  get_object(const LogUri& s3_uri, size_t offset, size_t length) const override;

  /// @see S3UtilsInterface:get_object
  [[nodiscard]] LogExpected<std::span<std::byte>>
  get_object(const LogUri& s3_uri, size_t offset, std::span<std::byte> buffer_span) const override;

  /// @see S3UtilsInterface:delete_object
  [[nodiscard]] LogExpected<void> delete_object(const LogUri& s3_uri) const override;

  /// Callback for retries from S3RetryStrategy
  static void retry_callback(
    const Aws::Client::AWSError<Aws::Client::CoreErrors>& error, int64_t attempted_retries, bool should_retry);

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// S3 client pointer
  std::shared_ptr<Aws::S3::S3Client> s3_client_ptr_;
};

} // namespace clockwork_logging::offboard
