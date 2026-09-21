// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/utility/string_param.hh"

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
  pre_launch_failure,
  process_not_running,
  process_exited,
  process_crashed,
};

WISE_ENUM_CLASS(
  (SignalGroupId, uint8_t), fault_injector_a, fault_injector_b, microphone_driver, tcp_bridge, simplelaunch)

enum class SignalGroupInstanceType : uint8_t
{
  a,
  b,
  interior
};

/// Helper class for getting SignalGroupId values from string template parameters
template <jewels::StringParam group_id_str>
struct SignalGroupIdParam
{
  // Note that it will only compile if group_id_str is a member of the enum.  Otherwise the
  // compiler will say it's not a constant expression which is a little confusing but means
  // that the .value() would throw due to the string not being found.
  static constexpr SignalGroupId value{wise_enum::from_string<SignalGroupId>(group_id_str).value()};
};

template <auto group_id>
struct SignalGroup
{
  using InstanceType = SignalGroupInstanceType;
};

} // namespace clockwork::diagnostics
