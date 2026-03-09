#pragma once
// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/diagnostics/report_definitions.hh"
#include "clockwork/diagnostics/reporter.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/pinion/shm_subscriber.hh" // IWYU pragma: keep
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
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <chrono>
#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace clockwork::pinion
{

/// TCP bridge implementation
class TcpBridge
{
public:
  /// Status report publish interval
  static constexpr auto status_report_interval = std::chrono::seconds(1);

  /// EPoll wait interval to allow the bridge to wake up for periodic processing
  static constexpr auto epoll_wait_interval = std::chrono::milliseconds(100);

  /// Bridge server map type
  using ServerMapType = std::pmr::unordered_map<
    jewels::Uuid<common::EndpointInstanceId>,
    std::shared_ptr<TcpBridgeServer>,
    jewels::UuidHasher<common::EndpointInstanceId>>;

  /// Bridge client map type
  using ClientMapType = std::pmr::unordered_map<
    jewels::Uuid<common::EndpointInstanceId>,
    std::shared_ptr<TcpBridgeClient>,
    jewels::UuidHasher<common::EndpointInstanceId>>;

  /// Constructor
  /// @param[in] memres Memory resource
  /// @param[in] channel_factory Channel factory
  /// @param[in] config TCP bridge configuration
  /// @param[in] current_time Current time
  TcpBridge(
    jewels::memory::MemoryResource memres,
    ShmChannelFactory channel_factory,
    const Tappy<TcpBridgeConfig<>>& config,
    jewels::time::SyncTime current_time = jewels::time::SyncClock::now());

  ~TcpBridge() noexcept = default;

  TcpBridge(const TcpBridge&) = delete;
  TcpBridge& operator=(const TcpBridge&) = delete;
  TcpBridge(TcpBridge&&) = delete;
  TcpBridge& operator=(TcpBridge&&) = delete;

  /// Initialize the bridge
  /// @return Success or failure
  jewels::BinaryOutcome initialize();

  /// Run the TCP bridge loop
  /// @return Success or failure
  jewels::BinaryOutcome run();

  /// Run a single iteration of the TCP bridge loop
  /// @param[in] current_time Current time
  /// @return Success or failure
  jewels::BinaryOutcome run_once(jewels::time::SyncTime current_time);

  /// @return Number of pending servers that are waiting for publishers to create the bridged channel
  [[nodiscard]] size_t get_num_pending_servers() const;

  /// @return Number of servers that are ready to serve clients
  [[nodiscard]] size_t get_num_servers() const;

  /// @return Number of clients
  [[nodiscard]] size_t get_num_clients() const;

  /// Channel factory accessor
  /// @return Channel factory
  [[nodiscard]] ShmChannelFactory& channel_factory();

  /// Diagnostics state accessor
  /// @return Diagnostics state
  [[nodiscard]] TcpBridgeDiagnosticsState& diagnostics_state();

  /// Open a channel publisher
  /// @param[in] memres Memory resource
  /// @param[in] publish_endpoint Publish endpoint configuration
  /// @param[in] channel_factory Shared memory channel factory
  /// @param[out] publisher Channel publisher
  /// @return Success or failure
  static jewels::BinaryOutcome open_publisher(
    const jewels::memory::MemoryResource& memres,
    const Tappy<common::PublishEndpoint<common::MAX_CHANNEL_NAME_SIZE>>& publish_endpoint,
    ShmChannelFactory& channel_factory,
    jewels::Out<std::shared_ptr<pinion::ShmPublisher>> publisher);

  /// Tell the worker threads to stop running
  void request_stop();

private:
  /// Open a channel subscriber
  ///
  /// @param[in] config Bridge server config
  /// @param[out] subscriber Subscriber pointer, will be null if the channel has not been created
  /// @return Success or failure
  jewels::BinaryOutcome
  open_subscriber(const Tappy<TcpBridgeServerConfig>& config, jewels::Out<std::shared_ptr<ShmSubscriber>> subscriber);

  /// Open channel publishers
  jewels::BinaryOutcome open_publishers();

  /// Initialize the list of pending servers
  void initialize_pending_servers();

  /// Create pending TCP bridge servers
  /// @param[in] current_time Current time
  /// @return Success or failure
  jewels::BinaryOutcome create_pending_bridge_servers(jewels::time::SyncTime current_time);

  /// Create TCP bridge clients
  /// @return Success or failure
  jewels::BinaryOutcome create_bridge_clients();

  /// Publish bridge diagnostics
  /// @param[in] diagnostics_counters Diagnostics counters
  /// @param[in] current_time Current time
  void publish_bridge_diagnostics(
    const TcpBridgeDiagnosticsCounters& diagnostics_counters, jewels::time::SyncTime current_time);

  /// Publish bridge status
  /// @param[in] diagnostics_counters Diagnostics counters
  /// @param[in] report_interval Duration covered by status message
  /// @param[in] current_time Current time
  void publish_bridge_status(
    const TcpBridgeDiagnosticsCounters& diagnostics_counters,
    std::chrono::nanoseconds report_interval,
    jewels::time::SyncTime current_time);

  /// Memory resource
  jewels::memory::MemoryResource memres_;

  /// TCP bridge configuration
  std::shared_ptr<Tappy<TcpBridgeConfig<>>> config_;

  /// Channel factory
  ShmChannelFactory channel_factory_;

  /// Channel subscribers
  scaffolding::ChannelMap subscribers_;

  /// Channel publishers
  scaffolding::ChannelMap publishers_;

  /// EPoll manager
  EPollManager epoll_;

  /// Configuration for bridge servers that are waiting for publishers
  std::pmr::list<jewels::memory::ObjectPtr<const Tappy<TcpBridgeServerConfig>>> pending_servers_;

  /// Bridge servers
  ServerMapType servers_;

  /// Bridge clients
  ClientMapType clients_;

  /// Status publisher
  std::shared_ptr<ShmPublisher> status_publisher_;

  /// Status publisher handle
  std::optional<PublisherHandle> maybe_status_publisher_handle_;

  /// Diagnostics publisher
  std::shared_ptr<ShmPublisher> diagnostics_publisher_;

  /// Diagnostics manager
  std::unique_ptr<diagnostics::ClockworkManager<diagnostics::SignalGroupId::tcp_bridge>> diagnostics_manager_;

  /// Diagnostics counter state
  std::shared_ptr<TcpBridgeDiagnosticsState> diagnostics_state_;

  /// Time of last attempt to open pending bridge servers
  jewels::time::SyncTime last_pending_bridge_server_time_;

  /// Last status report publish time
  jewels::time::SyncTime last_status_report_time_;

  /// Set of channels configured for bulk data transfers
  std::pmr::unordered_set<std::string_view> bulk_data_channels_;
};

} // namespace clockwork::pinion
