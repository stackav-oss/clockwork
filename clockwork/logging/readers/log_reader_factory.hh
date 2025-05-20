// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/types.hh"

#include <memory>
#include <optional>
#include <string_view>

namespace clockwork_logging
{

/// Construct the log reader based on the uri. This will create the correct log
/// reader implementation based on the uri.
/// @param[in] log_uri Log URI
/// @param[in] maybe_log_interval The interval to read from the log
/// @param[in] maybe_relative_interval The interval to read from the log relative to the sart of the log
/// @param[in] decompress_option Option for whether to decompress lite-compressed messages found in the log
/// @return Log reader pointer
std::unique_ptr<AbstractLogReader> make_reader(
  std::string_view log_uri,
  std::optional<LogInterval> maybe_log_interval,
  std::optional<RelativeInterval> maybe_relative_interval,
  DecompressOption decompress_option = DecompressOption::decompress);

} // namespace clockwork_logging
