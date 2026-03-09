// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace clockwork_logging::offboard
{

/// Return whether verbose logging is enabled for the S3 interface
/// @return True if verbose logging is enabled
[[nodiscard]] bool s3_logging_is_enabled();

/// Function to initialize the AWS API runtime
void init_aws_api();

} // namespace clockwork_logging::offboard
