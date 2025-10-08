// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/channel_publisher_config.hh"
#include "clockwork/logging/log_writer_config.hh"
#include "clockwork/logging/writers/channel_message_rates_config.hh"
#include "clockwork/logging/writers/logger_config.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/tools/metrics_channel_metadata/metrics_channel_metadata_config.hh"

#include <memory>
#include <string_view>

namespace clockwork_logging::tests
{

/// Generate a log writer configuration for unit tests
/// @return Log writer configuration
[[nodiscard]] std::unique_ptr<LogWriterConfigTap> get_test_log_writer_config();

/// Generate a logger configuration for unit tests
/// @param[in] log_root_dir Log root directory
/// @param[in] pinion_shm_root Pinion shared memory root directory
/// @return Logger configuration
[[nodiscard]] std::unique_ptr<LoggerConfigTap>
get_test_logger_config(std::string_view log_root_dir, std::string_view pinion_shm_root);

/// Generate a channel message rates configuration for unit tests
/// @return Channel message rates  configuration
[[nodiscard]] std::unique_ptr<clockwork::Tappy<ChannelMessageRatesConfig>> get_test_channel_message_rates_config();

/// Generate a channel publisher configuration for unit tests
/// @return Channel publisher configuration
[[nodiscard]] std::unique_ptr<ChannelPublisherConfigTap> get_test_channel_publisher_config();

[[nodiscard]] std::shared_ptr<const clockwork::tools::MetricsChannelMetadataConfigTap>
get_test_metrics_channel_metadata_config();

} // namespace clockwork_logging::tests
