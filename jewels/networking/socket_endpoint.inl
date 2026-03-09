// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
// IWYU pragma: private, include "jewels/networking/socket_endpoint.hh"

#pragma once

#include "jewels/networking/socket_endpoint.hh"

#include <fmt/base.h>

constexpr fmt::format_parse_context::iterator
fmt::formatter<jewels::networking::SocketEndpoint>::parse(fmt::format_parse_context& ctx)
{
  return ctx.begin();
}
