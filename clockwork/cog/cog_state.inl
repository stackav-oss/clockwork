// IWYU pragma: private, include "clockwork/cog/cog_state.hh"
#pragma once

#include "clockwork/cog/cog_state.hh"

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace clockwork
{

template <typename StateType>
CogStateDataImpl<StateType>::CogStateDataImpl(jewels::memory::MemoryResource memres)
  : state(memres), memres(std::move(memres))
{
}

template <typename StateType>
jewels::memory::ObjectPtr<StateType> CogStateDataImpl<StateType>::get_ptr() noexcept
{
  return jewels::memory::make_non_null_from_ref(state);
}

template <typename SchemaType>
CogStateDataImpl<Tap<Tachyon<SchemaType>>>::CogStateDataImpl(pinion::PublisherHandle publisher_in)
  : publisher(std::move(publisher_in))
{
  if (auto slot = publisher.reserve(); slot)
  {
    current_slot.emplace(*std::move(slot));
    if (auto pub = pinion::Publishable<StateType>::try_make(jewels::memory::make_non_null_from_ref(*current_slot)); pub)
    {
      current_publishable.emplace(*std::move(pub));
      return;
    }
    throw std::runtime_error("Could not create publishable for state");
  }
  throw std::runtime_error("Could not create slot for state");
}

template <typename SchemaType>
CogStateDataImpl<Tap<Tachyon<SchemaType>>>::~CogStateDataImpl() noexcept
{
  if (current_slot)
  {
    if (const auto result = current_slot->discard(); !result)
    {
      // TODO(OI-3047) Add error handling once state logging is added.
      jewels::log_cerr_error("Failed to discard CogStateDataImpl slot with error: {}", result.error());
    }
  }
}

template <typename SchemaType>
auto CogStateDataImpl<Tap<Tachyon<SchemaType>>>::get_ptr() noexcept -> jewels::memory::ObjectPtr<StateType>
{
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) the .value() checks this (and it should never be unset)
  return jewels::memory::make_non_null_from_ref(current_publishable.value().message());
}

template <typename SchemaType>
jewels::BinaryOutcome
CogStateDataImpl<Tap<Tachyon<SchemaType>>>::set_from_bytes(std::span<const std::byte> data) noexcept
{
  if (!current_publishable)
  {
    jewels::log_cerr_error("set_from_bytes called on state without publishable");
    return jewels::failure;
  }

  auto& message = current_publishable.value().message();
  if (data.size() != sizeof(message))
  {
    jewels::log_cerr_error("State data size mismatch: got {}, expected {}", data.size(), sizeof(message));
    return jewels::failure;
  }

  std::memcpy(&message, data.data(), data.size());
  return jewels::success;
}

template <typename Policy>
CogState<Policy>::CogState(RecordPtrType record)
  : record_(std::move(record))
{
}

template <typename Policy>
bool CogState<Policy>::validate() const
{
  return static_cast<bool>(record_);
}

template <typename Policy>
bool CogState<Policy>::try_lock()
{
  if (locked_)
  {
    return locked_;
  }

  if constexpr (Policy::read_only)
  {
    locked_ = record_->mutex.try_lock_shared();
  }
  else
  {
    locked_ = record_->mutex.try_lock();
  }

  return locked_;
}

template <typename Policy>
bool CogState<Policy>::is_locked() const
{
  return locked_;
}

template <typename Policy>
void CogState<Policy>::unlock()
{
  if (!locked_)
  {
    return;
  }

  if constexpr (Policy::read_only)
  {
    record_->mutex.unlock_shared();
  }
  else
  {
    record_->mutex.unlock();
  }

  locked_ = false;
}

template <typename Policy>
auto CogState<Policy>::get_state() -> StatePtrType
{
  return record_->get_ptr();
}

template <typename Policy>
auto CogState<Policy>::get_mutable_state() -> MutableStatePtrType
{
  return record_->get_ptr();
}

template <typename Policy>
auto CogState<Policy>::get_record_ptr() const -> RecordPtrType
{
  return record_;
}

template <typename Policy>
auto CogState<Policy>::get_snapshot_info() noexcept -> StateSnapshotInfo*
{
  if (snapshot_info_)
  {
    return &(*snapshot_info_);
  }
  return nullptr;
}

template <typename Policy>
void CogState<Policy>::set_snapshot_info(StateSnapshotInfo info)
{
  snapshot_info_.emplace(std::move(info));
}

} // namespace clockwork
