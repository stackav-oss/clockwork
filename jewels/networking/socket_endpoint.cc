// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/networking/socket_endpoint.hh"

#include <fmt10/base.h>

#include <string>

fmt::format_context::iterator fmt::formatter<jewels::networking::SocketEndpoint>::format(
  const jewels::networking::SocketEndpoint& endpoint, ::fmt::format_context& ctx)
{
  return fmt::format_to(ctx.out(), R"({}:{})", endpoint.host, endpoint.port);
}
