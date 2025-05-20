// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/filesystem/error_code.hh"
#include "jewels/std/expected.hh"

#include <wise_enum.h>

#include <cstdint>
#include <ostream>

namespace clockwork_logging
{

/// Errors returned by the logging code
WISE_ENUM_CLASS(
  (LogError, uint8_t),
  /// Out of memory
  out_of_memory,
  /// Caught unhandled exception
  unhandled_exception,
  /// Unable to access log device
  bad_log_device,
  /// Missing protobuf file
  missing_protobuf_file,
  /// Invalid protobuf file
  invalid_protobuf_file,
  /// Log write error
  write_error,
  /// Log is not open
  not_open,
  /// Log is paused
  paused,
  /// Log is not paused
  not_paused,
  /// Log is already logging
  already_open,
  /// Log already exists
  log_already_exists,
  /// Unrecoverable log error
  failed,
  /// Log read error
  read_error,
  /// Log read reached end of log
  end_of_log,
  /// Unknown channel
  unknown_channel,
  /// Message dropped
  message_dropped,
  // Failed to create the log directory
  failed_to_create_log_directory,
  // Failed to queue async write
  failed_to_queue_async_write,
  // Failed to allocate an async request
  failed_to_allocate_async_request,
  // Failed to initialize I/O ring
  failed_to_initialize_io_ring,
  // Failed to open log file
  failed_to_open_log_file,
  // Failed to close log file
  failed_to_close_log_file,
  // io_uring_get_sqe failed
  io_uring_get_sqe_failed,
  // Async writes completed out of order
  async_writes_completed_out_of_order,
  // Unrecoverable write failure
  unrecoverable_write_error,
  // Failed to flush buffered data to the log
  async_flush_failed,
  // Dropped message because the max write backlog was exceeded
  max_write_backlog_exceeded,
  // Failed to allocate a data buffer
  failed_to_allocate_data_buffer,
  // Async write failed
  async_write_error,
  // Failed to add a buffer to an async write request
  failed_to_add_buffer_to_request,
  // Failed to write record that was larger than the max record size
  record_length_exceeds_max_record_size,
  // Failed to write schema name longer than the max name size
  schema_name_exceeds_max_name_size,
  // Failed to write channel name longer than the max name size
  channel_name_exceeds_max_name_size,
  // Schema definition exceeds max size
  schema_definition_exceeds_max_size,
  // Close for log split failed
  close_for_split_log_failed,
  // Missing channel metadata
  missing_channel_metadata,
  // Message header exceeds max size
  message_header_exceeds_max_size,
  // Failed to pad for zero copy
  failed_to_pad_for_zero_copy,
  // Failed to get initial log file sequence number
  failed_to_get_initial_log_file_sequence_number,
  // Bad address
  bad_address,
  // Bad file descriptor
  bad_file_descriptor,
  // Device busy
  device_or_resource_busy,
  // Directory not empty
  directory_not_empty,
  // File exists
  file_exists,
  // File too large
  file_too_large,
  // Filename too long
  filename_too_long,
  // Interupted
  interrupted,
  // Invalid argument
  invalid_argument,
  // I/O error
  io_error,
  // Is a directory
  is_a_directory,
  // Not a directory
  not_a_directory,
  // Permission denied
  permission_denied,
  // Read only file system
  read_only_file_system,
  // No such file or directory
  no_such_file_or_directory,
  // Unspecidied system error
  unspecified_system_error,
  // Not implemented
  not_implemented,
  // Log already closed
  already_closed,
  // No space on device
  no_space_on_device,
  // Read returned less bytes than expected
  short_read,
  // Invalid file chunk
  invalid_file_chunk,
  // Compression failure
  compression_failure,
  // Decompression failure
  decompression_failure,
  // Message data exceeds max size
  message_data_exceeds_max_size,
  // End of log file chunk
  end_of_chunk,
  // Invalid log file
  invalid_log_uri,
  // Channel already exists
  channel_already_exists,
  // Not a log
  not_a_log,
  // Channel metadata mismatch
  metadata_mismatch,
  // Failed to load log metrics
  failed_to_load_metrics,
  // Failed to write the end log file record
  failed_to_write_end_log_file_record,
  // S3 INCOMPLETE_SIGNATURE error
  s3_incomplete_signature,
  // S3 INTERNAL_FAILURE error
  s3_internal_failure,
  // S3 INVALID_ACTION error
  s3_invalid_action,
  // S3 INVALID_CLIENT_TOKEN_ID error
  s3_invalid_client_token_id,
  // S3 INVALID_PARAMETER_COMBINATION error
  s3_invalid_parameter_combination,
  // S3 INVALID_QUERY_PARAMETER error
  s3_invalid_query_parameter,
  // S3 INVALID_PARAMETER_VALUE error
  s3_invalid_parameter_value,
  // S3 MISSING_ACTION error
  s3_missing_action,
  // S3 MISSING_AUTHENTICATION_TOKEN error
  s3_missing_authentication_token,
  // S3 MISSING_PARAMETER error
  s3_missing_parameter,
  // S3 OPT_IN_REQUIRED error
  s3_opt_in_required,
  // S3 REQUEST_EXPIRED error
  s3_request_expired,
  // S3 SERVICE_UNAVAILABLE error
  s3_service_unavailable,
  // S3 THROTTLING error
  s3_throttling,
  // S3 VALIDATION error
  s3_validation,
  // S3 ACCESS_DENIED error
  s3_access_denied,
  // S3 RESOURCE_NOT_FOUND error
  s3_resource_not_found,
  // S3 UNRECOGNIZED_CLIENT error
  s3_unrecognized_client,
  // S3 MALFORMED_QUERY_STRING error
  s3_malformed_query_string,
  // S3 NETWORK_CONNECTION error
  s3_network_connection,
  // S3 UNKNOWN error
  s3_unknown_error,
  // S3 BUCKET_ALREADY_EXISTS: error
  s3_bucket_already_exists,
  // S3 BUCKET_ALREADY_OWNED_BY_YOU,  error
  s3_bucket_already_owned_by_you,
  // S3 NO_SUCH_BUCKET, error
  s3_no_such_bucket,
  // S3 NO_SUCH_KEY,  error
  s3_no_such_key,
  // S3 NO_SUCH_UPLOAD,  error
  s3_no_such_upload,
  // S3 OBJECT_ALREADY_IN_ACTIVE_TIER,  error
  s3_object_already_in_active_tier,
  // S3 OBJECT_NOT_IN_ACTIVE_TIER error
  s3_object_not_in_active_tier,
  // S3 SLOW_DOWN error
  s3_slow_down,
  // S3 REQUEST_TIME_TOO_SKEWED error
  s3_request_time_too_skewed,
  // S3 INVALID_SIGNATURE error
  s3_invalid_signature,
  // S3 SIGNATURE_DOES_NOT_MATCH error
  s3_signature_does_not_match,
  // S3INVALID_ACCESS_KEY_ID error
  s3_invalid_access_key_id,
  // S3 REQUEST_TIMEOUT error
  s3_request_timeout,
  // S3 INVALID_OBJECT_STATE error
  s3_invalid_object_state,
  // Failed to load the vehicle configuration
  failed_to_load_vehicle_config,
  // Failed to deserialize configuration
  failed_to_deserialize_config,
  // Metadata map is disabled
  metadata_map_disabled,
  // Empty metadata file
  empty_metadata_file,
  // Channel is not persistent
  not_persistent,
  // Logger is currently in a logging state
  is_logging,
  // Internal clockwork error
  clockwork_error,
  // Empty log file
  empty_log_file,
  // Unsupported compression type
  unsupported_compression_type,
  // Not initialized
  not_initialized,
  // Bad UUID
  bad_uuid,
  // Buffer full
  buffer_full,
  // Already initialized
  already_initialized,
  // Invalid telemetry log
  invalid_telemetry_log,
  // Log not found
  log_not_found,
  // RPC call failed
  rpc_call_failed)

/// Logging expected type
/// @tparam T Expected return type
template <typename T>
using LogExpected = jewels::expected<T, LogError>;

/// Convert a system error code to a LogError
/// @param[in] error_code System error code
/// @return LogError corresponding to the system error
[[nodiscard]] inline LogError to_log_error(jewels::filesystem::ErrorCode error_code);

/// Output stream insertion operator for log errors
/// @param[in] ostream Output stream
/// @param[in] error Log error value
/// @return Output stream reference
inline std::ostream& operator<<(std::ostream& ostream, LogError error);

/// Output stream insertion operator for log expected
/// @param[in] ostream Output stream
/// @param[in] result Log expected result value
/// @return Output stream reference
template <typename T>
std::ostream& operator<<(std::ostream& ostream, LogExpected<T> result);

/// Output stream insertion operator for jewels::unexpected<LogError>
/// @param[in] ostream Output stream
/// @param[in] error Unexpected value
/// @return Output stream reference
inline std::ostream& operator<<(std::ostream& ostream, jewels::unexpected<LogError> error);

} // namespace clockwork_logging

#include "clockwork/logging/log_error.inl"
