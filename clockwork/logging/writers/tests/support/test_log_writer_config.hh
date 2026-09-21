// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/channel_publisher_config_clk_cc.hh"
#include "clockwork/logging/log_writer_config_clk_cc.hh"
#include "clockwork/logging/writers/channel_message_rates_config_clk_cc.hh"
#include "clockwork/logging/writers/logger_config_clk_cc.hh"
#include "clockwork/logging/writers/persistent_log_entry.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/tools/metrics_channel_metadata/metrics_channel_metadata_config_clk_cc.hh"
#include "jewels/memory/memory_resource.hh"

#include <memory>
#include <string_view>
#include <vector>

namespace clockwork_logging::tests
{

/// Generate a log writer configuration for unit tests
/// @return Log writer configuration
[[nodiscard]] std::shared_ptr<clockwork::Tappy<LogWriterConfig<>>> get_test_log_writer_config();

/// Generate a logger configuration for unit tests
/// @param[in] log_root_dir Log root directory
/// @param[in] pinion_shm_root Pinion shared memory root directory
/// @return Logger configuration
[[nodiscard]] std::shared_ptr<clockwork::Tappy<LoggerConfig>>
get_test_logger_config(std::string_view log_root_dir, std::string_view pinion_shm_root);

/// Generate a channel message rates configuration for unit tests
/// @return Channel message rates  configuration
[[nodiscard]] std::shared_ptr<clockwork::Tappy<ChannelMessageRatesConfig>> get_test_channel_message_rates_config();

/// Generate a channel publisher configuration for unit tests
/// @return Channel publisher configuration
[[nodiscard]] std::shared_ptr<clockwork::Tappy<ChannelPublisherConfig<>>> get_test_channel_publisher_config();

/// Generate a metrics channel metadata configuration for unit tests
/// @return Metrics channel metadata configuration
std::shared_ptr<const clockwork::Tappy<clockwork::tools::MetricsChannelMetadataConfig<>>>
get_test_metrics_channel_metadata_config();

/// Get a persistent log entry vector for unit tests, with options to include metrics channel metadata and signal
/// metadata entries
/// @param memres Memory resource to be used for the vector
/// @param include_metrics Whether to include a metrics channel metadata entry
/// @param include_signal_metadata Whether to include a signal metadata entry
/// @return Vector of persistent log entries based on the specified options
[[nodiscard]] std::pmr::vector<PersistentLogEntry> get_test_persistent_entries(
  jewels::memory::MemoryResource memres, bool include_metrics = true, bool include_signal_metadata = false);

} // namespace clockwork_logging::tests
