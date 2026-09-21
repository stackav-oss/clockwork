// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/tcp_bridge.hh"

#include "clockwork/common/platform_diagnostics_config_clk_cc.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/diagnostics/report_definitions.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/bridge_status_clk_cc.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/shm_subscriber.hh" // IWYU pragma: keep
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/tcp_bridge_client.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/pinion/tcp_bridge_config_clk_cc.hh"
#include "clockwork/pinion/tcp_bridge_server.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/runners/epoll_manager.hh"
#include "clockwork/scaffolding/channels.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
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

#include <wise_enum.h>
#include <xxh3.h>

#include <chrono>
#include <compare>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

namespace clockwork::pinion
{

using jewels::failure;
using jewels::ok;
using jewels::Out;
using jewels::success;

TcpBridge::TcpBridge(
  jewels::memory::MemoryResource memres,
  std::shared_ptr<AbstractChannelFactory> channel_factory,
  const Tappy<TcpBridgeConfig<>>& bridge_config,
  jewels::time::SyncTime current_time)
  : memres_(std::move(memres)),
    host_name_(bridge_config.get_host_name(), memres_),
    channel_factory_(std::move(channel_factory)),
    publishers_(memres_),
    epoll_(memres_),
    servers_(memres_),
    clients_(memres_),
    diagnostics_state_(jewels::memory::make_pmr_shared<TcpBridgeDiagnosticsState>(memres_)),
    last_status_report_time_(current_time),
    bulk_data_channels_(memres_),
    bulk_data_channel_names_(memres_)
{
}

jewels::BinaryOutcome TcpBridge::initialize(const Tappy<TcpBridgeConfig<>>& bridge_config)
{
  const auto& diagnostics_config = bridge_config.get_diagnostics_config();
  if (!diagnostics_config.get_reporter_id().is_nil())
  {
    if (diagnostics_config.get_group_id() != wise_enum::to_string(diagnostics::SignalGroupId::tcp_bridge))
    {
      jewels::log_cerr_error("Unsupported diagnostics signal group ID: {}", diagnostics_config.get_group_id());
      return jewels::failure;
    }
    if (!ok(open_publisher(
          memres_, diagnostics_config.get_publish_endpoint(), *channel_factory_, Out{diagnostics_publisher_})))
    {
      return failure;
    }
    scaffolding::bind_channel_to_epoll(static_pointer_cast<AbstractChannel>(diagnostics_publisher_), epoll_);
    diagnostics_manager_ = std::make_unique<diagnostics::ClockworkManager<diagnostics::SignalGroupId::tcp_bridge>>(
      diagnostics_config.get_instance_id(), diagnostics_config.get_reporter_id());
    diagnostics_manager_->publisher().set_handle(diagnostics_publisher_->extract_publisher().value());
  }
  const auto& status_publish_endpoint = bridge_config.get_status_publish_endpoint();
  if (!status_publish_endpoint.get_publisher_id().is_nil())
  {
    if (!ok(open_publisher(memres_, status_publish_endpoint, *channel_factory_, Out{status_publisher_})))
    {
      return jewels::failure;
    }
    scaffolding::bind_channel_to_epoll(static_pointer_cast<AbstractChannel>(status_publisher_), epoll_);
    maybe_status_publisher_handle_.emplace(std::move(status_publisher_->extract_publisher()).value());
  }
  if (!ok(open_publishers(bridge_config)))
  {
    return jewels::failure;
  }
  scaffolding::bind_channels_to_epoll(publishers_, epoll_);
  if (!ok(create_bridge_servers(bridge_config)))
  {
    return jewels::failure;
  }
  if (!ok(create_bridge_clients(bridge_config)))
  {
    return jewels::failure;
  }
  return success;
}

jewels::BinaryOutcome TcpBridge::run()
{
  while (true)
  {
    if (!ok(run_once(jewels::time::SyncClock::now())))
    {
      return failure;
    }
  }
}

jewels::BinaryOutcome TcpBridge::run_once(jewels::time::SyncTime current_time)
{
  if (current_time - last_diagnostics_report_time_ > diagnostics_report_interval)
  {
    const auto diagnostics_counters = diagnostics_state_->get_and_reset_counters();
    last_diagnostics_report_time_ = current_time;
    combine_diagnostics_counters(diagnostics_counters, bridge_status_diagnostics_counters_);
    publish_bridge_diagnostics(diagnostics_counters, current_time);
    if (diagnostics_counters.max_bridge_latency >= min_reportable_bridge_latency)
    {
      jewels::log_cerr_warn(
        "Max bridge latency {} msec, channel {}",
        std::chrono::duration_cast<std::chrono::milliseconds>(diagnostics_counters.max_bridge_latency).count(),
        diagnostics_counters.max_latency_channel_name);
    }
    if (diagnostics_counters.max_bridge_bulk_data_latency >= min_reportable_bridge_bulk_data_latency)
    {
      jewels::log_cerr_warn(
        "Max bridge bulk data latency {} msec, channel {}",
        std::chrono::duration_cast<std::chrono::milliseconds>(diagnostics_counters.max_bridge_bulk_data_latency)
          .count(),
        diagnostics_counters.max_bulk_data_latency_channel_name);
    }
  }
  if (current_time - last_status_report_time_ > status_report_interval)
  {
    const auto report_interval = current_time - last_status_report_time_;
    last_status_report_time_ = current_time;
    publish_bridge_status(bridge_status_diagnostics_counters_, report_interval, current_time);
    bridge_status_diagnostics_counters_ = {};
  }
  if (const auto result = epoll_.wait(epoll_wait_interval); !result)
  {
    diagnostics_state_->increment_epoll_errors();
    jewels::log_cerr_error("epoll wait failed with {}", result.error());
  }
  return success;
}

[[nodiscard]] size_t TcpBridge::get_num_servers() const
{
  return servers_.size();
}

[[nodiscard]] size_t TcpBridge::get_num_clients() const
{
  return clients_.size();
}

[[nodiscard]] AbstractChannelFactory& TcpBridge::channel_factory()
{
  return *channel_factory_;
}

[[nodiscard]] TcpBridgeDiagnosticsState& TcpBridge::diagnostics_state()
{
  return *diagnostics_state_;
}

jewels::BinaryOutcome TcpBridge::open_publisher(
  const jewels::memory::MemoryResource& memres,
  const Tappy<common::PublishEndpoint<common::MAX_CHANNEL_NAME_SIZE>>& publish_endpoint,
  AbstractChannelFactory& channel_factory,
  Out<std::shared_ptr<pinion::AbstractPublisher>> publisher)
{
  const auto& buffer_layout = publish_endpoint.get_buffer_layout();
  const auto layout = pinion::BufferLayout{
    .num_slots = buffer_layout.get_num_slots(),
    .message_size = buffer_layout.get_message_size(),
    .is_published_once = buffer_layout.get_is_published_once(),
  };
  const auto uuid_str = publish_endpoint.get_publisher_id().to_string(memres);
  auto open_result = channel_factory.open_publisher(
    uuid_str, publish_endpoint.get_channel_name(), layout, publish_endpoint.get_num_subscribers());
  if (!open_result)
  {
    jewels::log_cerr_error(
      "failed to open publisher for channel {}: {}", publish_endpoint.get_channel_name(), open_result.error());
    return failure;
  }
  *publisher = std::move(open_result).value();
  return success;
}

jewels::BinaryOutcome TcpBridge::open_publishers(const Tappy<TcpBridgeConfig<>>& bridge_config)
{
  for (const auto& config : bridge_config.get_bridge_clients())
  {
    std::shared_ptr<pinion::AbstractPublisher> publisher;
    if (!ok(open_publisher(memres_, config.get_publisher_endpoint(), *channel_factory_, Out{publisher})))
    {
      return failure;
    }
    while (!publisher->handshake())
    {
      jewels::log_cerr_info(
        "Waiting for publisher initialization to complete ({})", config.get_publisher_endpoint().get_channel_name());
      std::this_thread::sleep_for(clockwork::scaffolding::channel_connect_sleep_time);
    }
    publishers_.emplace(config.get_publisher_endpoint().get_publisher_id(), std::move(publisher));
  }
  return success;
}

jewels::BinaryOutcome TcpBridge::create_bridge_servers(const Tappy<TcpBridgeConfig<>>& bridge_config)
{
  for (const auto& config : bridge_config.get_bridge_servers())
  {
    if (config.get_is_bulk_data())
    {
      // NOLINTNEXTLINE(modernize-use-emplace) Compiler doesn't accept emplace_back(channel_name, memory_resource_)
      bulk_data_channel_names_.emplace_back(std::pmr::string{config.get_channel_name(), memres_});
      bulk_data_channels_.emplace(bulk_data_channel_names_.back());
    }
    auto bridge_server = TcpBridgeServer::make(
      memres_, config, channel_factory_, jewels::memory::make_non_null_from_ref(epoll_), diagnostics_state_);
    if (!bridge_server)
    {
      return failure;
    }
    servers_.emplace(config.get_publisher_id(), std::move(bridge_server));
  }
  return success;
}

jewels::BinaryOutcome TcpBridge::create_bridge_clients(const Tappy<TcpBridgeConfig<>>& bridge_config)
{
  for (const auto& config : bridge_config.get_bridge_clients())
  {
    auto& channel = publishers_.at(config.get_publisher_endpoint().get_publisher_id());
    auto publisher = std::dynamic_pointer_cast<AbstractPublisher>(channel)->extract_publisher();
    if (!publisher)
    {
      jewels::log_cerr_error(
        "Failed to extract publisher handle for channel {}", config.get_publisher_endpoint().get_publisher_id());
      return failure;
    }
    auto bridge_client = TcpBridgeClient::make(memres_, config, *std::move(publisher), channel, diagnostics_state_);
    if (!bridge_client)
    {
      return failure;
    }
    if (config.get_publisher_endpoint().get_is_bulk_data())
    {
      bulk_data_channel_names_.emplace_back(
        // NOLINTNEXTLINE(modernize-use-emplace) Compiler doesn't accept emplace(channel_name, memory_resource_)
        std::pmr::string{config.get_publisher_endpoint().get_channel_name(), memres_});
      bulk_data_channels_.emplace(bulk_data_channel_names_.back());
    }
    clients_.emplace(config.get_publisher_endpoint().get_publisher_id(), std::move(bridge_client));
  }
  return success;
}

void TcpBridge::publish_bridge_diagnostics(
  const TcpBridgeDiagnosticsCounters& diagnostics_counters, jewels::time::SyncTime current_time)
{
  if (!diagnostics_manager_)
  {
    return;
  }
  auto report = diagnostics_manager_->create_report(current_time);
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
  report.set<diagnostics::SignalId::max_bulk_data_latency_ms>(static_cast<size_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(diagnostics_counters.max_bridge_bulk_data_latency).count()));
}

void TcpBridge::publish_bridge_status(
  const TcpBridgeDiagnosticsCounters& diagnostics_counters,
  std::chrono::nanoseconds report_interval,
  jewels::time::SyncTime current_time)
{
  if (!maybe_status_publisher_handle_)
  {
    return;
  }
  auto reservation = maybe_status_publisher_handle_->reserve();
  if (!reservation)
  {
    jewels::log_cerr_error("Failed to reserve bridge status message slot: {}", reservation.error());
    diagnostics_state_->increment_status_errors();
    return;
  }
  auto slot = reservation->slots().front();
  auto& status_msg =
    *detail::marshal_as<Tappy<BridgeStatus>>(std::span<std::byte, sizeof(Tappy<BridgeStatus>)>{slot.message()});
  status_msg = {};
  status_msg.get_underlying_host_name().set_truncate(host_name_);
  TcpBridgeClientServerCounters total_client_counters{};
  TcpBridgeClientServerCounters total_client_bulk_data_counters{};
  std::pmr::map<std::string_view, TcpBridgeClientServerCounters> client_counters_map{memres_};
  for (auto& client : std::views::values(clients_))
  {
    const auto channel_name = client->channel_name();
    const auto counters = client->get_and_reset_counters();
    combine_client_server_counters(counters, client_counters_map[channel_name]);
    if (bulk_data_channels_.contains(channel_name))
    {
      combine_client_server_counters(counters, total_client_bulk_data_counters);
    }
    else
    {
      combine_client_server_counters(counters, total_client_counters);
    }
  }
  for (const auto& [channel_name, counters] : client_counters_map)
  {
    if (counters.message_count != 0U)
    {
      auto& status_counters = status_msg.get_underlying_client_counters().emplace_back();
      store_client_server_counters(channel_name, counters, report_interval, status_counters);
    }
  }
  auto& status_total_client_counters = status_msg.get_underlying_client_counters().emplace_back();
  store_client_server_counters("CLIENT TOTAL", total_client_counters, report_interval, status_total_client_counters);
  auto& status_total_client_bulk_data_counters = status_msg.get_underlying_client_counters().emplace_back();
  store_client_server_counters(
    "CLIENT BULK DATA TOTAL", total_client_bulk_data_counters, report_interval, status_total_client_bulk_data_counters);
  TcpBridgeClientServerCounters total_server_counters{};
  TcpBridgeClientServerCounters total_server_bulk_data_counters{};
  std::pmr::map<std::string_view, TcpBridgeClientServerCounters> server_counters_map{memres_};
  for (auto& server : std::views::values(servers_))
  {
    const auto channel_name = server->channel_name();
    const auto counters = server->get_and_reset_counters();
    combine_client_server_counters(counters, server_counters_map[channel_name]);
    if (bulk_data_channels_.contains(channel_name))
    {
      combine_client_server_counters(counters, total_server_bulk_data_counters);
    }
    else
    {
      combine_client_server_counters(counters, total_server_counters);
    }
  }
  for (const auto& [channel_name, counters] : server_counters_map)
  {
    if (counters.message_count != 0U)
    {
      auto& status_counters = status_msg.get_underlying_server_counters().emplace_back();
      store_client_server_counters(channel_name, counters, report_interval, status_counters);
    }
  }
  auto& status_total_server_counters = status_msg.get_underlying_server_counters().emplace_back();
  store_client_server_counters("SERVER TOTAL", total_server_counters, report_interval, status_total_server_counters);
  auto& status_total_server_bulk_data_counters = status_msg.get_underlying_server_counters().emplace_back();
  store_client_server_counters(
    "SERVER BULK DATA TOTAL", total_server_bulk_data_counters, report_interval, status_total_server_bulk_data_counters);
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
  status_msg.get_mutable_diagnostics_counters().set_max_bridge_bulk_data_latency(
    diagnostics_counters.max_bridge_bulk_data_latency);
  status_msg.get_mutable_diagnostics_counters().get_underlying_max_bulk_data_latency_channel_name().set_truncate(
    diagnostics_counters.max_bulk_data_latency_channel_name);
  if (const auto commit_result = reservation->commit(current_time); !commit_result)
  {
    jewels::log_cerr_error("Failed to commit bridge status message: {}", commit_result.error());
    diagnostics_state_->increment_status_errors();
    return;
  }
}

void TcpBridge::request_stop()
{
  for (auto& client : std::views::values(clients_))
  {
    client->request_stop();
  }
}

} // namespace clockwork::pinion
