// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/tests/support/test_message_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/std/span.hh"

#include <catch2/catch_test_macros.hpp>

#include <span>

namespace clockwork::pinion
{

struct DoesNotHaveClear
{
  bool value{};
};

struct ObservableClear
{
  void clear()
  {
    cleared = true;
  }
  bool cleared{};
};

TEST_CASE("InitMessageData")
{
  STATIC_REQUIRE(detail::HasClearMethod<Tappy<test::TestMessage>>);
  STATIC_REQUIRE(detail::HasClearMethod<ObservableClear>);
  STATIC_REQUIRE(!detail::HasClearMethod<DoesNotHaveClear>);

  SECTION("Validate call of clear")
  {
    ObservableClear observable{};
    REQUIRE(!observable.cleared);
    detail::InitMessageData<ObservableClear>::clear(std::as_writable_bytes(jewels::as_single_item_span(observable)));
    REQUIRE(observable.cleared);
  }

  SECTION("Init with clear")
  {
    Tappy<test::TestMessage> value{};
    value.set_test_field(123);
    detail::InitMessageData<Tappy<test::TestMessage>>::clear(
      std::as_writable_bytes(jewels::as_single_item_span(value)));
    REQUIRE(value.get_test_field() == 0);
  }

  SECTION("Init without clear")
  {
    DoesNotHaveClear value{};
    value.value = true;
    detail::InitMessageData<DoesNotHaveClear>::clear(std::as_writable_bytes(jewels::as_single_item_span(value)));
    REQUIRE(!value.value);
  }
}

} // namespace clockwork::pinion
