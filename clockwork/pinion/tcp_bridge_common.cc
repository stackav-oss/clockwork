// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/tcp_bridge_common.hh"

#include "jewels/container/tap/var_string.hh"

#include <algorithm>
#include <compare>
#include <utility>

namespace clockwork::pinion
{

void update_client_server_counters(
  uint64_t message_bytes,
  uint64_t compressed_message_bytes,
  std::chrono::nanoseconds receive_latency,
  std::chrono::nanoseconds transfer_time,
  std::chrono::nanoseconds compression_time,
  std::chrono::nanoseconds bridge_latency,
  TcpBridgeClientServerCounters& counters)
{
  ++counters.message_count;
  counters.message_bytes += message_bytes;
  counters.compressed_message_bytes += compressed_message_bytes;
  counters.total_receive_latency += receive_latency;
  counters.max_receive_latency = std::max(counters.max_receive_latency, receive_latency);
  counters.total_transfer_time += transfer_time;
  counters.max_transfer_time = std::max(counters.max_transfer_time, transfer_time);
  counters.total_compression_time += compression_time;
  counters.max_compression_time = std::max(counters.max_compression_time, compression_time);
  counters.total_bridge_latency += bridge_latency;
  counters.max_bridge_latency = std::max(counters.max_bridge_latency, bridge_latency);
}

void combine_client_server_counters(const TcpBridgeClientServerCounters& source, TcpBridgeClientServerCounters& dest)
{
  dest.message_count += source.message_count;
  dest.message_bytes += source.message_bytes;
  dest.compressed_message_bytes += source.compressed_message_bytes;
  dest.total_receive_latency += source.total_receive_latency;
  dest.max_receive_latency = std::max(dest.max_receive_latency, source.max_receive_latency);
  dest.total_transfer_time += source.total_transfer_time;
  dest.max_transfer_time = std::max(dest.max_transfer_time, source.max_transfer_time);
  dest.total_compression_time += source.total_compression_time;
  dest.max_compression_time = std::max(dest.max_compression_time, source.max_compression_time);
  dest.total_bridge_latency += source.total_bridge_latency;
  dest.max_bridge_latency = std::max(dest.max_bridge_latency, source.max_bridge_latency);
}

void store_client_server_counters(
  std::string_view channel_name,
  const TcpBridgeClientServerCounters& source,
  std::chrono::nanoseconds interval,
  Tappy<BridgeClientServerCounters>& dest)
{
  const auto real_time = std::chrono::duration<float>(interval);
  dest.get_underlying_channel_name().set_truncate(channel_name);
  dest.set_message_rate_hz(static_cast<float>(source.message_count) / real_time.count());
  dest.set_data_rate_bps(static_cast<float>(source.message_bytes) / real_time.count());
  dest.set_compressed_data_rate_bps(static_cast<float>(source.compressed_message_bytes) / real_time.count());
  if (source.message_count != 0U)
  {
    dest.set_compression_ratio(
      static_cast<float>(source.message_bytes) / static_cast<float>(source.compressed_message_bytes));
    dest.set_average_receive_latency(source.total_receive_latency / static_cast<int64_t>(source.message_count));
    dest.set_max_receive_latency(source.max_receive_latency);
    dest.set_average_transfer_time(source.total_transfer_time / static_cast<int64_t>(source.message_count));
    dest.set_max_transfer_time(source.max_transfer_time);
    dest.set_average_compression_time(source.total_compression_time / static_cast<int64_t>(source.message_count));
    dest.set_max_compression_time(source.max_compression_time);
    dest.set_average_bridge_latency(source.total_bridge_latency / static_cast<int64_t>(source.message_count));
    dest.set_max_bridge_latency(source.max_bridge_latency);
  }
  else
  {
    dest.set_compression_ratio(1.0);
    dest.set_average_receive_latency(std::chrono::nanoseconds(0));
    dest.set_max_receive_latency(std::chrono::nanoseconds(0));
    dest.set_average_transfer_time(std::chrono::nanoseconds(0));
    dest.set_max_transfer_time(std::chrono::nanoseconds(0));
    dest.set_average_compression_time(std::chrono::nanoseconds(0));
    dest.set_max_compression_time(std::chrono::nanoseconds(0));
    dest.set_average_bridge_latency(std::chrono::nanoseconds(0));
    dest.set_max_bridge_latency(std::chrono::nanoseconds(0));
  }
}

TcpBridgeDiagnosticsCounters TcpBridgeDiagnosticsState::get_and_reset_counters()
{
  std::lock_guard guard{mutex_};
  TcpBridgeDiagnosticsCounters counters{};
  std::swap(counters, counters_);
  return counters;
}

void TcpBridgeDiagnosticsState::increment_drop_count(size_t count)
{
  std::lock_guard guard{mutex_};
  counters_.drop_count += count;
}

void TcpBridgeDiagnosticsState::increment_failed_sends(size_t count)
{
  std::lock_guard guard{mutex_};
  counters_.failed_sends += count;
}

void TcpBridgeDiagnosticsState::increment_closed_socket_count(size_t count)
{
  std::lock_guard guard{mutex_};
  counters_.closed_socket_count += count;
}

void TcpBridgeDiagnosticsState::increment_failed_recvs(size_t count)
{
  std::lock_guard guard{mutex_};
  counters_.failed_recvs += count;
}

void TcpBridgeDiagnosticsState::increment_failed_reservations(size_t count)
{
  std::lock_guard guard{mutex_};
  counters_.failed_reservations += count;
}

void TcpBridgeDiagnosticsState::increment_malformed_messages(size_t count)
{
  std::lock_guard guard{mutex_};
  counters_.malformed_messages += count;
}

void TcpBridgeDiagnosticsState::increment_failed_commits(size_t count)
{
  std::lock_guard guard{mutex_};
  counters_.failed_commits += count;
}

void TcpBridgeDiagnosticsState::increment_failed_discards(size_t count)
{
  std::lock_guard guard{mutex_};
  counters_.failed_discards += count;
}

void TcpBridgeDiagnosticsState::increment_client_socket_errors(size_t count)
{
  std::lock_guard guard{mutex_};
  counters_.client_socket_errors += count;
}

void TcpBridgeDiagnosticsState::increment_progress_errors(size_t count)
{
  std::lock_guard guard{mutex_};
  counters_.progress_errors += count;
}

void TcpBridgeDiagnosticsState::increment_epoll_errors(size_t count)
{
  std::lock_guard guard{mutex_};
  counters_.epoll_errors += count;
}

void TcpBridgeDiagnosticsState::increment_status_errors(size_t count)
{
  std::lock_guard guard{mutex_};
  counters_.status_errors += count;
}

void TcpBridgeDiagnosticsState::update_max_bridge_latency(
  std::chrono::nanoseconds latency, std::string_view channel_name)
{
  std::lock_guard guard{mutex_};
  if (latency > counters_.max_bridge_latency)
  {
    counters_.max_bridge_latency = latency;
    counters_.max_latency_channel_name = channel_name;
  }
}

void TcpBridgeDiagnosticsState::update_max_bridge_bulk_data_latency(
  std::chrono::nanoseconds latency, std::string_view channel_name)
{
  std::lock_guard guard{mutex_};
  if (latency > counters_.max_bridge_bulk_data_latency)
  {
    counters_.max_bridge_bulk_data_latency = latency;
    counters_.max_bulk_data_latency_channel_name = channel_name;
  }
}

} // namespace clockwork::pinion
