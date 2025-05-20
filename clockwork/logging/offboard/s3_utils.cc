// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/s3_utils.hh"

#include "clockwork/logging/offboard/s3_read_streambuf.hh"
#include "clockwork/logging/offboard/s3_write_streambuf.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <aws/core/utils/HashingUtils.h>
#include <aws/core/utils/Outcome.h>
#include <aws/s3/S3Errors.h>
#include <aws/s3/model/CommonPrefix.h>
#include <aws/s3/model/CompleteMultipartUploadRequest.h>
#include <aws/s3/model/CompletedMultipartUpload.h>
#include <aws/s3/model/CompletedPart.h>
#include <aws/s3/model/CreateMultipartUploadRequest.h>
#include <aws/s3/model/CreateMultipartUploadResult.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/HeadObjectResult.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/ListObjectsV2Result.h>
#include <aws/s3/model/Object.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/UploadPartRequest.h>
#include <aws/s3/model/UploadPartResult.h>
#include <fmt10/format.h>

#include <algorithm>
#include <istream>
#include <memory>
#include <memory_resource>
#include <string>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

namespace
{

/// Convert an S3 error to a LogError
/// @param[in] s3_error S3 Error
/// @return LogError value
[[nodiscard]] LogError to_log_error(Aws::S3::S3Errors s3_error)
{
  switch (s3_error)
  {
  case Aws::S3::S3Errors::INCOMPLETE_SIGNATURE:
    return LogError::s3_incomplete_signature;
  case Aws::S3::S3Errors::INTERNAL_FAILURE:
    return LogError::s3_internal_failure;
  case Aws::S3::S3Errors::INVALID_ACTION:
    return LogError::s3_invalid_action;
  case Aws::S3::S3Errors::INVALID_CLIENT_TOKEN_ID:
    return LogError::s3_invalid_client_token_id;
  case Aws::S3::S3Errors::INVALID_PARAMETER_COMBINATION:
    return LogError::s3_invalid_parameter_combination;
  case Aws::S3::S3Errors::INVALID_QUERY_PARAMETER:
    return LogError::s3_invalid_query_parameter;
  case Aws::S3::S3Errors::INVALID_PARAMETER_VALUE:
    return LogError::s3_invalid_parameter_value;
  case Aws::S3::S3Errors::MISSING_ACTION:
    return LogError::s3_missing_action;
  case Aws::S3::S3Errors::MISSING_AUTHENTICATION_TOKEN:
    return LogError::s3_missing_authentication_token;
  case Aws::S3::S3Errors::MISSING_PARAMETER:
    return LogError::s3_missing_parameter;
  case Aws::S3::S3Errors::OPT_IN_REQUIRED:
    return LogError::s3_opt_in_required;
  case Aws::S3::S3Errors::REQUEST_EXPIRED:
    return LogError::s3_request_expired;
  case Aws::S3::S3Errors::SERVICE_UNAVAILABLE:
    return LogError::s3_service_unavailable;
  case Aws::S3::S3Errors::THROTTLING:
    return LogError::s3_throttling;
  case Aws::S3::S3Errors::VALIDATION:
    return LogError::s3_validation;
  case Aws::S3::S3Errors::ACCESS_DENIED:
    return LogError::s3_access_denied;
  case Aws::S3::S3Errors::RESOURCE_NOT_FOUND:
    return LogError::s3_resource_not_found;
  case Aws::S3::S3Errors::UNRECOGNIZED_CLIENT:
    return LogError::s3_unrecognized_client;
  case Aws::S3::S3Errors::MALFORMED_QUERY_STRING:
    return LogError::s3_malformed_query_string;
  case Aws::S3::S3Errors::NETWORK_CONNECTION:
    return LogError::s3_network_connection;
  case Aws::S3::S3Errors::UNKNOWN:
    return LogError::s3_unknown_error;
  case Aws::S3::S3Errors::BUCKET_ALREADY_EXISTS:
    return LogError::s3_bucket_already_exists;
  case Aws::S3::S3Errors::BUCKET_ALREADY_OWNED_BY_YOU:
    return LogError::s3_bucket_already_owned_by_you;
  case Aws::S3::S3Errors::NO_SUCH_BUCKET:
    return LogError::s3_no_such_bucket;
  case Aws::S3::S3Errors::NO_SUCH_KEY:
    return LogError::s3_no_such_key;
  case Aws::S3::S3Errors::NO_SUCH_UPLOAD:
    return LogError::s3_no_such_upload;
  case Aws::S3::S3Errors::OBJECT_ALREADY_IN_ACTIVE_TIER:
    return LogError::s3_object_already_in_active_tier;
  case Aws::S3::S3Errors::OBJECT_NOT_IN_ACTIVE_TIER:
    return LogError::s3_object_not_in_active_tier;
  case Aws::S3::S3Errors::SLOW_DOWN:
    return LogError::s3_slow_down;
  case Aws::S3::S3Errors::REQUEST_TIME_TOO_SKEWED:
    return LogError::s3_request_time_too_skewed;
  case Aws::S3::S3Errors::INVALID_SIGNATURE:
    return LogError::s3_invalid_signature;
  case Aws::S3::S3Errors::SIGNATURE_DOES_NOT_MATCH:
    return LogError::s3_signature_does_not_match;
  case Aws::S3::S3Errors::INVALID_ACCESS_KEY_ID:
    return LogError::s3_invalid_access_key_id;
  case Aws::S3::S3Errors::REQUEST_TIMEOUT:
    return LogError::s3_request_timeout;
  case Aws::S3::S3Errors::INVALID_OBJECT_STATE:
    return LogError::s3_invalid_object_state;
  }
  return LogError::s3_unknown_error;
}

} // namespace

[[nodiscard]] LogExpected<S3ListObjectsResult> s3_list_objects_v2(
  jewels::memory::MemoryResource memory_resource, const Aws::S3::S3Client& s3_client, const LogUri& s3_uri)
{
  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }

  Aws::S3::Model::ListObjectsV2Request request;
  request.SetBucket(std::string{s3_uri.host()});
  request.SetPrefix(std::string{s3_uri.path().substr(1U)});
  request.SetDelimiter("/");

  const auto outcome = s3_client.ListObjectsV2(request);
  if (!outcome.IsSuccess())
  {
    jewels::log_cerr_error("Failed to list objects under {}: {}", s3_uri.string(), outcome.GetError().GetMessage());
    return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
  }

  S3ListObjectsResult result{};
  result.objects = std::pmr::vector<std::pmr::string>{memory_resource};
  result.prefixes = std::pmr::vector<std::pmr::string>{memory_resource};
  result.objects.reserve(outcome.GetResult().GetContents().size());
  for (const auto& object : outcome.GetResult().GetContents())
  {
    std::pmr::string object_str{object.GetKey(), memory_resource};
    result.objects.push_back(std::move(object_str));
  }
  result.prefixes.reserve(outcome.GetResult().GetCommonPrefixes().size());
  for (const auto& prefix : outcome.GetResult().GetCommonPrefixes())
  {
    std::pmr::string prefix_str{prefix.GetPrefix(), memory_resource};
    result.prefixes.push_back(std::move(prefix_str));
  }
  return {std::move(result)};
}

[[nodiscard]] LogExpected<std::pmr::string> s3_create_multipart_upload(
  jewels::memory::MemoryResource memory_resource, const Aws::S3::S3Client& s3_client, const LogUri& s3_uri)
{
  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }

  Aws::S3::Model::CreateMultipartUploadRequest request;
  request.SetBucket(std::string{s3_uri.host()});
  request.SetKey(std::string{s3_uri.path().substr(1U)});
  request.SetContentType("binary/octet-stream");

  const auto outcome = s3_client.CreateMultipartUpload(request);
  if (!outcome.IsSuccess())
  {
    jewels::log_cerr_error(
      "Failed to create multipart upload for {}: {}", s3_uri.string(), outcome.GetError().GetMessage());
    return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
  }
  return std::pmr::string{outcome.GetResult().GetUploadId(), memory_resource};
}

[[nodiscard]] LogExpected<Aws::S3::Model::CompletedPart> s3_upload_part(
  const Aws::S3::S3Client& s3_client,
  const LogUri& s3_uri,
  std::string_view upload_id,
  int32_t part_number,
  std::pmr::vector<std::pmr::vector<std::byte>> buffers)
{
  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }

  S3WriteStreambuf sbuf{std::move(buffers)};
  const auto ios_ptr = std::make_shared<std::iostream>(&sbuf);
  const auto md5_sum = Aws::Utils::HashingUtils::CalculateMD5(*ios_ptr);
  ios_ptr->clear();
  ios_ptr->seekg(0);

  Aws::S3::Model::UploadPartRequest request;
  request.SetBucket(std::string{s3_uri.host()});
  request.SetKey(std::string{s3_uri.path().substr(1U)});
  request.SetUploadId(std::string{upload_id});
  request.SetPartNumber(part_number);
  request.SetContentLength(static_cast<int64_t>(sbuf.total_size()));
  request.SetContentMD5(Aws::Utils::HashingUtils::Base64Encode(md5_sum));
  request.SetBody(ios_ptr);
  request.SetRequestRetryHandler(
    [&ios_ptr](const auto&)
    {
      ios_ptr->clear();
      ios_ptr->seekg(0);
    });

  const auto outcome = s3_client.UploadPart(request);
  if (!outcome.IsSuccess())
  {
    jewels::log_cerr_error(
      "Failed to upload part {} for {}: {}", part_number, s3_uri.string(), outcome.GetError().GetMessage());
    return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
  }
  Aws::S3::Model::CompletedPart completed_part;
  completed_part.SetPartNumber(part_number);
  completed_part.SetETag(outcome.GetResult().GetETag());
  return {std::move(completed_part)};
}

[[nodiscard]] LogExpected<void> s3_complete_multipart_upload(
  const Aws::S3::S3Client& s3_client,
  const LogUri& s3_uri,
  std::string_view upload_id,
  std::span<Aws::S3::Model::CompletedPart> completed_parts)
{
  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }

  std::ranges::sort(
    completed_parts, [](const auto& lhs, const auto& rhs) { return lhs.GetPartNumber() < rhs.GetPartNumber(); });

  Aws::S3::Model::CompleteMultipartUploadRequest request;
  request.SetBucket(std::string{s3_uri.host()});
  request.SetKey(std::string{s3_uri.path().substr(1U)});
  request.SetUploadId(std::string{upload_id});
  Aws::S3::Model::CompletedMultipartUpload completed_upload;
  for (const auto& completed_part : completed_parts)
  {
    completed_upload.AddParts(completed_part);
  }
  request.SetMultipartUpload(std::move(completed_upload));

  const auto outcome = s3_client.CompleteMultipartUpload(request);
  if (!outcome.IsSuccess())
  {
    const auto error = to_log_error(outcome.GetError().GetErrorType());
    if (error != LogError::s3_no_such_upload)
    {
      jewels::log_cerr_error(
        "Failed to complete multipart upload for {}: {}", s3_uri.string(), outcome.GetError().GetMessage());
      return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
    }
  }
  return {};
}

[[nodiscard]] LogExpected<void> s3_put_object(
  const Aws::S3::S3Client& s3_client, const LogUri& s3_uri, std::pmr::vector<std::pmr::vector<std::byte>> buffers)
{
  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }

  S3WriteStreambuf sbuf{std::move(buffers)};
  const auto ios_ptr = std::make_shared<std::iostream>(&sbuf);
  const auto md5_sum = Aws::Utils::HashingUtils::CalculateMD5(*ios_ptr);

  Aws::S3::Model::PutObjectRequest request;
  request.SetBucket(std::string{s3_uri.host()});
  request.SetKey(std::string{s3_uri.path().substr(1U)});
  request.SetContentType("binary/octet-stream");
  request.SetContentLength(static_cast<int64_t>(sbuf.total_size()));
  request.SetContentMD5(Aws::Utils::HashingUtils::Base64Encode(md5_sum));
  request.SetBody(ios_ptr);
  request.SetRequestRetryHandler(
    [&ios_ptr](const auto&)
    {
      ios_ptr->clear();
      ios_ptr->seekg(0);
    });

  const auto outcome = s3_client.PutObject(request);
  if (!outcome.IsSuccess())
  {
    jewels::log_cerr_error("Failed to put object {}: {}", s3_uri.string(), outcome.GetError().GetMessage());
    return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
  }
  return {};
}

[[nodiscard]] LogExpected<size_t> s3_get_object_size(const Aws::S3::S3Client& s3_client, const LogUri& s3_uri)
{
  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }

  Aws::S3::Model::HeadObjectRequest request;
  request.SetBucket(std::string{s3_uri.host()});
  request.SetKey(std::string{s3_uri.path().substr(1U)});

  const auto outcome = s3_client.HeadObject(request);
  if (!outcome.IsSuccess())
  {
    jewels::log_cerr_info("Failed to get object attributes {}: {}", s3_uri.string(), outcome.GetError().GetMessage());
    return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
  }

  return static_cast<size_t>(outcome.GetResult().GetContentLength());
}

[[nodiscard]] LogExpected<void> s3_delete_object(const Aws::S3::S3Client& s3_client, const LogUri& s3_uri)
{
  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }

  Aws::S3::Model::DeleteObjectRequest request;
  request.SetBucket(std::string{s3_uri.host()});
  request.SetKey(std::string{s3_uri.path().substr(1U)});

  const auto outcome = s3_client.DeleteObject(request);
  if (!outcome.IsSuccess())
  {
    jewels::log_cerr_info("Failed to delete object {}: {}", s3_uri.string(), outcome.GetError().GetMessage());
    return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
  }
  return {};
}

[[nodiscard]] LogExpected<std::pmr::vector<std::byte>> s3_get_object(
  jewels::memory::MemoryResource memory_resource,
  const Aws::S3::S3Client& s3_client,
  const LogUri& s3_uri,
  size_t offset,
  size_t length)
{
  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }

  std::pmr::vector<std::byte> buffer(length, std::byte{0U}, memory_resource);
  S3ReadStreambuf sbuf(buffer);
  // std::iostream ios{&sbuf};

  const auto range_str = fmt::format("bytes={}-{}", offset, offset + length - 1U);

  Aws::S3::Model::GetObjectRequest request;
  request.SetBucket(std::string{s3_uri.host()});
  request.SetKey(std::string{s3_uri.path().substr(1U)});
  request.SetRange(range_str);
  request.SetResponseStreamFactory(
    [&sbuf]()
    {
      sbuf.pubseekpos(0, std::ios_base::out);
      return std::make_unique<std::iostream>(&sbuf).release();
    });
  request.SetRequestRetryHandler([&sbuf](const auto&) { sbuf.pubseekpos(0, std::ios_base::out); });

  const auto outcome = s3_client.GetObject(request);
  if (!outcome.IsSuccess())
  {
    jewels::log_cerr_info(
      "Failed to read ({}:{}) from {}: {}", offset, length, s3_uri.string(), outcome.GetError().GetMessage());
    return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
  }

  return {std::move(buffer)};
}

} // namespace clockwork_logging::offboard
