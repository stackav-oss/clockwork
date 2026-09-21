// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/s3_otel_utils.hh"

#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/s3_read_streambuf.hh"
#include "clockwork/logging/offboard/s3_write_streambuf.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/otel/otel.hh"
#include "jewels/std/expected.hh"

#include <aws/core/client/AWSError.h>
#include <aws/core/utils/HashingUtils.h>
#include <aws/core/utils/Outcome.h>
#include <aws/s3/S3Client.h>
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
#include <fmt/format.h>
#include <opentelemetry/trace/scope.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_metadata.h>
#include <opentelemetry/trace/tracer.h>

#include <algorithm>
#include <istream>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

namespace
{

/// Tracer name for observability
constexpr auto tracer_name = "offboard.logging";

/// Tracer URI attribute
constexpr auto uri_attribute = "uri";

/// Tracer content length attribute
constexpr auto content_length_attribute = "content-length";

/// Tracer offset attribute
constexpr auto offset_attribute = "offset";

/// Tracer upload ID attribute
constexpr auto upload_id_attribute = "upload-id";

/// Tracer part number attribute
constexpr auto part_number_attribute = "part-number";

} // namespace

S3OtelUtils::S3OtelUtils(
  jewels::memory::MemoryResource memory_resource, std::shared_ptr<Aws::S3::S3Client> s3_client_ptr)
  : memory_resource_(std::move(memory_resource)), s3_client_ptr_(std::move(s3_client_ptr))
{
}

[[nodiscard]] LogExpected<S3ListObjectsResult> S3OtelUtils::list_objects_v2(const LogUri& s3_uri) const
{
  auto tracer = jewels::otel::get_tracer(tracer_name);
  auto span = tracer->StartSpan("ListObjectsV2", {{uri_attribute, s3_uri.string()}});
  auto scope = opentelemetry::trace::Scope(span);

  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    jewels::log_cerr_error("Failed to list objects under {}: invalid_log_uri", s3_uri.string());
    span->SetStatus(opentelemetry::trace::StatusCode::kError, "invalid_log_uri");
    return jewels::unexpected(LogError::invalid_log_uri);
  }

  Aws::S3::Model::ListObjectsV2Request request;
  request.SetBucket(std::string{s3_uri.host()});
  request.SetPrefix(std::string{s3_uri.path().substr(1U)});
  request.SetDelimiter("/");

  const auto outcome = s3_client_ptr_->ListObjectsV2(request);
  if (!outcome.IsSuccess())
  {
    jewels::log_cerr_error("Failed to list objects under {}: {}", s3_uri.string(), outcome.GetError().GetMessage());
    span->SetStatus(opentelemetry::trace::StatusCode::kError, outcome.GetError().GetMessage());
    return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
  }

  S3ListObjectsResult result{};
  result.objects = std::pmr::vector<std::pmr::string>{memory_resource_};
  result.prefixes = std::pmr::vector<std::pmr::string>{memory_resource_};
  result.objects.reserve(outcome.GetResult().GetContents().size());
  for (const auto& object : outcome.GetResult().GetContents())
  {
    std::pmr::string object_str{object.GetKey(), memory_resource_};
    result.objects.push_back(std::move(object_str));
  }
  result.prefixes.reserve(outcome.GetResult().GetCommonPrefixes().size());
  for (const auto& prefix : outcome.GetResult().GetCommonPrefixes())
  {
    std::pmr::string prefix_str{prefix.GetPrefix(), memory_resource_};
    result.prefixes.push_back(std::move(prefix_str));
  }

  span->SetStatus(opentelemetry::trace::StatusCode::kOk);

  return {std::move(result)};
}

[[nodiscard]] LogExpected<std::pmr::string> S3OtelUtils::create_multipart_upload(const LogUri& s3_uri) const
{
  auto tracer = jewels::otel::get_tracer(tracer_name);
  auto span = tracer->StartSpan("CreateMultipartUpload", {{uri_attribute, s3_uri.string()}});
  auto scope = opentelemetry::trace::Scope(span);

  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    jewels::log_cerr_error("Failed to create multipart upload for {}: invalid_log_uri", s3_uri.string());
    span->SetStatus(opentelemetry::trace::StatusCode::kError, "invalid_log_uri");
    return jewels::unexpected(LogError::invalid_log_uri);
  }

  Aws::S3::Model::CreateMultipartUploadRequest request;
  request.SetBucket(std::string{s3_uri.host()});
  request.SetKey(std::string{s3_uri.path().substr(1U)});
  request.SetContentType("binary/octet-stream");

  const auto outcome = s3_client_ptr_->CreateMultipartUpload(request);
  if (!outcome.IsSuccess())
  {
    jewels::log_cerr_error(
      "Failed to create multipart upload for {}: {}", s3_uri.string(), outcome.GetError().GetMessage());
    span->SetStatus(opentelemetry::trace::StatusCode::kError, outcome.GetError().GetMessage());
    return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
  }

  span->SetStatus(opentelemetry::trace::StatusCode::kOk);

  return std::pmr::string{outcome.GetResult().GetUploadId(), memory_resource_};
}

[[nodiscard]] LogExpected<Aws::S3::Model::CompletedPart> S3OtelUtils::upload_part(
  const LogUri& s3_uri,
  std::string_view upload_id,
  int32_t part_number,
  std::pmr::vector<std::pmr::vector<std::byte>> buffers) const
{
  S3WriteStreambuf sbuf{std::move(buffers)};
  const auto content_length = sbuf.total_size();

  auto tracer = jewels::otel::get_tracer(tracer_name);
  auto span = tracer->StartSpan(
    "UploadPart",
    {{uri_attribute, s3_uri.string()},
     {upload_id_attribute, upload_id},
     {part_number_attribute, part_number},
     {content_length_attribute, content_length}});
  auto scope = opentelemetry::trace::Scope(span);

  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    jewels::log_cerr_error("Failed to create upload part for {}: invalid_log_uri", s3_uri.string());
    span->SetStatus(opentelemetry::trace::StatusCode::kError, "invalid_log_uri");
    return jewels::unexpected(LogError::invalid_log_uri);
  }

  const auto ios_ptr = std::make_shared<std::iostream>(&sbuf);
  const auto md5_sum = Aws::Utils::HashingUtils::CalculateMD5(*ios_ptr);
  ios_ptr->clear();
  ios_ptr->seekg(0);

  Aws::S3::Model::UploadPartRequest request;
  request.SetBucket(std::string{s3_uri.host()});
  request.SetKey(std::string{s3_uri.path().substr(1U)});
  request.SetUploadId(std::string{upload_id});
  request.SetPartNumber(part_number);
  request.SetContentLength(static_cast<int64_t>(content_length));
  request.SetContentMD5(Aws::Utils::HashingUtils::Base64Encode(md5_sum));
  request.SetBody(ios_ptr);
  request.SetRequestRetryHandler(
    [&ios_ptr](const auto&)
    {
      ios_ptr->clear();
      ios_ptr->seekg(0);
    });

  const auto outcome = s3_client_ptr_->UploadPart(request);
  if (!outcome.IsSuccess())
  {
    jewels::log_cerr_error(
      "Failed to upload part {} for {}: {}", part_number, s3_uri.string(), outcome.GetError().GetMessage());
    span->SetStatus(opentelemetry::trace::StatusCode::kError, outcome.GetError().GetMessage());
    return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
  }
  Aws::S3::Model::CompletedPart completed_part;
  completed_part.SetPartNumber(part_number);
  completed_part.SetETag(outcome.GetResult().GetETag());

  span->SetStatus(opentelemetry::trace::StatusCode::kOk);

  return {std::move(completed_part)};
}

[[nodiscard]] LogExpected<void> S3OtelUtils::complete_multipart_upload(
  const LogUri& s3_uri, std::string_view upload_id, std::span<Aws::S3::Model::CompletedPart> completed_parts) const
{
  auto tracer = jewels::otel::get_tracer(tracer_name);
  auto span =
    tracer->StartSpan("CompleteMultipartUpload", {{uri_attribute, s3_uri.string()}, {upload_id_attribute, upload_id}});
  auto scope = opentelemetry::trace::Scope(span);

  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    jewels::log_cerr_error("Failed to complete multipart upload part for {}: invalid_log_uri", s3_uri.string());
    span->SetStatus(opentelemetry::trace::StatusCode::kError, "invalid_log_uri");
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

  const auto outcome = s3_client_ptr_->CompleteMultipartUpload(request);
  if (!outcome.IsSuccess())
  {
    const auto error = to_log_error(outcome.GetError().GetErrorType());
    if (error != LogError::s3_no_such_upload)
    {
      jewels::log_cerr_error(
        "Failed to complete multipart upload for {}: {}", s3_uri.string(), outcome.GetError().GetMessage());
      span->SetStatus(opentelemetry::trace::StatusCode::kError, outcome.GetError().GetMessage());
      return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
    }
  }

  span->SetStatus(opentelemetry::trace::StatusCode::kOk);

  return {};
}

[[nodiscard]] LogExpected<void>
S3OtelUtils::put_object(const LogUri& s3_uri, std::pmr::vector<std::pmr::vector<std::byte>> buffers) const
{
  S3WriteStreambuf sbuf{std::move(buffers)};
  const auto content_length = sbuf.total_size();

  auto tracer = jewels::otel::get_tracer(tracer_name);
  auto span =
    tracer->StartSpan("PutObject", {{uri_attribute, s3_uri.string()}, {content_length_attribute, content_length}});
  auto scope = opentelemetry::trace::Scope(span);

  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    jewels::log_cerr_error("Failed to put object for {}: invalid_log_uri", s3_uri.string());
    span->SetStatus(opentelemetry::trace::StatusCode::kError, "invalid_log_uri");
    return jewels::unexpected(LogError::invalid_log_uri);
  }

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

  const auto outcome = s3_client_ptr_->PutObject(request);
  if (!outcome.IsSuccess())
  {
    jewels::log_cerr_error("Failed to put object {}: {}", s3_uri.string(), outcome.GetError().GetMessage());
    span->SetStatus(opentelemetry::trace::StatusCode::kError, outcome.GetError().GetMessage());
    return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
  }

  span->SetStatus(opentelemetry::trace::StatusCode::kOk);

  return {};
}

[[nodiscard]] LogExpected<size_t> S3OtelUtils::get_object_size(const LogUri& s3_uri) const
{
  auto tracer = jewels::otel::get_tracer(tracer_name);
  auto span = tracer->StartSpan("HeadObject", {{uri_attribute, s3_uri.string()}});
  auto scope = opentelemetry::trace::Scope(span);

  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    jewels::log_cerr_error("Failed to get object size for {}: invalid_log_uri", s3_uri.string());
    span->SetStatus(opentelemetry::trace::StatusCode::kError, "invalid_log_uri");
    return jewels::unexpected(LogError::invalid_log_uri);
  }

  Aws::S3::Model::HeadObjectRequest request;
  request.SetBucket(std::string{s3_uri.host()});
  request.SetKey(std::string{s3_uri.path().substr(1U)});

  const auto outcome = s3_client_ptr_->HeadObject(request);
  if (!outcome.IsSuccess())
  {
    jewels::log_cerr_info("Failed to get object attributes {}: {}", s3_uri.string(), outcome.GetError().GetMessage());
    span->SetStatus(opentelemetry::trace::StatusCode::kError, outcome.GetError().GetMessage());
    return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
  }

  span->SetAttribute(content_length_attribute, outcome.GetResult().GetContentLength());
  span->SetStatus(opentelemetry::trace::StatusCode::kOk);

  return static_cast<size_t>(outcome.GetResult().GetContentLength());
}

[[nodiscard]] LogExpected<void> S3OtelUtils::delete_object(const LogUri& s3_uri) const
{
  auto tracer = jewels::otel::get_tracer(tracer_name);
  auto span = tracer->StartSpan("DeleteObject", {{uri_attribute, s3_uri.string()}});
  auto scope = opentelemetry::trace::Scope(span);

  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    jewels::log_cerr_error("Failed to delete object for {}: invalid_log_uri", s3_uri.string());
    span->SetStatus(opentelemetry::trace::StatusCode::kError, "invalid_log_uri");
    return jewels::unexpected(LogError::invalid_log_uri);
  }

  Aws::S3::Model::DeleteObjectRequest request;
  request.SetBucket(std::string{s3_uri.host()});
  request.SetKey(std::string{s3_uri.path().substr(1U)});

  const auto outcome = s3_client_ptr_->DeleteObject(request);
  if (!outcome.IsSuccess())
  {
    jewels::log_cerr_info("Failed to delete object {}: {}", s3_uri.string(), outcome.GetError().GetMessage());
    span->SetStatus(opentelemetry::trace::StatusCode::kError, outcome.GetError().GetMessage());
    return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
  }

  span->SetStatus(opentelemetry::trace::StatusCode::kOk);

  return {};
}

[[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
S3OtelUtils::get_object(const LogUri& s3_uri, size_t offset, size_t length) const
{
  std::pmr::vector<std::byte> buffer(length, std::byte{0U}, memory_resource_);
  const auto get_result = get_object(s3_uri, offset, buffer);
  if (!get_result)
  {
    return jewels::unexpected(get_result.error());
  }
  return {std::move(buffer)};
}

[[nodiscard]] LogExpected<std::span<std::byte>>
S3OtelUtils::get_object(const LogUri& s3_uri, size_t offset, std::span<std::byte> buffer_span) const
{
  auto tracer = jewels::otel::get_tracer(tracer_name);
  auto span = tracer->StartSpan(
    "GetObject",
    {{uri_attribute, s3_uri.string()}, {offset_attribute, offset}, {content_length_attribute, buffer_span.size()}});
  auto scope = opentelemetry::trace::Scope(span);

  if (s3_uri.scheme() != LogUriScheme::s3)
  {
    jewels::log_cerr_error("Failed to get object for {}: invalid_log_uri", s3_uri.string());
    span->SetStatus(opentelemetry::trace::StatusCode::kError, "invalid_log_uri");
    return jewels::unexpected(LogError::invalid_log_uri);
  }

  S3ReadStreambuf sbuf(buffer_span);

  const auto range_str = fmt::format("bytes={}-{}", offset, offset + buffer_span.size() - 1U);

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

  const auto outcome = s3_client_ptr_->GetObject(request);
  if (!outcome.IsSuccess())
  {
    jewels::log_cerr_info(
      "Failed to read ({}:{}) from {}: {}",
      offset,
      buffer_span.size(),
      s3_uri.string(),
      outcome.GetError().GetMessage());
    span->SetStatus(opentelemetry::trace::StatusCode::kError, outcome.GetError().GetMessage());
    return jewels::unexpected(to_log_error(outcome.GetError().GetErrorType()));
  }

  span->SetStatus(opentelemetry::trace::StatusCode::kOk);

  return buffer_span;
}

void S3OtelUtils::retry_callback(
  const Aws::Client::AWSError<Aws::Client::CoreErrors>& error, int64_t attempted_retries, bool should_retry)
{
  auto tracer = jewels::otel::get_tracer("offboard.logging");
  auto span = tracer->StartSpan(
    "ShouldRetry",
    {{"attempted_retries", attempted_retries}, {"error_message", error.GetMessage()}, {"should_retry", should_retry}});
  span->SetStatus(opentelemetry::trace::StatusCode::kOk);
}

} // namespace clockwork_logging::offboard
