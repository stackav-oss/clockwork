// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/onboard/clockwork_message_handle.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/buffer_index.hh"
#include "clockwork/pinion/tests/support/mock_buffer.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_test_macros.hpp>

#include <iterator>
#include <span>

namespace clockwork_logging::onboard
{
namespace
{

TEST_CASE("Clockwork message handle")
{
  constexpr clockwork::pinion::BufferLayout layout{
    .num_slots = 2UL,
    .message_size = 1234UL,
  };

  clockwork::pinion::support::BufferStorage<layout> buffer_storage{};
  const auto storage_span =
    as_writable_bytes(jewels::as_single_item_span(buffer_storage))
      .first(sizeof(buffer_storage) - clockwork::pinion::support::storage_trail_padding<layout>);
  auto maybe_buffer = clockwork::pinion::Buffer::try_make(storage_span, layout);
  REQUIRE(maybe_buffer);
  auto buffer = *maybe_buffer;

  REQUIRE(buffer.begin() == buffer.end());

  const auto buffer_iterator1 = buffer.end();
  REQUIRE(buffer.increment_head(clockwork::pinion::BufferIndex{0U}, 1UL));
  const ClockworkMessageHandle message_handle1{jewels::memory::make_non_null_from_ref(buffer), buffer_iterator1};
  REQUIRE(message_handle1.is_valid());
  REQUIRE(message_handle1.get_buffer_iterator() == buffer_iterator1);

  const auto buffer_iterator2 = buffer.end();
  REQUIRE(buffer.increment_head(clockwork::pinion::BufferIndex{1U}, 1UL));
  const ClockworkMessageHandle message_handle2{jewels::memory::make_non_null_from_ref(buffer), buffer_iterator2};
  REQUIRE(message_handle1.is_valid());
  REQUIRE(message_handle2.is_valid());

  REQUIRE(buffer.increment_tail(clockwork::pinion::BufferIndex{0U}, 1UL));
  const auto buffer_iterator3 = buffer.end();
  REQUIRE(buffer.increment_head(clockwork::pinion::BufferIndex{2U}, 1UL));
  const ClockworkMessageHandle message_handle3{jewels::memory::make_non_null_from_ref(buffer), buffer_iterator3};
  REQUIRE_FALSE(message_handle1.is_valid());
  REQUIRE(message_handle2.is_valid());
  REQUIRE(message_handle3.is_valid());

  REQUIRE(buffer.increment_tail(clockwork::pinion::BufferIndex{1U}, 1UL));
  const auto buffer_iterator4 = buffer.end();
  REQUIRE(buffer.increment_head(clockwork::pinion::BufferIndex{3U}, 1UL));
  const ClockworkMessageHandle message_handle4{jewels::memory::make_non_null_from_ref(buffer), buffer_iterator4};
  REQUIRE_FALSE(message_handle1.is_valid());
  REQUIRE_FALSE(message_handle2.is_valid());
  REQUIRE(message_handle3.is_valid());
  REQUIRE(message_handle4.is_valid());

  REQUIRE(buffer.increment_tail(clockwork::pinion::BufferIndex{2U}, 1UL));
  REQUIRE_FALSE(message_handle1.is_valid());
  REQUIRE_FALSE(message_handle2.is_valid());
  REQUIRE_FALSE(message_handle3.is_valid());
  REQUIRE(message_handle4.is_valid());

  REQUIRE(buffer.increment_tail(clockwork::pinion::BufferIndex{3U}, 1UL));
  REQUIRE_FALSE(message_handle1.is_valid());
  REQUIRE_FALSE(message_handle2.is_valid());
  REQUIRE_FALSE(message_handle3.is_valid());
  REQUIRE_FALSE(message_handle4.is_valid());
}

} // namespace
} // namespace clockwork_logging::onboard
