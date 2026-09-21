// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/schema_encoding_clk_cc.hh"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace clockwork_logging
{

inline constexpr std::string_view metrics_channel_metadata_channel_name = "/clockwork/metrics_channel_metadata";
inline constexpr std::string_view signal_metadata_channel_name = "/clockwork/signal_metadata";

/// Self-contained description of a persistent message to be written to a log during initialization.
/// Each entry represents one config (e.g., metrics channel metadata, signal metadata) that should
/// be published as a persistent channel in the output log.
struct PersistentLogEntry
{
  std::string channel_name;
  std::string schema_name;
  SchemaEncoding schema_encoding{SchemaEncoding::clockwork_tachyon};
  std::string schema_definition;

  /// Type-erased owner that keeps the underlying tachyon object alive while `data` references it.
  std::shared_ptr<const void> data_owner;
  std::span<const std::byte> data;
};

} // namespace clockwork_logging
