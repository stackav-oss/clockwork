// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/interface.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <memory>
#include <memory_resource> // IWYU pragma: keep
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <vector>

namespace clockwork
{

/// CogState type that contains the state object and necessary mutexs.
template <typename StateType>
struct CogStateDataImpl : CogStateData
{
  /// Creates the State Record using the memory resource to allocate storage for the messagey state
  explicit CogStateDataImpl(jewels::memory::MemoryResource memres);

  // Return a reference to the current state data
  jewels::memory::ObjectPtr<StateType> get_ptr() noexcept;

  // The state
  StateType state;
  // Read mutex
  std::shared_mutex mutex;
  // List of observers to notify on mutex release.
  std::pmr::vector<std::shared_ptr<pinion::Observer>> observers;
};

/// CogState specialization for tachyon+pinion based states
template <typename SchemaType>
struct CogStateDataImpl<Tap<Tachyon<SchemaType>>> : CogStateData
{
  using StateType = Tap<Tachyon<SchemaType>>;

  /// Creates the State Record using the publisher handle to allocate storage for the messagey state
  explicit CogStateDataImpl(pinion::PublisherHandle publisher_in);

  // Since the pinion types refer to one another this can't be copied or moved
  CogStateDataImpl(const CogStateDataImpl&) = delete;
  CogStateDataImpl(CogStateDataImpl&&) = delete;
  CogStateDataImpl& operator=(const CogStateDataImpl&) = delete;
  CogStateDataImpl& operator=(CogStateDataImpl&&) = delete;
  ~CogStateDataImpl() noexcept override;

  // Return a reference to the current state data
  jewels::memory::ObjectPtr<StateType> get_ptr() noexcept;

  // The backing buffer handle
  pinion::PublisherHandle publisher;
  // The backing store for the current backing of state
  std::optional<pinion::ReservedSlot> current_slot;
  // The backing store for the current backing of state
  std::optional<pinion::Publishable<StateType>> current_publishable;
  // Read mutex
  std::shared_mutex mutex;
  // List of observers to notify on mutex release.
  std::pmr::vector<std::shared_ptr<pinion::Observer>> observers;
};

/// Helper class to handle the set of cog state. Maintains the necessary bookkeeping and
/// conversion to dial input types based on the templated policy.
///
/// @tparam Policy structure as follows:
///   struct Policy
///   {
///     // The input message type
///     using StateType;
///     // The endpoint id of the state
///     static constexpr EndpointClassId endpoint_id;
///     // Flag indicating if the state is read only.
///     static constexpr bool read_only;
///   };
template <typename Policy>
class CogState
{
public:
  using PolicyType = Policy;
  using StateType = typename Policy::StateType;
  using StatePtrType = jewels::memory::ObjectPtr<std::conditional_t<Policy::read_only, const StateType, StateType>>;
  using RecordType = CogStateDataImpl<StateType>;
  using RecordPtrType = std::shared_ptr<CogStateDataImpl<StateType>>;

  /// Constructor
  /// @param[in] record The state record
  explicit CogState(RecordPtrType record);

  /// Validate that all internal types are set.
  /// @return True if all internal types have been initialized.
  [[nodiscard]] bool validate() const;

  /// Attempt to acquire the lock.
  /// @see std::shared_mutex::try_lock for usage limitations
  /// @return True if the lock was successfully acquired.
  [[nodiscard]] bool try_lock();

  /// Is the lock acquired?
  /// @return True if the locked.
  [[nodiscard]] bool is_locked() const;

  /// Release the lock.
  /// @see std::shared_mutex::try_lock for usage limitations
  void unlock();

  /// Get the state.
  /// @pre The lock has been acquired.
  /// @return Pointer to the state.
  [[nodiscard]] StatePtrType get_state();

private:
  /// The state records
  RecordPtrType record_;

  /// Flag indicating if the lock is acquired.
  bool locked_ = false;
};

} // namespace clockwork

#include "clockwork/cog/cog_state.inl"
