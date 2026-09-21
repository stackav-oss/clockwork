// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/channel_config_clk_cc.hh"
#include "clockwork/pinion/tcp_bridge.hh"
#include "clockwork/pinion/tcp_bridge_config_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <tclap/CmdLine.h>
#include <tclap/UnlabeledValueArg.h>

#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace clockwork::pinion::tcp_bridge
{

int main(int argc, const char** argv)
{
  const auto memres = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  TCLAP::CmdLine cmd("Clockwork TCP bridge", ' ', "1.0", true);
  const TCLAP::UnlabeledValueArg<std::string> arg_config_file(
    "config", "the bridge configuration file path", true, "", "string", cmd);
  const PinionArgs pinion_args{memres, cmd};

  try
  {
    cmd.parse(argc, argv);
  }
  catch (const std::exception& exc)
  {
    jewels::log_cerr_error("Caught exception parsing command line: {}", exc.what());
    return EXIT_FAILURE;
  }

  auto config_result = read_tachyon_config_to_heap<Tappy<TcpBridgeConfig<>>>(arg_config_file.getValue());
  if (!config_result)
  {
    return EXIT_FAILURE;
  }
  const auto& config = *config_result.value();

  std::pmr::unordered_map<std::pmr::string, clockwork::pinion::ChannelType> channel_types;
  std::pmr::unordered_map<std::pmr::string, std::pmr::vector<std::pmr::string>> publisher_keys(memres);
  for (const auto& consumer : config.get_bridge_clients())
  {
    std::pmr::vector<std::pmr::string> keys(memres);
    const auto& config_keys = consumer.get_publisher_keys();
    keys.reserve(config_keys.size());
    for (const auto& key : config_keys)
    {
      keys.emplace_back(key);
    }
    channel_types.emplace(
      consumer.get_publisher_endpoint().get_publisher_id().to_string(memres),
      consumer.get_publisher_endpoint().get_channel_type());
    publisher_keys.emplace(consumer.get_publisher_endpoint().get_publisher_id().to_string(memres), std::move(keys));
  }
  std::pmr::unordered_map<std::pmr::string, std::pmr::string> subscriber_keys(memres);
  for (const auto& producer : config.get_bridge_servers())
  {
    subscriber_keys.emplace(producer.get_publisher_id().to_string(memres), producer.get_subscriber_key());
    channel_types.emplace(producer.get_publisher_id().to_string(memres), producer.get_channel_type());
  }

  auto channel_factory_result =
    pinion_args.make_factory(std::move(channel_types), std::move(publisher_keys), std::move(subscriber_keys));
  if (!channel_factory_result)
  {
    jewels::log_cerr_error("failed to create channel factory");
    return EXIT_FAILURE;
  }

  TcpBridge bridge{memres, *std::move(channel_factory_result), *config_result.value()};

  if (!ok(bridge.initialize(*config_result.value())))
  {
    return EXIT_FAILURE;
  }

  config_result->reset();

  if (!ok(bridge.run()))
  {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

} // namespace clockwork::pinion::tcp_bridge

int main(int argc, const char** argv)
{
  return clockwork::pinion::tcp_bridge::main(argc, argv);
}
