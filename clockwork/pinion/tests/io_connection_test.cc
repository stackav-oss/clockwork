// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/io_connection.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <memory_resource>

namespace clockwork::pinion
{

TEST_CASE("Base IoConnection")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  auto channel = std::make_shared<InMemoryChannel<int, 1UL, false>>(memres);
  IoConnection io_stream{};
  REQUIRE(
    io_stream.connect_publisher({}, channel->make_publisher(0UL)) ==
    jewels::unexpected{IoConnection::Error::unsupported});
  REQUIRE(io_stream.connect_subscriber({}, channel) == jewels::unexpected{IoConnection::Error::unsupported});
  REQUIRE(
    io_stream.connect_diagnostics({}, channel->make_publisher(0UL)) ==
    jewels::unexpected{IoConnection::Error::unsupported});
}

} // namespace clockwork::pinion
