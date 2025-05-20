// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/memory/memory_resource.hh"

#include <aws/s3/S3Client.h>
#include <aws/s3/model/CompletedPart.h>

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
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

/// Send an S3 ListObjectsV2 request
/// @param[in] memory_resource Memory resource
/// @param[in] s3_client S3 client
/// @param[in] s3_uri S3 URI
/// @return Objects under the prefix or LogError on failure
[[nodiscard]] LogExpected<S3ListObjectsResult> s3_list_objects_v2(
  jewels::memory::MemoryResource memory_resource, const Aws::S3::S3Client& s3_client, const LogUri& s3_uri);

/// Send an S3 CreateMultipartUpload request
/// @param[in] memory_resource Memory resource
/// @param[in] s3_client S3 client
/// @param[in] s3_uri S3 URI
/// @return Multipart upload ID or LogError on failure
[[nodiscard]] LogExpected<std::pmr::string> s3_create_multipart_upload(
  jewels::memory::MemoryResource memory_resource, const Aws::S3::S3Client& s3_client, const LogUri& s3_uri);

/// Send an S3 UploadPart request
/// @param[in] s3_client S3 client
/// @param[in] s3_uri S3 URI
/// @param[in] upload_id Multipart upload ID
/// @param[in] part_number Part number
/// @param[in] buffers Data buffers to upload
/// @return Completed part or LogError on failure
[[nodiscard]] LogExpected<Aws::S3::Model::CompletedPart> s3_upload_part(
  const Aws::S3::S3Client& s3_client,
  const LogUri& s3_uri,
  std::string_view upload_id,
  int32_t part_number,
  std::pmr::vector<std::pmr::vector<std::byte>> buffers);

/// Send an S3 CreateMultipartUpload request
/// @param[in] s3_client S3 client
/// @param[in] s3_uri S3 URI
/// @param[in] upload_id Multipart upload ID
/// @param[in] completed_parts Completed upload parts
/// @return LogError on failure
[[nodiscard]] LogExpected<void> s3_complete_multipart_upload(
  const Aws::S3::S3Client& s3_client,
  const LogUri& s3_uri,
  std::string_view upload_id,
  std::span<Aws::S3::Model::CompletedPart> completed_parts);

/// Send an S3 PutObject request
/// @param[in] s3_client S3 client
/// @param[in] s3_uri S3 URI
/// @param[in] buffers Data buffers to upload
/// @return LogError on failure
[[nodiscard]] LogExpected<void> s3_put_object(
  const Aws::S3::S3Client& s3_client, const LogUri& s3_uri, std::pmr::vector<std::pmr::vector<std::byte>> buffers);

/// Get the size of an S3 object
/// @param[in] s3_client S3 client
/// @param[in] s3_uri S3 URI
/// @return Object size or LogError on failure
[[nodiscard]] LogExpected<size_t> s3_get_object_size(const Aws::S3::S3Client& s3_client, const LogUri& s3_uri);

/// Read an object from S3
/// @param[in] memory_resource Memory resource
/// @param[in] s3_client S3 client
/// @param[in] s3_uri S3 URI
/// @param[in] offset File offset bytes
/// @param[in] length Number of bytes to read
[[nodiscard]] LogExpected<std::pmr::vector<std::byte>> s3_get_object(
  jewels::memory::MemoryResource memory_resource,
  const Aws::S3::S3Client& s3_client,
  const LogUri& s3_uri,
  size_t offset,
  size_t length);

/// Delete an S3 object
/// @param[in] s3_client S3 client
/// @param[in] s3_uri S3 URI
/// @return LogError on failure
[[nodiscard]] LogExpected<void> s3_delete_object(const Aws::S3::S3Client& s3_client, const LogUri& s3_uri);

} // namespace clockwork_logging::offboard
