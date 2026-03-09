// IWYU pragma: private, include "clockwork/logging/log_error.hh"
#pragma once

#include "clockwork/logging/log_error.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/std/expected.hh"

#include <aws/s3/S3Errors.h>
#include <wise_enum.h>

#include <cerrno>
#include <iosfwd>
#include <ostream>
#include <string_view>

namespace clockwork_logging
{

[[nodiscard]] LogError to_log_error(jewels::filesystem::ErrorCode error_code)
{
  switch (error_code.value())
  {
  case ENOENT:
    return LogError::no_such_file_or_directory;
  case EFAULT:
    return LogError::bad_address;
  case EBADF:
    return LogError::bad_file_descriptor;
  case EBUSY:
    return LogError::device_or_resource_busy;
  case ENOTEMPTY:
    return LogError::directory_not_empty;
  case EEXIST:
    return LogError::file_exists;
  case EFBIG:
    return LogError::file_too_large;
  case ENAMETOOLONG:
    return LogError::filename_too_long;
  case EINTR:
    return LogError::interrupted;
  case EINVAL:
    return LogError::invalid_argument;
  case EIO:
    return LogError::io_error;
  case EISDIR:
    return LogError::is_a_directory;
  case ENOTDIR:
    return LogError::not_a_directory;
  case EACCES:
    return LogError::permission_denied;
  case EROFS:
    return LogError::read_only_file_system;
  case ENOSPC:
    return LogError::no_space_on_device;
  default:
    return LogError::unspecified_system_error;
  }
}

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

std::ostream& operator<<(std::ostream& ostream, LogError error)
{
  ostream << wise_enum::to_string(error);
  return ostream;
}

template <typename T>
std::ostream& operator<<(std::ostream& ostream, LogExpected<T> result)
{
  if (result)
  {
    ostream << "{?}";
  }
  else
  {
    ostream << wise_enum::to_string(result.error());
  }
  return ostream;
}

std::ostream& operator<<(std::ostream& ostream, jewels::unexpected<LogError> error)
{
  ostream << wise_enum::to_string(error.value());
  return ostream;
}

} // namespace clockwork_logging
