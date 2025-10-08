// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/platform_diagnostics_config.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/diagnostics/report_definitions.hh"
#include "clockwork/diagnostics/reporter.hh"
#include "clockwork/pinion/bridge_status.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/shm_channel.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/pinion/shm_subscriber.hh" // IWYU pragma: keep
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/tcp_bridge_client.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/pinion/tcp_bridge_config.hh"
#include "clockwork/pinion/tcp_bridge_server.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/runners/epoll_manager.hh"
#include "clockwork/scaffolding/channels.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <tclap/CmdLine.h>
#include <tclap/UnlabeledValueArg.h>
#include <wise_enum.h>
#include <xxh3.h>

#include <chrono>
#include <compare>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace clockwork::pinion::tcp_bridge
{

namespace
{

constexpr auto status_report_interval = std::chrono::seconds(1);
constexpr auto epoll_wait_interval = std::chrono::milliseconds(1);
constexpr auto min_reportable_bridge_latency = std::chrono::milliseconds(25);
constexpr auto send_null_header_interval = std::chrono::milliseconds(2);

jewels::expected<scaffolding::ChannelMap, jewels::MonoError> open_subscribers(
  jewels::memory::MemoryResource memres,
  ShmChannelFactory& channel_factory,
  std::span<const TcpBridgeServerConfigTap> server_configs)
{
  scaffolding::ChannelMap subscribers(server_configs.size(), memres);
  std::pmr::list<std::reference_wrapper<const TcpBridgeServerConfigTap>> pending(memres);
  for (const auto& config : server_configs)
  {
    pending.push_back(std::ref(config));
  }

  while (true)
  {
    for (auto config_it = pending.begin(); config_it != pending.end();)
    {
      const auto& config = config_it->get();
      const auto uuid_str = config.get_publisher_id().to_string(memres);
      const pinion::BufferLayout layout{
        .num_slots = config.get_buffer_layout().get_num_slots(),
        .message_size = config.get_buffer_layout().get_message_size(),
      };
      auto subscriber = channel_factory.open_subscriber(uuid_str, config.get_channel_name(), layout, 1);
      if (subscriber)
      {
        subscribers.emplace(config.get_publisher_id(), std::move(subscriber.value()));
        config_it = pending.erase(config_it);
      }
      else if (subscriber.error() == pinion::ShmChannel::Error::missing)
      {
        ++config_it;
      }
      else
      {
        jewels::log_cerr_error(
          "Could not create subscriber '{}': {}", config.get_channel_name(), wise_enum::to_string(subscriber.error()));
        return jewels::unexpected(jewels::MonoError());
      }
    }
    if (pending.empty())
    {
      break;
    }
    jewels::log_cerr_info(
      "Waiting for publisher creation ({} subscribers remaining. Next up: {})",
      pending.size(),
      pending.begin()->get().get_channel_name());
    std::this_thread::sleep_for(scaffolding::channel_connect_sleep_time);
  }
  return std::move(subscribers);
}

template <size_t max_channel_name_len>
jewels::expected<std::shared_ptr<pinion::ShmPublisher>, jewels::MonoError> open_publisher(
  jewels::memory::MemoryResource memres,
  ShmChannelFactory& channel_factory,
  const Tappy<common::PublishEndpoint<max_channel_name_len>>& publish_endpoint)
{
  const auto& buffer_layout = publish_endpoint.get_buffer_layout();
  const auto layout = pinion::BufferLayout{
    .num_slots = buffer_layout.get_num_slots(),
    .message_size = buffer_layout.get_message_size(),
  };
  const auto uuid_str = publish_endpoint.get_publisher_id().to_string(memres);
  auto open_result = channel_factory.open_publisher(
    uuid_str, publish_endpoint.get_channel_name(), layout, publish_endpoint.get_num_subscribers());
  if (!open_result)
  {
    jewels::log_cerr_error(
      "failed to open publisher for channel {}: {}", publish_endpoint.get_channel_name(), open_result.error());
    return jewels::unexpected{jewels::MonoError{}};
  }
  return {std::move(open_result).value()};
}

jewels::expected<scaffolding::ChannelMap, jewels::MonoError> open_publishers(
  jewels::memory::MemoryResource memres,
  ShmChannelFactory& channel_factory,
  std::span<const TcpBridgeClientConfigTap> client_configs)
{
  scaffolding::ChannelMap publishers(memres);
  for (const auto& config : client_configs)
  {
    auto open_result = open_publisher(memres, channel_factory, config.get_publisher_endpoint());
    if (!open_result)
    {
      return jewels::unexpected{jewels::MonoError{}};
    }
    publishers.emplace(config.get_publisher_endpoint().get_publisher_id(), std::move(open_result).value());
  }
  return {publishers};
}

using ServerMapType = std::pmr::unordered_map<
  jewels::Uuid<common::EndpointInstanceId>,
  std::shared_ptr<TcpBridgeServer>,
  jewels::UuidHasher<common::EndpointInstanceId>>;

jewels::expected<ServerMapType, jewels::MonoError> create_bridge_servers(
  jewels::memory::MemoryResource memres,
  scaffolding::ChannelMap& subscribers,
  AbstractEPollManager& epoll,
  std::span<const TcpBridgeServerConfigTap> server_configs,
  const std::shared_ptr<TcpBridgeDiagnosticsCounters>& diagnostics_counters)
{
  ServerMapType servers(memres);
  for (const auto& config : server_configs)
  {
    const auto& channel = subscribers.at(config.get_publisher_id());
    auto bridge_server = TcpBridgeServer::make(
      memres, config, channel->make_subscriber(), jewels::memory::make_non_null_from_ref(epoll), diagnostics_counters);
    if (!bridge_server)
    {
      return jewels::unexpected{jewels::MonoError{}};
    }
    if (!channel->add_observer(jewels::memory::make_non_null_from_ref(*bridge_server)))
    {
      jewels::log_cerr_error("failed to add observer on channel {}", config.get_publisher_id());
      return jewels::unexpected{jewels::MonoError{}};
    }

    servers.emplace(config.get_publisher_id(), std::move(bridge_server));
  }
  return {servers};
}

using ClientMapType = std::pmr::unordered_map<
  jewels::Uuid<common::EndpointInstanceId>,
  std::shared_ptr<TcpBridgeClient>,
  jewels::UuidHasher<common::EndpointInstanceId>>;

jewels::expected<ClientMapType, jewels::MonoError> create_bridge_clients(
  jewels::memory::MemoryResource memres,
  scaffolding::ChannelMap& publishers,
  AbstractEPollManager& epoll,
  std::span<const TcpBridgeClientConfigTap> client_configs,
  const std::shared_ptr<TcpBridgeDiagnosticsCounters>& diagnostics_counters)
{
  ClientMapType clients(memres);
  for (const auto& config : client_configs)
  {
    auto& channel = publishers.at(config.get_publisher_endpoint().get_publisher_id());
    auto publisher = std::dynamic_pointer_cast<ShmPublisher>(channel)->extract_publisher();
    if (!publisher)
    {
      jewels::log_cerr_error(
        "Failed to extract publisher handle for channel {}", config.get_publisher_endpoint().get_publisher_id());
      return jewels::unexpected{jewels::MonoError{}};
    }
    auto bridge_client = TcpBridgeClient::make(
      memres,
      config,
      *std::move(publisher),
      channel->make_subscriber(),
      jewels::memory::make_non_null_from_ref(epoll),
      diagnostics_counters);
    if (!bridge_client)
    {
      return jewels::unexpected{jewels::MonoError{}};
    }
    clients.emplace(config.get_publisher_endpoint().get_publisher_id(), std::move(bridge_client));
  }
  return {clients};
}

void publish_bridge_diagnostics(
  diagnostics::ClockworkManager<diagnostics::SignalGroupId::tcp_bridge>& diagnostics_manager,
  const TcpBridgeDiagnosticsCounters& diagnostics_counters)
{
  auto report = diagnostics_manager.create_report(jewels::time::SyncClock::now());
  report.set<diagnostics::SignalId::drop_count>(diagnostics_counters.drop_count);
  report.set<diagnostics::SignalId::failed_sends>(diagnostics_counters.failed_sends);
  report.set<diagnostics::SignalId::closed_socket_count>(diagnostics_counters.closed_socket_count);
  report.set<diagnostics::SignalId::failed_recvs>(diagnostics_counters.failed_recvs);
  report.set<diagnostics::SignalId::failed_reservations>(diagnostics_counters.failed_reservations);
  report.set<diagnostics::SignalId::malformed_messages>(diagnostics_counters.malformed_messages);
  report.set<diagnostics::SignalId::failed_commits>(diagnostics_counters.failed_commits);
  report.set<diagnostics::SignalId::failed_discards>(diagnostics_counters.failed_discards);
  report.set<diagnostics::SignalId::client_socket_errors>(diagnostics_counters.client_socket_errors);
  report.set<diagnostics::SignalId::progress_errors>(diagnostics_counters.progress_errors);
  report.set<diagnostics::SignalId::epoll_errors>(diagnostics_counters.epoll_errors);
  report.set<diagnostics::SignalId::status_errors>(diagnostics_counters.status_errors);
  report.set<diagnostics::SignalId::max_latency_ms>(static_cast<size_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(diagnostics_counters.max_bridge_latency).count()));
}

void publish_bridge_status(
  jewels::memory::MemoryResource memres,
  PublisherHandle& publisher_handle,
  std::string_view host_name,
  std::chrono::nanoseconds interval,
  ClientMapType& clients,
  ServerMapType& servers,
  TcpBridgeDiagnosticsCounters& diagnostics_counters)
{
  auto reservation = publisher_handle.reserve();
  if (!reservation)
  {
    jewels::log_cerr_error("Failed to reserve bridge status message slot: {}", reservation.error());
    diagnostics_counters.status_errors++;
    return;
  }
  auto slot = reservation->slot();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) TODO(OI-3013): refactor to use marshal_as(...)
  auto& status_msg = *reinterpret_cast<Tappy<BridgeStatus>*>(slot.message().data());
  status_msg = {};
  status_msg.get_underlying_host_name().set_truncate(host_name);
  TcpBridgeClientServerCounters total_client_counters{};
  std::pmr::map<std::string_view, TcpBridgeClientServerCounters> client_counters_map{memres};
  for (auto& client : std::views::values(clients))
  {
    const auto channel_name = client->channel_name();
    const auto counters = client->get_and_reset_counters();
    combine_client_server_counters(counters, client_counters_map[channel_name]);
    combine_client_server_counters(counters, total_client_counters);
  }
  for (const auto& [channel_name, counters] : client_counters_map)
  {
    auto& status_counters = status_msg.get_underlying_client_counters().emplace_back();
    store_client_server_counters(channel_name, counters, interval, status_counters);
  }
  auto& status_total_client_counters = status_msg.get_underlying_client_counters().emplace_back();
  store_client_server_counters("CLIENT TOTAL", total_client_counters, interval, status_total_client_counters);
  TcpBridgeClientServerCounters total_server_counters{};
  std::pmr::map<std::string_view, TcpBridgeClientServerCounters> server_counters_map{memres};
  for (auto& server : std::views::values(servers))
  {
    const auto channel_name = server->channel_name();
    const auto counters = server->get_and_reset_counters();
    combine_client_server_counters(counters, server_counters_map[channel_name]);
    combine_client_server_counters(counters, total_server_counters);
  }
  for (const auto& [channel_name, counters] : server_counters_map)
  {
    auto& status_counters = status_msg.get_underlying_server_counters().emplace_back();
    store_client_server_counters(channel_name, counters, interval, status_counters);
  }
  auto& status_total_server_counters = status_msg.get_underlying_server_counters().emplace_back();
  store_client_server_counters("SERVER TOTAL", total_server_counters, interval, status_total_server_counters);
  status_msg.get_mutable_diagnostics_counters().set_drop_count(diagnostics_counters.drop_count);
  status_msg.get_mutable_diagnostics_counters().set_failed_sends(diagnostics_counters.failed_sends);
  status_msg.get_mutable_diagnostics_counters().set_closed_socket_count(diagnostics_counters.closed_socket_count);
  status_msg.get_mutable_diagnostics_counters().set_failed_recvs(diagnostics_counters.failed_recvs);
  status_msg.get_mutable_diagnostics_counters().set_failed_reservations(diagnostics_counters.failed_reservations);
  status_msg.get_mutable_diagnostics_counters().set_malformed_messages(diagnostics_counters.malformed_messages);
  status_msg.get_mutable_diagnostics_counters().set_failed_commits(diagnostics_counters.failed_commits);
  status_msg.get_mutable_diagnostics_counters().set_failed_discards(diagnostics_counters.failed_discards);
  status_msg.get_mutable_diagnostics_counters().set_client_socket_errors(diagnostics_counters.client_socket_errors);
  status_msg.get_mutable_diagnostics_counters().set_progress_errors(diagnostics_counters.progress_errors);
  status_msg.get_mutable_diagnostics_counters().set_epoll_errors(diagnostics_counters.epoll_errors);
  status_msg.get_mutable_diagnostics_counters().set_max_bridge_latency(diagnostics_counters.max_bridge_latency);
  status_msg.get_mutable_diagnostics_counters().get_underlying_max_latency_channel_name().set_truncate(
    diagnostics_counters.max_latency_channel_name);
  if (const auto commit_result = reservation->commit(jewels::time::SyncClock::now()); !commit_result)
  {
    jewels::log_cerr_error("Failed to commit bridge status message: {}", commit_result.error());
    diagnostics_counters.status_errors++;
    return;
  }
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity, readability-function-size) TODO(OI-3013) Refactor TcpBridge
int main(int argc, const char** argv)
{
  auto memres_channels = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  TCLAP::CmdLine cmd("Clockwork TCP bridge", ' ', "1.0", true);
  const TCLAP::UnlabeledValueArg<std::string> arg_config_file(
    "config", "the bridge configuration file path", true, "", "string", cmd);
  const PinionArgs pinion_args{memres_channels, cmd};

  try
  {
    cmd.parse(argc, argv);
  }
  catch (const std::exception& exc)
  {
    std::cerr << "Caught exception parsing command line: " << exc.what() << '\n';
    return 1;
  }

  auto config = read_tachyon_config<TcpBridgeConfigTap>(arg_config_file.getValue());
  if (!config)
  {
    return EXIT_FAILURE;
  }

  auto channel_factory = pinion_args.make_factory();
  if (!channel_factory)
  {
    jewels::log_cerr_error("failed to create channel factory");
    return EXIT_FAILURE;
  }
  std::shared_ptr<ShmPublisher> diagnostics_publisher;
  const auto& diagnostics_config = config->get_diagnostics_config();
  if (!diagnostics_config.get_reporter_id().is_nil())
  {
    if (diagnostics_config.get_group_id() != wise_enum::to_string(diagnostics::SignalGroupId::tcp_bridge))
    {
      jewels::log_cerr_error("Unsupported diagnostics signal group ID: {}", diagnostics_config.get_group_id());
      return EXIT_FAILURE;
    }
    auto diagnostics_result =
      open_publisher(memres_channels, *channel_factory, diagnostics_config.get_publish_endpoint());
    if (!diagnostics_result)
    {
      return EXIT_FAILURE;
    }
    diagnostics_publisher = std::move(diagnostics_result).value();
  }
  std::shared_ptr<ShmPublisher> status_publisher;
  std::optional<PublisherHandle> status_publisher_handle;
  const auto& status_publish_endpoint = config->get_status_publish_endpoint();
  if (!status_publish_endpoint.get_publisher_id().is_nil())
  {
    auto status_result = open_publisher(memres_channels, *channel_factory, status_publish_endpoint);
    if (!status_result)
    {
      return EXIT_FAILURE;
    }
    status_publisher = std::move(status_result).value();
    status_publisher_handle.emplace(std::move(status_publisher->extract_publisher()).value());
  }
  auto publishers = open_publishers(memres_channels, *channel_factory, config->get_bridge_clients());
  if (!publishers)
  {
    return EXIT_FAILURE;
  }
  auto subscribers = open_subscribers(memres_channels, *channel_factory, config->get_bridge_servers());
  if (!subscribers)
  {
    return EXIT_FAILURE;
  }

  const auto memres_epoll = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  EPollManager epoll{memres_epoll};
  scaffolding::bind_channels_to_epoll(*subscribers, epoll);
  scaffolding::bind_channels_to_epoll(*publishers, epoll);
  if (diagnostics_publisher)
  {
    scaffolding::bind_channel_to_epoll(static_pointer_cast<ShmChannel>(diagnostics_publisher), epoll);
  }
  if (status_publisher)
  {
    scaffolding::bind_channel_to_epoll(static_pointer_cast<ShmChannel>(status_publisher), epoll);
  }

  std::optional<diagnostics::ClockworkManager<diagnostics::SignalGroupId::tcp_bridge>> maybe_diagnostics_manager;
  if (diagnostics_publisher)
  {
    maybe_diagnostics_manager.emplace(diagnostics_config.get_instance_id(), diagnostics_config.get_reporter_id());
    maybe_diagnostics_manager->publisher().set_handle(diagnostics_publisher->extract_publisher().value());
  }

  auto memres_bridges = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  const auto diagnostics_counters = jewels::memory::make_pmr_shared<TcpBridgeDiagnosticsCounters>(memres_bridges);
  auto servers =
    create_bridge_servers(memres_bridges, *subscribers, epoll, config->get_bridge_servers(), diagnostics_counters);
  if (!servers)
  {
    return EXIT_FAILURE;
  }
  auto clients =
    create_bridge_clients(memres_bridges, *publishers, epoll, config->get_bridge_clients(), diagnostics_counters);
  if (!clients)
  {
    return EXIT_FAILURE;
  }

  auto last_status_report_time = jewels::time::SyncClock::now();
  auto last_null_header_send_time = jewels::time::SyncClock::now();
  while (true)
  {
    auto result = epoll.wait(epoll_wait_interval);
    if (!result)
    {
      diagnostics_counters->epoll_errors++;
      jewels::log_cerr_error("epoll wait failed with {}", result.error());
    }
    auto current_time = jewels::time::SyncClock::now();
    if (current_time - last_null_header_send_time > send_null_header_interval)
    {
      last_null_header_send_time = current_time;
      for (auto& server : std::views::values(*servers))
      {
        if (server)
        {
          server->send_null_header_if_waiting_for_ack();
        }
      }
    }
    current_time = jewels::time::SyncClock::now();
    if (current_time - last_status_report_time > status_report_interval)
    {
      const auto report_interval = current_time - last_status_report_time;
      last_status_report_time = current_time;
      if (status_publisher_handle)
      {
        publish_bridge_status(
          memres_bridges,
          *status_publisher_handle,
          config->get_host_name(),
          report_interval,
          clients.value(),
          servers.value(),
          *diagnostics_counters);
      }
      if (maybe_diagnostics_manager)
      {
        publish_bridge_diagnostics(*maybe_diagnostics_manager, *diagnostics_counters);
      }
      if (diagnostics_counters->max_bridge_latency >= min_reportable_bridge_latency)
      {
        jewels::log_cerr_warn(
          "Max bridge latency {} msec, channel {}",
          std::chrono::duration_cast<std::chrono::milliseconds>(diagnostics_counters->max_bridge_latency).count(),
          diagnostics_counters->max_latency_channel_name);
      }
      *diagnostics_counters = {};
    }
  }

  return 0;
}

} // namespace clockwork::pinion::tcp_bridge

// TODO(OI-2692): Implement end to end unit test for the TCP bridge
int main(int argc, const char** argv)
{
  return clockwork::pinion::tcp_bridge::main(argc, argv);
}
