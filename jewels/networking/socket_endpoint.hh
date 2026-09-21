// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/base.h>

#include <cstdint>
#include <memory_resource>
#include <string>

namespace jewels::networking
{

/// A host and port.
struct SocketEndpoint
{
  /// Host
  std::pmr::string host;
  /// Port
  uint16_t port;
};

} // namespace jewels::networking

template <>
struct fmt::formatter<jewels::networking::SocketEndpoint>
{
  static constexpr fmt::format_parse_context::iterator parse(fmt::format_parse_context& ctx);

  static fmt::format_context::iterator
  format(const jewels::networking::SocketEndpoint& endpoint, fmt::format_context& ctx);
};

#include "jewels/networking/socket_endpoint.inl"
