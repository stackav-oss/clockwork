// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/data_source_loader.hh"

#include "clockwork/common/process_description_clk_cc.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/utility/fix_clockwork_path.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace clockwork::scaffolding
{

namespace
{

// Helper function to validate the current data source index
jewels::BinaryOutcome validate_data_source_index(
  uint16_t current_index,
  std::span<const Tappy<common::DataSource<>>> data_sources,
  const std::pmr::unordered_set<uint16_t>& visited_indices,
  std::string_view instance_name)
{
  if (current_index == common::no_fallback_data_source_sentinel)
  {
    jewels::log_cerr_error("instance '{}': reached no_fallback sentinel while loading data source", instance_name);
    return jewels::failure;
  }

  if (current_index >= data_sources.size())
  {
    jewels::log_cerr_error(
      "instance '{}': data source index {} is out of bounds (only {} data sources available)",
      instance_name,
      current_index,
      data_sources.size());
    return jewels::failure;
  }

  if (visited_indices.contains(current_index))
  {
    jewels::log_cerr_error(
      "instance '{}': cycle detected in data source fallback chain at index {}", instance_name, current_index);
    return jewels::failure;
  }

  return jewels::success;
}

// Helper function to load data from a file data source
jewels::BinaryOutcome load_from_file(
  jewels::Out<std::pmr::vector<std::byte>> data_out,
  const Tappy<common::DataSource<>>& data_source,
  jewels::memory::MemoryResource memres,
  uint16_t current_index,
  std::string_view instance_name)
{
  const auto file_path = jewels::fix_clockwork_path(data_source.get_source_path_or_name());
  auto file = jewels::filesystem::File::open(file_path);
  if (!file)
  {
    jewels::log_cerr_error(
      "instance '{}': failed to open file '{}' for data source {}: {}",
      instance_name,
      data_source.get_source_path_or_name(),
      current_index,
      file.error());
    return jewels::failure;
  }

  auto data = file->read_all(memres);
  if (!data)
  {
    jewels::log_cerr_error(
      "instance '{}': failed to read file '{}' for data source {}: {}",
      instance_name,
      data_source.get_source_path_or_name(),
      current_index,
      data.error());
    return jewels::failure;
  }

  *data_out = std::move(*data);
  return jewels::success;
}

// Helper function to load data from a log first message cache
jewels::BinaryOutcome load_from_cache(
  jewels::Out<std::pmr::vector<std::byte>> data_out,
  const Tappy<common::DataSource<>>& data_source,
  const FirstMessageCache& first_message_cache,
  uint16_t current_index,
  std::string_view instance_name)
{
  const auto channel_name = data_source.get_source_path_or_name();
  auto cache_iter = first_message_cache.find(channel_name);

  if (cache_iter == first_message_cache.end())
  {
    jewels::log_cerr_error(
      "instance '{}': log channel '{}' for data source {} not found in first message cache",
      instance_name,
      channel_name,
      current_index);
    return jewels::failure;
  }

  data_out->assign(cache_iter->second.begin(), cache_iter->second.end());
  return jewels::success;
}

// Helper function to attempt loading from a single data source
jewels::BinaryOutcome try_load_from_data_source(
  jewels::Out<std::pmr::vector<std::byte>> data_out,
  const Tappy<common::DataSource<>>& data_source,
  const FirstMessageCache& first_message_cache,
  jewels::memory::MemoryResource memres,
  uint16_t current_index,
  std::string_view instance_name)
{
  if (data_source.get_data_source_type() == common::DataSourceType::file)
  {
    return load_from_file(jewels::Out{*data_out}, data_source, memres, current_index, instance_name);
  }

  if (data_source.get_data_source_type() == common::DataSourceType::log_first_message)
  {
    return load_from_cache(jewels::Out{*data_out}, data_source, first_message_cache, current_index, instance_name);
  }

  jewels::log_cerr_error(
    "instance '{}': unsupported data source type for data source {}", instance_name, current_index);
  return jewels::failure;
}

} // anonymous namespace

jewels::BinaryOutcome load_data_from_source(
  jewels::Out<DataSourceLoadResult> result_out,
  uint16_t data_source_index,
  std::span<const Tappy<common::DataSource<>>> data_sources,
  jewels::memory::MemoryResource memres,
  const FirstMessageCache& first_message_cache,
  std::string_view instance_name)
{
  std::pmr::unordered_set<uint16_t> visited_indices(memres);

  uint16_t current_index = data_source_index;

  // Iterate through the fallback chain until we succeed, run out of fallbacks, or detect an error
  while (true)
  {
    if (current_index == common::default_construct_data_source_sentinel)
    {
      result_out->data.clear();
      result_out->representation_id = jewels::Uuid<RepresentationTag>{};
      result_out->should_default_construct = true;
      return jewels::success;
    }

    if (jewels::fails(validate_data_source_index(current_index, data_sources, visited_indices, instance_name)))
    {
      return jewels::failure;
    }
    visited_indices.insert(current_index);

    const auto& data_source = data_sources[current_index];
    const auto representation_id = data_source.get_representation_id();

    if (jewels::ok(try_load_from_data_source(
          jewels::Out{result_out->data}, data_source, first_message_cache, memres, current_index, instance_name)))
    {
      result_out->representation_id = representation_id;
      result_out->should_default_construct = false;
      return jewels::success;
    }
    current_index = data_source.get_fallback_source();
  }
}

} // namespace clockwork::scaffolding
