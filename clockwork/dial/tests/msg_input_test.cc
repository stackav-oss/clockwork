// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/msg_input.hh"
#include "jewels/container/circular_buffer.hh"

#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <utility>
#include <vector>

namespace clockwork
{
namespace
{
struct Foo
{
};
constexpr size_t max_size = 10U;

using FooMsgInput = MessageInputDialWithCursorControl<Foo, max_size, 0, 0>;
using ContainerType = jewels::container::CircularBuffer<detail::MsgPolicy<Foo>, std::span<const Foo*, max_size>>;

struct TestView
{
  std::array<const Foo*, max_size> underlying_buffer{};
  ContainerType circular_buffer;

  TestView()
    : circular_buffer{std::in_place, std::span(underlying_buffer)}
  {
  }

  FooMsgInput make_input()
  {
    const typename FooMsgInput::ViewType view{circular_buffer};
    auto cursor = view.begin();
    auto first_new = view.begin();
    return FooMsgInput{view, cursor, first_new};
  }
};

} // namespace

TEST_CASE("View on Empty CircularBuffer")
{
  TestView test_buffer;
  auto dial = test_buffer.make_input();
  REQUIRE(dial.num_messages_skipped() == 0U);
  auto view = dial.get_view();
  REQUIRE(view.empty());
  REQUIRE(view.begin() == view.end());
  REQUIRE(dial.get_cursor() == view.end());
  REQUIRE(dial.get_first_new() == view.end());

  auto cursor_view = dial.get_cursor_view();
  REQUIRE(cursor_view.empty());

  auto new_msgs_view = dial.get_new_msgs_view();
  REQUIRE(new_msgs_view.empty());
  REQUIRE(new_msgs_view.begin() == new_msgs_view.end());
}

TEST_CASE("View on non-empty CircularBuffers")
{
  const size_t size = GENERATE(1U, max_size - 1U);
  TestView test_buffer;
  std::vector<Foo> foos(size);
  for (auto i = 0U; i < size; ++i)
  {
    test_buffer.circular_buffer.emplace_back(&foos[i]);
  }
  auto dial = test_buffer.make_input();
  REQUIRE(dial.num_messages_skipped() == 0U);
  auto view = dial.get_view();
  REQUIRE_FALSE(view.empty());
  REQUIRE(view.size() == size);
  auto begin = view.begin();
  REQUIRE(&*begin == foos.data());
  REQUIRE(dial.get_cursor() == begin);
  REQUIRE(dial.get_first_new() == begin);
  REQUIRE(begin + static_cast<int64_t>(size) == view.end());
  auto idx = 0U;
  for (const auto& foo : view)
  {
    REQUIRE(&foo == &foos[idx]);
    ++idx;
  }

  auto cursor_view = dial.get_cursor_view();
  REQUIRE(cursor_view.size() == size);
  REQUIRE(cursor_view.begin() == begin);

  auto new_msgs_view = dial.get_new_msgs_view();
  REQUIRE(new_msgs_view.size() == size);
  REQUIRE(new_msgs_view.begin() == begin);
}

} // namespace clockwork
