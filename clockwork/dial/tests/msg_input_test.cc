// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/msg_input.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <vector>

namespace clockwork
{
namespace
{
struct Foo
{
};
constexpr size_t max_size = 10U;

using FooMsgInput = MessageInputDial<Foo, max_size, 0, 0, true>;

struct TestView
{
  std::array<FooMsgInput::ViewItem, max_size> underlying_buffer{};
  size_t count{0};

  void add(const Foo* ptr)
  {
    underlying_buffer.at(count++).message = ptr;
  }

  FooMsgInput make_input()
  {
    const typename FooMsgInput::ViewType view{underlying_buffer.data(), count};
    auto cursor = view.begin();
    auto first_new = view.begin();
    return FooMsgInput{view, cursor, first_new};
  }
};

} // namespace

TEST_CASE("View on Empty Buffer")
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

TEST_CASE("View on non-empty buffers")
{
  const size_t size = GENERATE(1U, max_size - 1U);
  TestView test_buffer;
  std::vector<Foo> foos(size);
  for (auto i = 0U; i < size; ++i)
  {
    test_buffer.add(&foos[i]);
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

using SeqnoMsgInput = MessageInputDial<Foo, max_size, 0, 0, true, true>;

struct SeqnoTestView
{
  std::array<SeqnoMsgInput::ViewItem, max_size> underlying_buffer{};
  size_t count{0};

  void add(const Foo* ptr, uint64_t seqno)
  {
    underlying_buffer.at(count).message = ptr;
    underlying_buffer.at(count).seqno = seqno;
    ++count;
  }

  SeqnoMsgInput make_input(size_t msg_count)
  {
    const typename SeqnoMsgInput::ViewType view{underlying_buffer.data(), msg_count};
    auto cursor = view.begin();
    auto first_new = view.begin();
    return SeqnoMsgInput{view, cursor, first_new};
  }

  SeqnoMsgInput make_input_with_skip(size_t msg_count, size_t skip_count)
  {
    const typename SeqnoMsgInput::ViewType view{underlying_buffer.data(), msg_count};
    auto cursor = view.begin();
    auto first_new = view.begin();
    return SeqnoMsgInput{view, cursor, first_new, skip_count};
  }
};

TEST_CASE("Seqno: empty view has no sequence numbers to query")
{
  SeqnoTestView test_buffer;
  auto dial = test_buffer.make_input(0);
  auto view = dial.get_view();
  REQUIRE(view.empty());
}

TEST_CASE("Seqno: get_sequence_number returns correct values")
{
  const size_t size = GENERATE(1U, 3U, max_size);
  SeqnoTestView test_buffer;
  std::vector<Foo> foos(size);

  for (auto i = 0U; i < size; ++i)
  {
    test_buffer.add(&foos[i], 100UL + i);
  }

  auto dial = test_buffer.make_input(size);
  auto view = dial.get_view();
  REQUIRE(view.size() == size);

  for (auto i = 0U; i < size; ++i)
  {
    auto iter = std::next(view.begin(), static_cast<int64_t>(i));
    REQUIRE(dial.get_sequence_number(iter) == 100UL + i);
  }
}

TEST_CASE("Seqno: sequence numbers with skip count")
{
  constexpr size_t size = 3U;
  constexpr size_t skip = 5U;
  SeqnoTestView test_buffer;
  std::vector<Foo> foos(size);

  for (auto i = 0U; i < size; ++i)
  {
    test_buffer.add(&foos[i], 42UL + i);
  }

  auto dial = test_buffer.make_input_with_skip(size, skip);
  REQUIRE(dial.num_messages_skipped() == skip);

  auto view = dial.get_view();
  REQUIRE(view.size() == size);

  for (auto i = 0U; i < size; ++i)
  {
    auto iter = std::next(view.begin(), static_cast<int64_t>(i));
    REQUIRE(dial.get_sequence_number(iter) == 42UL + i);
  }
}

TEST_CASE("Seqno: disconnected input")
{
  SeqnoTestView test_buffer;
  const typename SeqnoMsgInput::ViewType view{test_buffer.underlying_buffer.data(), 0};
  auto dial = SeqnoMsgInput{view, view.end(), view.end(), false};
  REQUIRE_FALSE(dial.connected());
  REQUIRE(dial.get_view().empty());
}

} // namespace clockwork
