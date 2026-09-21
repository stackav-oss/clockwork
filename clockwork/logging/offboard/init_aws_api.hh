// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace clockwork_logging::offboard
{

/// Return whether verbose logging is enabled for the S3 interface
/// @return True if verbose logging is enabled
[[nodiscard]] bool s3_logging_is_enabled();

/// Function to initialize the AWS API runtime
///
/// Note: To avoid panics at shutdown init_aws_api should always be called
///       from the main thread. Any code that is reading/writing to S3 from
///       worker threads should always call init_aws_api explicitly from the
///       main thread.
///
void init_aws_api();

} // namespace clockwork_logging::offboard
