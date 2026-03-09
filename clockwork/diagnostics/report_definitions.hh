// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <wise_enum.h>

#include <cstdint>

namespace clockwork::diagnostics
{

enum class SignalId : uint8_t
{
  mixer_configured,
  a2b_configured,
  is_failed,
  drop_count,
  injected_a,
  injected_b,
  max_latency_ms,
  failed_discards,
  failed_sends,
  closed_socket_count,
  failed_reservations,
  failed_recvs,
  failed_commits,
  malformed_messages,
  client_socket_errors,
  progress_errors,
  epoll_errors,
  status_errors,
  max_bulk_data_latency_ms,
};

WISE_ENUM_CLASS((SignalGroupId, uint8_t), fault_injector_a, fault_injector_b, microphone_driver, tcp_bridge)

enum class SignalGroupInstanceType : uint8_t
{
  a,
  interior
};

template <auto group_id>
struct SignalGroup
{
  using InstanceType = SignalGroupInstanceType;
};

} // namespace clockwork::diagnostics
