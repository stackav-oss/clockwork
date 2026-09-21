// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/first_message_cache.hh"

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/runners/channel_publisher.hh"
#include "clockwork/scaffolding/data_source_loader.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"

#include <functional>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace clockwork::scaffolding
{

jewels::BinaryOutcome populate_first_message_cache(
  jewels::Out<FirstMessageCache> first_message_cache_out,
  std::span<const Tappy<common::DataSource<>>> data_sources,
  MessageFetcher& message_fetcher,
  jewels::memory::MemoryResource memres)
{
  // Collect all channel names that are referenced by log_first_message data sources
  std::pmr::unordered_set<std::string_view> pending_channels{memres};
  for (const auto& data_source : data_sources)
  {
    if (data_source.get_data_source_type() == common::DataSourceType::log_first_message)
    {
      pending_channels.emplace(data_source.get_source_path_or_name());
    }
  }

  if (pending_channels.empty())
  {
    return jewels::success;
  }

  while (!pending_channels.empty())
  {
    auto maybe_message = message_fetcher.try_fetch_message();
    if (!maybe_message)
    {
      break;
    }

    auto& message_info = *maybe_message;
    if (const auto channel_it = pending_channels.find(message_info.channel); channel_it != pending_channels.end())
    {
      first_message_cache_out->emplace(*channel_it, std::move(message_info.msgs.front()));
      pending_channels.erase(channel_it);
      if (pending_channels.empty())
      {
        break;
      }
    }
  }

  if (!pending_channels.empty())
  {
    jewels::log_cerr_warn(
      "Only found {} of {} expected data source channels in log",
      first_message_cache_out->size(),
      pending_channels.size());

    for (const auto& expected_channel : pending_channels)
    {
      jewels::log_cerr_warn("  Missing channel: {}", expected_channel);
    }
  }

  return jewels::success;
}

} // namespace clockwork::scaffolding
