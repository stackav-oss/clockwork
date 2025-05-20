// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/publisher_handle.hh"

#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace clockwork::pinion
{

PublisherHandle::PublisherHandle(
  jewels::memory::ObjectPtr<Buffer> buffer, size_t num_observers, jewels::memory::MemoryResource resource) noexcept
  : buffer_{buffer}, observers_(resource), head_{buffer->head()}, tail_{buffer->tail()}
{
  observers_.reserve(num_observers);
}

PublisherHandle::~PublisherHandle() = default;

const BufferLayout& PublisherHandle::layout() const noexcept
{
  return buffer_->layout();
}

bool PublisherHandle::add_observer(jewels::memory::ObjectPtr<Observer> observer) noexcept
{
  if (observers_.size() < observers_.capacity())
  {
    observers_.emplace_back(observer);
    return true;
  }
  return false;
}

jewels::expected<void, ReserveError> PublisherHandle::hide(size_t count) noexcept
{
  if (reserved_ > 0U)
  {
    return jewels::unexpected{ReserveError::existing_reservation};
  }
  if (count > buffer_->layout().num_slots)
  {
    return jewels::unexpected{ReserveError::count_too_large};
  }
  const auto slots_in_use = head_ - tail_;
  const auto slots_available = buffer_->layout().num_slots - slots_in_use;
  const auto slots_to_hide = count > slots_available ? count - slots_available : 0UL;

  // Increment the tail to hide enough elements to satisfy the reservation.
  const auto maybe_new_tail = buffer_->increment_tail(tail_, slots_to_hide);
  if (!maybe_new_tail)
  {
    return jewels::unexpected{ReserveError::unexpected_tail};
  }

  // The control block has been updated already so subscribers can't
  // see these slots anymore.  Safe to zero out headers and footers of
  // hidden slots.
  for (auto hidden_slot : std::ranges::subrange<BufferIterator>{
         BufferIterator{buffer_->get(), buffer_->layout(), tail_},
         BufferIterator{buffer_->get(), buffer_->layout(), *maybe_new_tail}})
  {
    for (auto bytes : hidden_slot.headers_footers())
    {
      if (!bytes.empty())
      {
        std::memset(bytes.data(), 0, bytes.size_bytes());
      }
    }
  }

  tail_ = *maybe_new_tail;

  reserved_ = count;
  return {};
}

jewels::expected<ReservedSlot, ReserveError> PublisherHandle::reserve() noexcept
{
  if (const auto result = hide(1UL); !result)
  {
    return jewels::unexpected{result.error()};
  }
  return {jewels::in_place, ReservedSlot{jewels::memory::make_non_null_from_ref(*this), head_}};
}

jewels::expected<BatchReservedSlot, ReserveError> PublisherHandle::reserve(size_t count) noexcept
{
  if (const auto result = hide(count); !result)
  {
    return jewels::unexpected{result.error()};
  }
  return {jewels::in_place, BatchReservedSlot{jewels::memory::make_non_null_from_ref(*this), head_, count}};
}

jewels::expected<void, WriteError>
PublisherHandle::commit(BufferIndex reserved_slot, jewels::time::SyncTime publish_time) noexcept
{
  if (reserved_ == 0UL || head_ != reserved_slot)
  {
    return jewels::unexpected{WriteError::unexpected_reservation};
  }

  // Write the sequence number before committing.
  auto index = 0UL;
  const auto current_time = jewels::time::SyncClock::now();
  for (auto hidden_slot : std::ranges::subrange<BufferIterator>{
         BufferIterator{buffer_->get(), buffer_->layout(), head_},
         BufferIterator{buffer_->get(), buffer_->layout(), head_ + reserved_}})
  {
    hidden_slot.header()->sequence_number = head_ + index++;
    hidden_slot.header()->publish_timestamp = publish_time.time_since_epoch().count();
    hidden_slot.header()->source_commit_timestamp = current_time.time_since_epoch().count();
    hidden_slot.header()->latest_commit_timestamp = current_time.time_since_epoch().count();
  }

  return commit_common(publish_time);
}

jewels::expected<void, WriteError> PublisherHandle::commit(
  BufferIndex reserved_slot,
  jewels::time::SyncTime publish_time,
  size_t sequence_number,
  jewels::time::SyncTime source_commit_time) noexcept
{
  if (reserved_ == 0UL || head_ != reserved_slot)
  {
    return jewels::unexpected{WriteError::unexpected_reservation};
  }

  if (reserved_ != 1UL)
  {
    return jewels::unexpected{WriteError::seqno_count_mismatch};
  }

  // Write the sequence number before committing.
  auto hidden_slot = BufferIterator{buffer_->get(), buffer_->layout(), head_};
  hidden_slot->header()->sequence_number = sequence_number;
  hidden_slot->header()->publish_timestamp = publish_time.time_since_epoch().count();
  hidden_slot->header()->source_commit_timestamp = source_commit_time.time_since_epoch().count();
  hidden_slot->header()->latest_commit_timestamp = jewels::time::SyncClock::now().time_since_epoch().count();

  return commit_common(publish_time);
}

jewels::expected<void, WriteError> PublisherHandle::commit_common(jewels::time::SyncTime publish_time) noexcept
{
  // Set up the notification before committing and incrementing the head index
  const Observer::Event notification{
    .tail = tail_,
    .head = head_,
    .current_time = publish_time,
  };

  // Incrementing the head exposes the new element.
  const auto maybe_new_head = buffer_->increment_head(head_, reserved_);
  if (!maybe_new_head)
  {
    return jewels::unexpected{WriteError::unexpected_head};
  }
  reserved_ = 0UL;
  head_ = *maybe_new_head;

  // Pass notification to observers
  for (auto observer : observers_)
  {
    observer->notify(notification);
  }
  return {};
}

jewels::expected<void, WriteError> PublisherHandle::discard(BufferIndex reserved_slot) noexcept
{
  if (reserved_ == 0UL || head_ != reserved_slot)
  {
    return jewels::unexpected{WriteError::unexpected_reservation};
  }
  reserved_ = 0UL;
  return {};
}

ReservationState::ReservationState(ReservationState&& other) noexcept
  : state_{std::exchange(other.state_, State::ignore)}
{
}

void ReservationState::set(State state) noexcept
{
  state_ = state;
}

ReservationState::State ReservationState::get() const noexcept
{
  return state_;
}

bool ReservationState::operator==(State state) const noexcept
{
  return state_ == state;
}

ReservedSlot::ReservedSlot(
  jewels::memory::ObjectPtr<PublisherHandle> publisher_handle, BufferIndex reserved_index) noexcept
  : publisher_handle_{publisher_handle}, reserved_index_{reserved_index}
{
}

ReservedSlot::~ReservedSlot() noexcept(false)
{
  // When possible, the owner of the slot should call .process() prior
  // to the destructor so the destructor does nothing.
  switch (state_.get())
  {
  case ReservationState::State::discard:
    if (!discard())
    {
      throw std::runtime_error{"A slot was not explicitly procssed and failed to discard."};
    }
    break;
  case ReservationState::State::commit:
    throw std::runtime_error{"Unable to commit a marked slot without a timestamp."};
    break;
  case ReservationState::State::ignore:
    break;
  };
}

void ReservedSlot::mark_for_commit() noexcept
{
  state_.set(ReservationState::State::commit);
}

void ReservedSlot::mark_for_discard() noexcept
{
  state_.set(ReservationState::State::discard);
}

jewels::expected<void, WriteError> ReservedSlot::process(jewels::time::SyncTime publish_time) noexcept
{
  const auto current_state = state_.get();
  state_.set(ReservationState::State::ignore);
  if (current_state == ReservationState::State::commit)
  {
    return publisher_handle_->commit(reserved_index_, publish_time);
  }
  if (current_state == ReservationState::State::discard)
  {
    return publisher_handle_->discard(reserved_index_);
  }
  return {};
}

jewels::expected<void, WriteError> ReservedSlot::commit(
  jewels::time::SyncTime publish_time, uint64_t sequence_number, jewels::time::SyncTime source_commit_time) noexcept
{
  state_.set(ReservationState::State::ignore);
  return publisher_handle_->commit(reserved_index_, publish_time, sequence_number, source_commit_time);
}

jewels::expected<void, WriteError> ReservedSlot::commit(jewels::time::SyncTime publish_time) noexcept
{
  state_.set(ReservationState::State::ignore);
  return publisher_handle_->commit(reserved_index_, publish_time);
}

jewels::expected<void, WriteError> ReservedSlot::discard() noexcept
{
  state_.set(ReservationState::State::ignore);
  return publisher_handle_->discard(reserved_index_);
}

ReservationState::State ReservedSlot::state() const noexcept
{
  return state_.get();
}

Slot ReservedSlot::slot() const noexcept
{
  const auto& buffer = *publisher_handle_->buffer_;
  return *BufferIterator{buffer.get(), buffer.layout(), reserved_index_};
}

BatchReservedSlot::~BatchReservedSlot() noexcept(false)
{
  // When possible, the owner of the slot should call .process() prior
  // to the destructor so the destructor does nothing.
  switch (state_.get())
  {
  case ReservationState::State::discard:
    if (!discard())
    {
      throw std::runtime_error{"A slot was not explicitly procssed and failed to discard."};
    }
    break;
  case ReservationState::State::commit:
    throw std::runtime_error{"Unable to commit a marked slot without a timestamp."};
    break;
  case ReservationState::State::ignore:
    break;
  };
}

jewels::expected<void, WriteError> BatchReservedSlot::commit(jewels::time::SyncTime publish_time) noexcept
{
  state_.set(ReservationState::State::ignore);
  return publisher_handle_->commit(reserved_index_, publish_time);
}

jewels::expected<void, WriteError> BatchReservedSlot::discard() noexcept
{
  state_.set(ReservationState::State::ignore);
  return publisher_handle_->discard(reserved_index_);
}

std::ranges::subrange<BufferIterator> BatchReservedSlot::slots() const noexcept
{
  const auto& buffer = *publisher_handle_->buffer_;
  const auto begin = BufferIterator{buffer.get(), buffer.layout(), reserved_index_};
  const auto end = BufferIterator{buffer.get(), buffer.layout(), reserved_index_ + count_};

  return std::ranges::subrange<BufferIterator>{begin, end};
}

BatchReservedSlot::BatchReservedSlot(
  jewels::memory::ObjectPtr<PublisherHandle> publisher_handle, BufferIndex reserved_index, size_t count) noexcept
  : publisher_handle_{publisher_handle}, reserved_index_{reserved_index}, count_{count}
{
}

} // namespace clockwork::pinion
