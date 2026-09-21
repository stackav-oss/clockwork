// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/publisher_slot_ref.hh"

#include "clockwork/pinion/buffer_layout.hh"

#include <chrono>
#include <iterator>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace clockwork::pinion
{
namespace detail
{
BufferReservation::BufferReservation(
  jewels::memory::ObjectPtr<Buffer> buffer, BufferIndex index, size_t count, bool connected) noexcept
  : buffer_(buffer), index_(index), count_(count), connected_(connected)
{
}

jewels::expected<void, WriteError>
BufferReservation::process(PublisherSlotRefRange commit, PublisherSlotRefRange discard) noexcept
{
  if (commit.size() + discard.size() != count_)
  {
    return jewels::unexpected(WriteError::unexpected_reservation);
  }
  if (commit.size() == 0)
  {
    return buffer_->discard(index_);
  }
  return buffer_->commit(index_, commit.size());
}

jewels::expected<void, WriteError> BufferReservation::discard() noexcept
{
  return buffer_->discard(index_);
}

PublisherSlotRefRange BufferReservation::slots() const noexcept
{
  PublisherSlotRef begin{buffer_, BufferIterator{buffer_->get(), buffer_->layout(), index_}, connected_};
  return {begin, std::next(begin, static_cast<ptrdiff_t>(count_))};
}
} // namespace detail

template <typename Impl>
PublisherReservation::PublisherReservation(Observer* observer, Impl&& impl, uint64_t sequence_number)
  : observer_(observer),
    impl_(std::forward<Impl>(impl)),
    slots_(std::get<std::decay_t<Impl>>(impl_).slots()),
    sequence_number_(sequence_number)
{
  for (auto slot : slots_)
  {
    jewels::memory::ObjectPtr<Header> header = slot.header();
    header->sequence_number = sequence_number_discard;
    header->publish_timestamp = unset_publish_timestamp;
  }
}

PublisherReservation::PublisherReservation(
  Observer* observer, jewels::memory::ObjectPtr<Buffer> buffer, BufferIndex index, size_t count, bool connected)
  : PublisherReservation(observer, detail::BufferReservation(buffer, index, count, connected), index)
{
}

PublisherReservation::~PublisherReservation() noexcept(false)
{
  // When possible, the owner of the slot should call .process() prior
  // to the destructor so the destructor does nothing.
  if (!discard())
  {
    throw std::runtime_error{"A slot was not explicitly processed and failed to discard."};
  }
}

[[nodiscard]] jewels::expected<void, WriteError> PublisherReservation::discard() noexcept
{
  auto result = std::visit([](auto& iter) { return iter.discard(); }, impl_);
  unset();
  return result;
}

[[nodiscard]] jewels::expected<void, WriteError>
PublisherReservation::commit(jewels::time::SyncTime publish_time) noexcept
{
  const auto current_time = jewels::time::SyncClock::now();
  return process(true, publish_time, sequence_number_, current_time, current_time);
}

[[nodiscard]] jewels::expected<void, WriteError> PublisherReservation::commit(
  jewels::time::SyncTime publish_time, uint64_t sequence_number, jewels::time::SyncTime source_commit_time) noexcept
{
  const auto current_time = jewels::time::SyncClock::now();
  return process(true, publish_time, sequence_number, source_commit_time, current_time);
}

[[nodiscard]] jewels::expected<void, WriteError>
PublisherReservation::process(jewels::time::SyncTime publish_time) noexcept
{
  const auto current_time = jewels::time::SyncClock::now();
  return process(false, publish_time, sequence_number_, current_time, current_time);
}

[[nodiscard]] jewels::expected<void, WriteError> PublisherReservation::process(
  bool force_commit,
  jewels::time::SyncTime publish_time,
  uint64_t sequence_number,
  jewels::time::SyncTime source_commit_time,
  jewels::time::SyncTime latest_commit_timestamp) noexcept
{
  const auto [slot_begin, slot_end] = slots();
  auto slot_it = slot_begin;
  for (; slot_it != slot_end; ++slot_it)
  {
    jewels::memory::ObjectPtr<Header> header = slot_it->header();
    if (!force_commit && header->sequence_number == sequence_number_discard)
    {
      break;
    }
    header->sequence_number = sequence_number++;
    if (header->publish_timestamp == unset_publish_timestamp)
    {
      header->publish_timestamp = publish_time.time_since_epoch().count();
    }
    header->source_commit_timestamp = source_commit_time.time_since_epoch().count();
    header->latest_commit_timestamp = latest_commit_timestamp.time_since_epoch().count();
  }
  for (auto tail_it = slot_it; tail_it != slot_end; ++tail_it)
  {
    jewels::memory::ObjectPtr<Header> header = tail_it->header();
    if (header->sequence_number != sequence_number_discard)
    {
      std::ignore = discard();
      return jewels::unexpected(WriteError::non_contiguous);
    }
  }
  const auto result =
    std::visit([=](auto& iter) { return iter.process({slot_begin, slot_it}, {slot_it, slot_end}); }, impl_);
  if (not result)
  {
    return result;
  }
  if (slot_it != slot_begin && observer_ != nullptr)
  {
    const Observer::Event notification{
      .current_time = publish_time,
    };
    observer_->notify(notification);
  }
  unset();
  return result;
}

} // namespace clockwork::pinion
