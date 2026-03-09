// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/memory/memory_resource.hh"

#include <aws/core/client/CoreErrors.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/CompletedPart.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace clockwork_logging::offboard
{

/// Return value from s3_list_objects
struct S3ListObjectsResult
{
  /// List of objects under the URI
  std::pmr::vector<std::pmr::string> objects;
  /// List of prefixes under the URI
  std::pmr::vector<std::pmr::string> prefixes;
};

/// Interface implemented by classes that handle the interface to S3
class S3UtilsInterface
{
public:
  explicit S3UtilsInterface() = default;
  virtual ~S3UtilsInterface() = default;

  S3UtilsInterface(const S3UtilsInterface& other) = delete;
  S3UtilsInterface& operator=(const S3UtilsInterface& other) = delete;
  S3UtilsInterface(S3UtilsInterface&&) noexcept = delete;
  S3UtilsInterface& operator=(S3UtilsInterface&&) noexcept = delete;

  /// Send an S3 ListObjectsV2 request
  /// @param[in] s3_uri S3 URI
  /// @return Objects under the prefix or LogError on failure
  [[nodiscard]] virtual LogExpected<S3ListObjectsResult> list_objects_v2(const LogUri& s3_uri) const = 0;

  /// Send an S3 CreateMultipartUpload request
  /// @param[in] s3_uri S3 URI
  /// @return Multipart upload ID or LogError on failure
  [[nodiscard]] virtual LogExpected<std::pmr::string> create_multipart_upload(const LogUri& s3_uri) const = 0;

  /// Send an S3 UploadPart request
  /// @param[in] s3_uri S3 URI
  /// @param[in] upload_id Multipart upload ID
  /// @param[in] part_number Part number
  /// @param[in] buffers Data buffers to upload
  /// @return Completed part or LogError on failure
  [[nodiscard]] virtual LogExpected<Aws::S3::Model::CompletedPart> upload_part(
    const LogUri& s3_uri,
    std::string_view upload_id,
    int32_t part_number,
    std::pmr::vector<std::pmr::vector<std::byte>> buffers) const = 0;

  /// Send an S3 CreateMultipartUpload request
  /// @param[in] s3_uri S3 URI
  /// @param[in] upload_id Multipart upload ID
  /// @param[in] completed_parts Completed upload parts
  /// @return LogError on failure
  [[nodiscard]] virtual LogExpected<void> complete_multipart_upload(
    const LogUri& s3_uri,
    std::string_view upload_id,
    std::span<Aws::S3::Model::CompletedPart> completed_parts) const = 0;

  /// Send an S3 PutObject request
  /// @param[in] s3_uri S3 URI
  /// @param[in] buffers Data buffers to upload
  /// @return LogError on failure
  [[nodiscard]] virtual LogExpected<void>
  put_object(const LogUri& s3_uri, std::pmr::vector<std::pmr::vector<std::byte>> buffers) const = 0;

  /// Get the size of an S3 object
  /// @param[in] s3_uri S3 URI
  /// @return Object size or LogError on failure
  [[nodiscard]] virtual LogExpected<size_t> get_object_size(const LogUri& s3_uri) const = 0;

  /// Read an object from S3
  /// @param[in] s3_uri S3 URI
  /// @param[in] offset File offset bytes
  /// @param[in] length Number of bytes to read
  [[nodiscard]] virtual LogExpected<std::pmr::vector<std::byte>>
  get_object(const LogUri& s3_uri, size_t offset, size_t length) const = 0;

  /// Delete an S3 object
  /// @param[in] s3_uri S3 URI
  /// @return LogError on failure
  [[nodiscard]] virtual LogExpected<void> delete_object(const LogUri& s3_uri) const = 0;
};

} // namespace clockwork_logging::offboard
