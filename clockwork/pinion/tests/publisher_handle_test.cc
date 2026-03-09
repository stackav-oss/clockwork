// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/buffer_index.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tests/support/mock_buffer.hh"
#include "clockwork/pinion/tests/support/pub_sub.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/time/sync_time.hh"

#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <utility>

namespace clockwork::pinion
{

/// Fixed timestamp for publish timestamp.
inline constexpr auto fake_publish_time{jewels::time::SyncTime{std::chrono::nanoseconds{12345}}};

TEST_CASE("ReservationState")
{
  SECTION("Default constructor")
  {
    const ReservationState state{};
    REQUIRE(state == ReservationState::State::discard);
  }

  SECTION("set / get / compare")
  {
    ReservationState state{};
    state.set(ReservationState::State::ignore);
    REQUIRE(state.get() == ReservationState::State::ignore);
    REQUIRE(state == ReservationState::State::ignore);

    state.set(ReservationState::State::commit);
    REQUIRE(state.get() == ReservationState::State::commit);
    REQUIRE(state == ReservationState::State::commit);

    state.set(ReservationState::State::discard);
    REQUIRE(state.get() == ReservationState::State::discard);
    REQUIRE(state == ReservationState::State::discard);
  }

  SECTION("Move constructor")
  {
    // Wrap in an optional so clang doesn't complain about use after move.
    std::optional<ReservationState> moved_from{};
    moved_from.emplace();
    // change from default first
    moved_from->set(ReservationState::State::commit);

    auto moved_to{std::move(*moved_from)};

    // Intentionally using the moved-from object to validate the state.
    REQUIRE(*moved_from == ReservationState::State::ignore);
    REQUIRE(moved_to == ReservationState::State::commit);
  }
}

TEST_CASE("Publisher handle") // NOLINT(readability-function-size) This test is a bit long due to multiple sections
{
  using testing::TestObserver;

  constexpr BufferLayout layout{
    .num_slots = 2UL,
    .message_size = 8UL,
    .is_published_once = false,
  };
  support::BufferStorage<layout> buffer_storage{};
  support::zero_buffer_storage(buffer_storage);
  const auto storage_span = as_writable_bytes(jewels::as_single_item_span(buffer_storage))
                              .first(sizeof(buffer_storage) - support::storage_trail_padding<layout>);
  auto maybe_buffer = Buffer::try_make(storage_span, layout);
  REQUIRE(maybe_buffer);
  auto buffer = *maybe_buffer;

  const SubscriberHandle sub_handle{jewels::memory::make_non_null_from_ref(buffer)};
  constexpr auto num_observers{2UL};
  PublisherHandle pub_handle{
    jewels::memory::make_non_null_from_ref(buffer),
    num_observers,
    jewels::memory::MemoryResource{std::pmr::new_delete_resource()}};
  REQUIRE(pub_handle.buffer().layout().num_slots == layout.num_slots);
  REQUIRE(pub_handle.buffer().layout().message_size == layout.message_size);

  REQUIRE(std::ranges::empty(sub_handle.available()));

  SECTION("Check state transitions")
  {
    auto reservation = pub_handle.reserve();
    REQUIRE(reservation);
    REQUIRE(reservation->state() == ReservationState::State::discard);
    reservation->mark_for_commit();
    REQUIRE(reservation->state() == ReservationState::State::commit);
    reservation->mark_for_discard();
    REQUIRE(reservation->state() == ReservationState::State::discard);
  }

  SECTION("Move constructor")
  {
    auto reservation = pub_handle.reserve();
    REQUIRE(reservation);
    SECTION("Move with discard")
    {
      reservation->mark_for_discard();
      auto moved_to{std::move(*reservation)};
      REQUIRE(moved_to.state() == ReservationState::State::discard);
    }
    SECTION("Move with commit")
    {
      reservation->mark_for_commit();
      auto moved_to{std::move(*reservation)};
      REQUIRE(moved_to.state() == ReservationState::State::commit);
      REQUIRE(moved_to.discard());
    }
    SECTION("Moved from")
    {
      auto moved_to{std::move(*reservation)};
      REQUIRE(moved_to.state() == ReservationState::State::discard);
      REQUIRE(reservation->state() == ReservationState::State::ignore);
    }
  }

  SECTION("Destructor")
  {
    TestObserver observer;
    REQUIRE(pub_handle.add_observer(jewels::memory::make_non_null_from_ref(observer)));

    auto reservation = pub_handle.reserve();
    REQUIRE(reservation);

    SECTION("Failed discard in destructor")
    {
      REQUIRE(reservation->discard());
      reservation->mark_for_discard();
      REQUIRE_THROWS([&] { auto copied{*std::move(reservation)}; }());
      CHECK(!observer.event);
    }
    SECTION("Commit is ignored in destructor")
    {
      reservation->mark_for_commit();
      REQUIRE_NOTHROW([&] { auto copied{*std::move(reservation)}; }());
      CHECK(!observer.event);
    }
  }

  SECTION("Duplicate reservation")
  {
    auto reservation = pub_handle.reserve();
    REQUIRE(reservation);
    REQUIRE(pub_handle.reserve() == jewels::unexpected{ReserveError::existing_reservation});
  }

  SECTION("Corrupted head")
  {
    auto reservation = pub_handle.reserve();
    REQUIRE(reservation);
    REQUIRE(buffer.increment_head(BufferIndex{0UL}, 1UL) == BufferIndex{1UL});
    REQUIRE(reservation->commit(fake_publish_time) == jewels::unexpected{WriteError::unexpected_head});
  }

  SECTION("Commit")
  {
    SECTION("Mark then process")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      reservation->mark_for_commit();
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
      REQUIRE(reservation->process(fake_publish_time));
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 1UL);
    }
    SECTION("Force commit")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
      REQUIRE(reservation->commit(fake_publish_time));
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 1UL);
    }
    SECTION("Header")
    {
      auto reservation0 = pub_handle.reserve();
      REQUIRE(reservation0);
      REQUIRE(reservation0->slot().header()->sequence_number == 0UL);
      REQUIRE(reservation0->slot().header()->publish_timestamp == 0L);
      REQUIRE(reservation0->slot().header()->source_commit_timestamp == 0L);
      REQUIRE(reservation0->slot().header()->latest_commit_timestamp == 0L);
      const auto commit_time0 = jewels::time::SyncClock::now();
      REQUIRE(reservation0->commit(fake_publish_time));
      REQUIRE(reservation0->slot().header()->sequence_number == 0UL);
      REQUIRE(reservation0->slot().header()->publish_timestamp == fake_publish_time.time_since_epoch().count());
      REQUIRE(reservation0->slot().header()->source_commit_timestamp >= commit_time0.time_since_epoch().count());
      REQUIRE(
        reservation0->slot().header()->latest_commit_timestamp ==
        reservation0->slot().header()->source_commit_timestamp);

      auto reservation1 = pub_handle.reserve();
      REQUIRE(reservation1);
      REQUIRE(reservation1->slot().header()->sequence_number == 0UL);
      REQUIRE(reservation1->slot().header()->publish_timestamp == 0L);
      REQUIRE(reservation1->slot().header()->source_commit_timestamp == 0L);
      REQUIRE(reservation1->slot().header()->latest_commit_timestamp == 0L);
      const auto commit_time1 = jewels::time::SyncClock::now();
      REQUIRE(reservation1->commit(fake_publish_time + std::chrono::nanoseconds{1}));
      REQUIRE(reservation1->slot().header()->sequence_number == 1UL);
      REQUIRE(reservation1->slot().header()->publish_timestamp == fake_publish_time.time_since_epoch().count() + 1L);
      REQUIRE(reservation1->slot().header()->source_commit_timestamp >= commit_time1.time_since_epoch().count());
      REQUIRE(
        reservation1->slot().header()->latest_commit_timestamp ==
        reservation1->slot().header()->source_commit_timestamp);
    }
    SECTION("Header with sequence number and source commit time")
    {
      constexpr uint64_t fake_sequence_number1 = 123456U;
      constexpr auto fake_commit_time1{jewels::time::SyncTime{std::chrono::nanoseconds{23456}}};
      constexpr uint64_t fake_sequence_number2 = 234567U;
      constexpr auto fake_commit_time2{jewels::time::SyncTime{std::chrono::nanoseconds{34578}}};
      auto reservation0 = pub_handle.reserve();
      REQUIRE(reservation0);
      REQUIRE(reservation0->slot().header()->sequence_number == 0UL);
      REQUIRE(reservation0->slot().header()->publish_timestamp == 0UL);
      REQUIRE(reservation0->slot().header()->source_commit_timestamp == 0L);
      REQUIRE(reservation0->slot().header()->latest_commit_timestamp == 0L);
      const auto commit_time0 = jewels::time::SyncClock::now();
      REQUIRE(reservation0->commit(fake_publish_time, fake_sequence_number1, fake_commit_time1));
      REQUIRE(reservation0->slot().header()->sequence_number == fake_sequence_number1);
      REQUIRE(reservation0->slot().header()->publish_timestamp == fake_publish_time.time_since_epoch().count());
      REQUIRE(reservation0->slot().header()->source_commit_timestamp == fake_commit_time1.time_since_epoch().count());
      REQUIRE(reservation0->slot().header()->latest_commit_timestamp >= commit_time0.time_since_epoch().count());

      auto reservation1 = pub_handle.reserve();
      REQUIRE(reservation1);
      REQUIRE(reservation1->slot().header()->sequence_number == 0UL);
      REQUIRE(reservation1->slot().header()->publish_timestamp == 0UL);
      REQUIRE(reservation1->slot().header()->source_commit_timestamp == 0L);
      REQUIRE(reservation1->slot().header()->latest_commit_timestamp == 0L);
      const auto commit_time1 = jewels::time::SyncClock::now();
      REQUIRE(reservation1->commit(
        fake_publish_time + std::chrono::nanoseconds{1}, fake_sequence_number2, fake_commit_time2));
      REQUIRE(reservation1->slot().header()->sequence_number == fake_sequence_number2);
      REQUIRE(reservation1->slot().header()->publish_timestamp == fake_publish_time.time_since_epoch().count() + 1L);
      REQUIRE(reservation1->slot().header()->source_commit_timestamp == fake_commit_time2.time_since_epoch().count());
      REQUIRE(reservation1->slot().header()->latest_commit_timestamp >= commit_time1.time_since_epoch().count());
    }
    SECTION("Twice fails")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(reservation->commit(fake_publish_time));
      REQUIRE_FALSE(reservation->commit(fake_publish_time) == jewels::unexpected{WriteError::unexpected_head});
    }
    SECTION("After discard fails")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(reservation->discard());
      REQUIRE_FALSE(reservation->commit(fake_publish_time) == jewels::unexpected{WriteError::unexpected_head});
    }
  }

  SECTION("Discard")
  {
    SECTION("Mark then process")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      reservation->mark_for_discard();
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
      REQUIRE(reservation->process(fake_publish_time));
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
    }
    SECTION("Force discard")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
      REQUIRE(reservation->discard());
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
    }
    SECTION("Twice fails")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(reservation->discard());
      REQUIRE_FALSE(reservation->discard() == jewels::unexpected{WriteError::unexpected_head});
    }
    SECTION("After commit fails")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(reservation->commit(fake_publish_time));
      REQUIRE_FALSE(reservation->discard() == jewels::unexpected{WriteError::unexpected_head});
    }
  }

  SECTION("Discard-commit sequencing")
  {
    // Reserve and discard.  Has capacity.
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
      REQUIRE(reservation->slot().header()->sequence_number == 0UL);
      REQUIRE(reservation->discard());
    }

    // Buffer has not changed.
    REQUIRE(std::ranges::empty(sub_handle.available()));
    REQUIRE(buffer.tail() == 0UL);
    REQUIRE(buffer.head() == 0UL);

    // Reserve and commit
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
      REQUIRE(reservation->slot().header()->sequence_number == 0UL);
      REQUIRE(reservation->commit(fake_publish_time));
    }

    // Commit changed the buffer.
    REQUIRE(std::ranges::size(sub_handle.available()) == 1UL);
    REQUIRE(buffer.tail() == 0UL);
    REQUIRE(sub_handle.available().front().header()->sequence_number == 0UL);

    // Reserve and discard.  Has capacity.
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 1UL);
      REQUIRE(reservation->slot().header()->sequence_number == 0UL);
      REQUIRE(reservation->discard());
    }

    // Buffer has not changed.
    REQUIRE(std::ranges::size(sub_handle.available()) == 1UL);
    REQUIRE(buffer.tail() == 0UL);
    REQUIRE(buffer.head() == 1UL);
    REQUIRE(sub_handle.available().front().header()->sequence_number == 0UL);

    // Reserve and commit.  Now full.
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 1UL);
      REQUIRE(reservation->slot().header()->sequence_number == 0UL);
      REQUIRE(reservation->commit(fake_publish_time));
    }

    // Commit changed the buffer.
    REQUIRE(std::ranges::size(sub_handle.available()) == 2UL);
    REQUIRE(buffer.tail() == 0UL);
    REQUIRE(buffer.head() == 2UL);
    REQUIRE(sub_handle.available().front().header()->sequence_number == 0UL);
    REQUIRE(sub_handle.available().back().header()->sequence_number == 1UL);

    // Reserve and discard. No capacity.
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 1UL);
      REQUIRE(buffer.head() == 2UL);
      REQUIRE(reservation->slot().header()->sequence_number == 0UL);
      REQUIRE(std::ranges::size(sub_handle.available()) == 1UL);
      REQUIRE(sub_handle.available().front().header()->sequence_number == 1UL);
      REQUIRE(reservation->discard());
    }

    // Discard changed the buffer.  Oldest element remains hidden.
    REQUIRE(std::ranges::size(sub_handle.available()) == 1UL);
    REQUIRE(buffer.tail() == 1UL);
    REQUIRE(buffer.head() == 2UL);
    REQUIRE(sub_handle.available().front().header()->sequence_number == 1UL);

    // Reserve and commit. Back to full.
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 1UL);
      REQUIRE(buffer.head() == 2UL);
      REQUIRE(reservation->slot().header()->sequence_number == 0UL);
      REQUIRE(std::ranges::size(sub_handle.available()) == 1UL);
      REQUIRE(sub_handle.available().front().header()->sequence_number == 1UL);
      REQUIRE(reservation->commit(fake_publish_time));
    }

    // Commit changed the buffer.
    REQUIRE(std::ranges::size(sub_handle.available()) == 2UL);
    REQUIRE(buffer.tail() == 1UL);
    REQUIRE(buffer.head() == 3UL);
    REQUIRE(sub_handle.available().front().header()->sequence_number == 1UL);
    REQUIRE(sub_handle.available().back().header()->sequence_number == 2UL);

    // Reserve and commit. Stays full.
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 2UL);
      REQUIRE(buffer.head() == 3UL);
      REQUIRE(reservation->slot().header()->sequence_number == 0UL);
      REQUIRE(std::ranges::size(sub_handle.available()) == 1UL);
      REQUIRE(sub_handle.available().front().header()->sequence_number == 2UL);
      REQUIRE(reservation->commit(fake_publish_time));
    }

    // Commit changed the buffer.
    REQUIRE(std::ranges::size(sub_handle.available()) == 2UL);
    REQUIRE(buffer.tail() == 2UL);
    REQUIRE(buffer.head() == 4UL);
    REQUIRE(sub_handle.available().front().header()->sequence_number == 2UL);
    REQUIRE(sub_handle.available().back().header()->sequence_number == 3UL);
  }

  SECTION("Observers")
  {
    SECTION("Too many observers")
    {
      TestObserver observer;
      REQUIRE(pub_handle.add_observer(jewels::memory::make_non_null_from_ref(observer)));
      REQUIRE(pub_handle.add_observer(jewels::memory::make_non_null_from_ref(observer)));
      REQUIRE_FALSE(pub_handle.add_observer(jewels::memory::make_non_null_from_ref(observer)));
    }
    SECTION("One observer")
    {
      TestObserver observer;
      REQUIRE(pub_handle.add_observer(jewels::memory::make_non_null_from_ref(observer)));
      for (int i = 0; i < 3; i++)
      {
        observer.reset();
        auto reservation = pub_handle.reserve();
        REQUIRE(reservation);
        REQUIRE_FALSE(observer.event);
        REQUIRE(reservation->commit(fake_publish_time));
        REQUIRE(observer.event);
        CHECK(observer.event->head == sub_handle.available().back().header()->sequence_number);
        CHECK(observer.event->tail == sub_handle.available().front().header()->sequence_number);
      }
    }
    SECTION("Two observers")
    {
      TestObserver observer_a;
      TestObserver observer_b;
      REQUIRE(pub_handle.add_observer(jewels::memory::make_non_null_from_ref(observer_a)));
      REQUIRE(pub_handle.add_observer(jewels::memory::make_non_null_from_ref(observer_b)));
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE_FALSE(observer_a.event);
      REQUIRE_FALSE(observer_b.event);
      REQUIRE(reservation->commit(fake_publish_time));
      REQUIRE(observer_a.event);
      REQUIRE(observer_b.event);
      CHECK(observer_a.event->head == observer_b.event->head);
      CHECK(observer_a.event->tail == observer_b.event->tail);
    }
  }

  SECTION("Publishable")
  {
    auto reservation = pub_handle.reserve();
    REQUIRE(reservation);

    SECTION("Size mismatch")
    {
      REQUIRE_FALSE(Publishable<uint32_t>::try_make(jewels::memory::make_non_null_from_ref(*reservation)));
      REQUIRE_FALSE(
        Publishable<std::pair<uint64_t, uint64_t>>::try_make(jewels::memory::make_non_null_from_ref(*reservation)));
    }
    SECTION("Valid size")
    {
      auto publishable = Publishable<uint64_t>::try_make(jewels::memory::make_non_null_from_ref(*reservation));
      REQUIRE(publishable);
      REQUIRE(reservation->state() == ReservationState::State::discard);
      publishable->message() = 123456789UL;
      SECTION("Publish")
      {
        publishable->mark_for_publish();
        REQUIRE(reservation->state() == ReservationState::State::commit);
        REQUIRE(std::ranges::empty(sub_handle.available()));
        REQUIRE(reservation->process(fake_publish_time));
        auto message_range = to_message_range<const uint64_t>(sub_handle.available());
        REQUIRE(message_range);
        REQUIRE(std::ranges::size(*message_range) == 1UL);
        REQUIRE(message_range->front() == 123456789UL);
      }
      SECTION("Sim only mark for publish")
      {
        constexpr jewels::time::SyncTime sim_publish_time{std::chrono::nanoseconds{1234567890}};
        publishable->sim_only_mark_for_publish_with_fake_timestamp(sim_publish_time);
        REQUIRE(reservation->state() == ReservationState::State::commit);
        REQUIRE(std::ranges::empty(sub_handle.available()));
        REQUIRE(reservation->process(fake_publish_time));
        auto begin = sub_handle.available().begin();
        REQUIRE(begin->header()->publish_timestamp == sim_publish_time.time_since_epoch().count());
      }
    }
  }
}

TEST_CASE("Batch reservations")
{
  using testing::TestObserver;

  constexpr BufferLayout layout{
    .num_slots = 2UL,
    .message_size = 8UL,
    .is_published_once = false,
  };
  support::BufferStorage<layout> buffer_storage{};
  support::zero_buffer_storage(buffer_storage);
  const auto storage_span = as_writable_bytes(jewels::as_single_item_span(buffer_storage))
                              .first(sizeof(buffer_storage) - support::storage_trail_padding<layout>);
  auto maybe_buffer = Buffer::try_make(storage_span, layout);
  REQUIRE(maybe_buffer);
  auto buffer = *maybe_buffer;

  constexpr auto num_observers{0UL};
  PublisherHandle pub_handle{
    jewels::memory::make_non_null_from_ref(buffer),
    num_observers,
    jewels::memory::MemoryResource{std::pmr::new_delete_resource()}};

  SECTION("Reservation too large")
  {
    REQUIRE(pub_handle.reserve(layout.num_slots));
    REQUIRE(pub_handle.reserve(layout.num_slots + 1UL) == jewels::unexpected{ReserveError::count_too_large});
  }

  constexpr auto reservation_size{2UL};
  SECTION("Slots")
  {
    auto reservation = pub_handle.reserve(reservation_size);
    REQUIRE(reservation);
    REQUIRE(std::ranges::size(reservation->slots()) == 2UL);
  }

  SECTION("Double reservation")
  {
    SECTION("Batch first")
    {
      auto reservation = pub_handle.reserve(reservation_size);
      REQUIRE(reservation);
      SECTION("One reserve")
      {
        REQUIRE(pub_handle.reserve() == jewels::unexpected{ReserveError::existing_reservation});
      }
      SECTION("Batch reserve")
      {
        REQUIRE(pub_handle.reserve(reservation_size) == jewels::unexpected{ReserveError::existing_reservation});
      }
    }
    SECTION("One first")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      SECTION("One reserve")
      {
        REQUIRE(pub_handle.reserve() == jewels::unexpected{ReserveError::existing_reservation});
      }
      SECTION("Batch reserve")
      {
        REQUIRE(pub_handle.reserve(reservation_size) == jewels::unexpected{ReserveError::existing_reservation});
      }
    }
  }

  SECTION("Discard")
  {

    SECTION("Implicit")
    {
      REQUIRE(std::ranges::empty(buffer));
      {
        auto reservation = pub_handle.reserve(reservation_size);
        REQUIRE(reservation);
      }
      REQUIRE(std::ranges::empty(buffer));
    }

    SECTION("Explicit")
    {
      REQUIRE(std::ranges::empty(buffer));
      {
        auto reservation = pub_handle.reserve(reservation_size);
        REQUIRE(reservation);
        REQUIRE(reservation->discard());
      }
      REQUIRE(std::ranges::empty(buffer));
    }
  }

  SECTION("Commit")
  {
    SECTION("Start from empty")
    {
      REQUIRE(buffer.head() == 0UL);
      REQUIRE(buffer.tail() == 0UL);

      {
        auto reservation = pub_handle.reserve(reservation_size);
        REQUIRE(reservation);
        REQUIRE(buffer.head() == 0UL);
        REQUIRE(buffer.tail() == 0UL);

        REQUIRE(std::ranges::empty(buffer));
        REQUIRE(reservation->commit(fake_publish_time));
        REQUIRE(std::ranges::size(buffer) == 2UL);
        REQUIRE(buffer.begin()->header()->sequence_number == 0UL);
        REQUIRE(buffer.begin()->header()->publish_timestamp == fake_publish_time.time_since_epoch().count());
        REQUIRE(std::next(buffer.begin())->header()->sequence_number == 1UL);
        REQUIRE(std::next(buffer.begin())->header()->publish_timestamp == fake_publish_time.time_since_epoch().count());
        REQUIRE(buffer.head() == 2UL);
        REQUIRE(buffer.tail() == 0UL);
      }

      {
        auto reservation = pub_handle.reserve(reservation_size);
        REQUIRE(reservation);
        REQUIRE(buffer.head() == 2UL);
        REQUIRE(buffer.tail() == 2UL);

        const auto next_publish_time = fake_publish_time + std::chrono::nanoseconds{1L};

        REQUIRE(reservation->commit(next_publish_time));
        REQUIRE(std::ranges::size(buffer) == 2UL);
        REQUIRE(buffer.begin()->header()->sequence_number == 2UL);
        REQUIRE(buffer.begin()->header()->publish_timestamp == next_publish_time.time_since_epoch().count());
        REQUIRE(std::next(buffer.begin())->header()->sequence_number == 3UL);
        REQUIRE(std::next(buffer.begin())->header()->publish_timestamp == next_publish_time.time_since_epoch().count());
        REQUIRE(buffer.head() == 4UL);
        REQUIRE(buffer.tail() == 2UL);
      }
    }

    SECTION("Start with one already reserved")
    {
      REQUIRE(buffer.head() == 0UL);
      REQUIRE(buffer.tail() == 0UL);

      {
        // Reserves just one.
        auto reservation = pub_handle.reserve();
        REQUIRE(reservation);
        REQUIRE(buffer.head() == 0UL);
        REQUIRE(buffer.tail() == 0UL);

        REQUIRE(std::ranges::empty(buffer));
        REQUIRE(reservation->commit(fake_publish_time));
        REQUIRE(std::ranges::size(buffer) == 1UL);
        REQUIRE(buffer.head() == 1UL);
        REQUIRE(buffer.tail() == 0UL);
      }
      {
        auto reservation = pub_handle.reserve(reservation_size);
        REQUIRE(reservation);
        REQUIRE(buffer.head() == 1UL);
        REQUIRE(buffer.tail() == 1UL);
      }
    }
  }

  SECTION("Corrupted head")
  {
    REQUIRE(buffer.increment_head(BufferIndex{0UL}, 1UL) == BufferIndex{1UL});
    auto reservation = pub_handle.reserve();
    REQUIRE(reservation);
    REQUIRE(reservation->commit(fake_publish_time) == jewels::unexpected{WriteError::unexpected_head});
  }

  SECTION("Corrupted tail")
  {
    REQUIRE(buffer.increment_tail(BufferIndex{0UL}, 1UL) == BufferIndex{1UL});
    REQUIRE(pub_handle.reserve() == jewels::unexpected{ReserveError::unexpected_tail});
  }
}

TEST_CASE("Processing of multiple publisher reservations")
{
  constexpr BufferLayout layout{
    .num_slots = 2UL,
    .message_size = 8UL,
    .is_published_once = false,
  };
  support::BufferStorage<layout> buffer_storage0{};
  support::BufferStorage<layout> buffer_storage1{};
  support::zero_buffer_storage(buffer_storage0);
  support::zero_buffer_storage(buffer_storage1);
  const auto storage_span0 = as_writable_bytes(jewels::as_single_item_span(buffer_storage0))
                               .first(sizeof(buffer_storage0) - support::storage_trail_padding<layout>);
  const auto storage_span1 = as_writable_bytes(jewels::as_single_item_span(buffer_storage1))
                               .first(sizeof(buffer_storage1) - support::storage_trail_padding<layout>);
  auto maybe_buffer0 = Buffer::try_make(storage_span0, layout);
  REQUIRE(maybe_buffer0);
  auto buffer0 = *maybe_buffer0;
  auto maybe_buffer1 = Buffer::try_make(storage_span1, layout);
  REQUIRE(maybe_buffer1);
  auto buffer1 = *maybe_buffer1;

  PublisherHandle pub_handle0{
    jewels::memory::make_non_null_from_ref(buffer0),
    0UL,
    jewels::memory::MemoryResource{std::pmr::new_delete_resource()}};
  PublisherHandle pub_handle1{
    jewels::memory::make_non_null_from_ref(buffer1),
    0UL,
    jewels::memory::MemoryResource{std::pmr::new_delete_resource()}};

  REQUIRE(buffer0.tail() == 0UL);
  REQUIRE(buffer0.head() == 0UL);
  REQUIRE(buffer1.tail() == 0UL);
  REQUIRE(buffer1.head() == 0UL);
  SECTION("Batch process")
  {
    auto slot0 = pub_handle0.reserve();
    REQUIRE(slot0);
    auto slot1 = pub_handle1.reserve();
    REQUIRE(slot1);

    auto slots = std::array{*std::move(slot0), *std::move(slot1)};
    SECTION("Both commit")
    {
      for (auto& slot : slots)
      {
        slot.mark_for_commit();
      }
      REQUIRE(process_slots(std::span{slots}, fake_publish_time));
      REQUIRE(buffer0.tail() == 0UL);
      REQUIRE(buffer0.head() == 1UL);
      REQUIRE(buffer0.begin()->header()->publish_timestamp == fake_publish_time.time_since_epoch().count());
      REQUIRE(buffer1.tail() == 0UL);
      REQUIRE(buffer1.head() == 1UL);
      REQUIRE(buffer1.begin()->header()->publish_timestamp == fake_publish_time.time_since_epoch().count());
    }
    SECTION("One commits")
    {
      slots.front().mark_for_commit();
      REQUIRE(process_slots(std::span{slots}, fake_publish_time));
      REQUIRE(buffer0.tail() == 0UL);
      REQUIRE(buffer0.head() == 1UL);
      REQUIRE(buffer0.begin()->header()->publish_timestamp == fake_publish_time.time_since_epoch().count());
      REQUIRE(buffer1.tail() == 0UL);
      REQUIRE(buffer1.head() == 0UL);
      REQUIRE(buffer1.begin()->header()->publish_timestamp == 0);
    }
    SECTION("Fails")
    {
      for (auto index = 0UL; index < slots.size(); index++)
      {
        DYNAMIC_SECTION("Fail on index: " << index)
        {
          REQUIRE(slots.at(index).discard());
          slots.at(index).mark_for_commit();
          REQUIRE_FALSE(process_slots(std::span{slots}, fake_publish_time));
        }
      }
    }
  }
  SECTION("Batch discard")
  {
    auto slot0 = pub_handle0.reserve();
    REQUIRE(slot0);
    auto slot1 = pub_handle1.reserve();
    REQUIRE(slot1);

    auto slots = std::array{*std::move(slot0), *std::move(slot1)};
    for (auto& slot : slots)
    {
      slot.mark_for_commit();
    }
    const auto is_discard = [](const auto& slot) { return slot.state() == ReservationState::State::discard; };
    REQUIRE(!std::ranges::any_of(slots, is_discard));
    mark_slots_for_discard(std::span{slots});
    REQUIRE(std::ranges::all_of(slots, is_discard));
  }
}

TEST_CASE("Resume after restart")
{
  constexpr BufferLayout layout{
    .num_slots = 2UL,
    .message_size = 8UL,
    .is_published_once = false,
  };
  support::BufferStorage<layout> buffer_storage{};
  support::zero_buffer_storage(buffer_storage);
  const auto storage_span = as_writable_bytes(jewels::as_single_item_span(buffer_storage))
                              .first(sizeof(buffer_storage) - support::storage_trail_padding<layout>);

  auto maybe_buffer = Buffer::try_make(storage_span, layout);
  REQUIRE(maybe_buffer);
  auto buffer = *maybe_buffer;

  REQUIRE(buffer.tail() == 0UL);
  REQUIRE(buffer.head() == 0UL);

  PublisherHandle pub_handle{
    jewels::memory::make_non_null_from_ref(buffer),
    0UL,
    jewels::memory::MemoryResource{std::pmr::new_delete_resource()}};

  REQUIRE(buffer.tail() == 0UL);
  REQUIRE(buffer.head() == 0UL);

  for (size_t i = 0U; i < 2U; ++i)
  {
    auto slot = pub_handle.reserve();
    REQUIRE(slot);
    REQUIRE(slot->commit(fake_publish_time));
  }

  REQUIRE(buffer.tail() == 0UL);
  REQUIRE(buffer.head() == 2UL);

  SECTION("Discard without reservation")
  {
    PublisherHandle pub_handle2{
      jewels::memory::make_non_null_from_ref(buffer),
      0UL,
      jewels::memory::MemoryResource{std::pmr::new_delete_resource()}};

    REQUIRE(buffer.tail() == 0UL);
    REQUIRE(buffer.head() == 2UL);

    {
      auto slot = pub_handle2.reserve();
      REQUIRE(slot);
      REQUIRE(slot->commit(fake_publish_time));
    }

    REQUIRE(buffer.tail() == 1UL);
    REQUIRE(buffer.head() == 3UL);
  }

  SECTION("Discard with reservation")
  {
    auto abandoned_slot = pub_handle.reserve();

    REQUIRE(buffer.tail() == 1UL);
    REQUIRE(buffer.head() == 2UL);

    PublisherHandle pub_handle2{
      jewels::memory::make_non_null_from_ref(buffer),
      0UL,
      jewels::memory::MemoryResource{std::pmr::new_delete_resource()}};

    REQUIRE(buffer.tail() == 1UL);
    REQUIRE(buffer.head() == 2UL);

    {
      auto slot = pub_handle2.reserve();
      REQUIRE(slot);
      REQUIRE(slot->commit(fake_publish_time));
    }

    REQUIRE(buffer.tail() == 1UL);
    REQUIRE(buffer.head() == 3UL);
  }
}

} // namespace clockwork::pinion
