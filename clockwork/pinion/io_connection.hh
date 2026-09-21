// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/observer.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <wise_enum.h>

#include <cstdint>
#include <memory>

namespace clockwork::pinion
{

/// Connects an IO endpoint to a pinion endpoint.
class IoConnection
{
public:
  WISE_ENUM_CLASS_MEMBER(
    (Error, uint8_t), unsupported, already_connected, invalid_buffer_layout, reserve_failure, unexpected_endpoint_id)

  IoConnection() = default;

  IoConnection(const IoConnection&) = delete;
  void operator=(const IoConnection&) = delete;

  IoConnection(IoConnection&&) noexcept = default;
  IoConnection& operator=(IoConnection&&) noexcept = default;

  virtual ~IoConnection() = default;

  /// Try to connect a subscriber.
  /// @param subscriber A subscriber handle.
  [[nodiscard]] virtual jewels::expected<jewels::memory::NonNullSharedPtr<pinion::Observer>, Error> connect_subscriber(
    jewels::Uuid<common::EndpointClassId> /*endpoint_id*/, std::shared_ptr<pinion::AbstractChannel> /*subscriber*/);

  /// Try to connect a publisher.
  /// @param publisher A publisher handle.
  [[nodiscard]] virtual jewels::expected<void, Error>
    connect_publisher(jewels::Uuid<common::EndpointClassId> /*endpoint_id*/, pinion::PublisherHandle /*publisher*/);

  /// Try to connect a diagnostics publisher.
  /// @param publisher A publisher handle.
  [[nodiscard]] virtual jewels::expected<void, Error>
  connect_diagnostics(jewels::Uuid<common::EndpointInstanceId> endpoint, pinion::PublisherHandle /*publisher*/);
};

} // namespace clockwork::pinion
