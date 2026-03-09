// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/tags.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace clockwork::scaffolding
{

/// Cache of first messages from log channels, used for data source restoration.
/// Channel name string_views must point to memory that remains valid for the lifetime of the cache.
/// Value is the first message data for that channel.
using FirstMessageCache =
  std::pmr::unordered_map<std::string_view /* channel_name */, std::pmr::vector<std::byte> /* first_message_data */>;

/// Result of loading data from a DataSource
struct DataSourceLoadResult
{
  /// Constructor that takes a memory resource and initializes data with it
  explicit DataSourceLoadResult(jewels::memory::MemoryResource memres)
    : data(memres)
  {
  }

  /// The loaded data (empty if default_construct was used)
  std::pmr::vector<std::byte> data;

  /// The representation ID of the loaded data
  jewels::Uuid<RepresentationTag> representation_id{};

  /// True if we hit the default_construct sentinel (data will be empty)
  bool should_default_construct{};
};

/// Load data from a DataSource with fallback support.
///
/// This function handles all DataSource types (file, log_first_message) and follows
/// fallback chains when the primary source is unavailable. It also handles sentinel
/// values for default construction and no-fallback. It does not do any data conversion.
///
/// @param result_out Loaded data (might be mutated even on failure)
/// @param data_source_index Index of the data source to load (into data_sources span)
/// @param data_sources Span of all available data sources
/// @param memres Memory resource for allocations
/// @param first_message_cache Pre-populated cache of first messages from log
/// @param instance_name Human-readable name for error messages
/// @return success on successful load, failure otherwise
jewels::BinaryOutcome load_data_from_source(
  jewels::Out<DataSourceLoadResult> result_out,
  uint16_t data_source_index,
  std::span<const Tappy<common::DataSource<>>> data_sources,
  jewels::memory::MemoryResource memres,
  const FirstMessageCache& first_message_cache,
  std::string_view instance_name);

} // namespace clockwork::scaffolding
