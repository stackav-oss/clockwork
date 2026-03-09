// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/runners/channel_publisher.hh"
#include "clockwork/scaffolding/data_source_loader.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"

#include <span>

namespace clockwork::scaffolding
{

/// Populate the first message cache from a message fetcher.
///
/// This function scans messages from a MessageFetcher and caches the first message
/// on each channel referenced by log_first_message data sources. It stops early
/// once all required channels have been found.
///
/// @note The MessageFetcher must be configured to read all relevant channels.
/// In typical use this is ensured by the system composition compiler generating
/// the channel publisher config to include the data source channels.
///
/// @param first_message_cache_out The cache to populate (might be mutated even on failure)
/// @param data_sources Data source definitions to scan for log_first_message types
/// @param message_fetcher The message fetcher to read messages from
/// @param memres Memory resource for allocations
/// @return success if cache was populated, failure on error
jewels::BinaryOutcome populate_first_message_cache(
  jewels::Out<FirstMessageCache> first_message_cache_out,
  std::span<const Tappy<common::DataSource<>>> data_sources,
  MessageFetcher& message_fetcher,
  jewels::memory::MemoryResource memres);

} // namespace clockwork::scaffolding
