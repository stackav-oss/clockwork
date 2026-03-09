// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/cog_state.hh"
#include "clockwork/cog/interface.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <memory>
#include <tuple>

namespace clockwork
{

/// Helper class to handle the set of cog states. Maintains the necessary bookkeeping and
/// conversion to dial input types based on the templated policies.
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
template <typename... Policies>
class CogStates
{
public:
  static constexpr auto policy_count = sizeof...(Policies);
  template <typename Policy>
  using StatePtr = std::shared_ptr<CogState<Policy>>;
  using PoliciesTuple = std::tuple<Policies...>;
  using CogStatesTuple = std::tuple<StatePtr<Policies>...>;
  using StatesTuple = std::tuple<typename CogState<Policies>::StatePtrType...>;
  template <size_t index>
  using StateType = CogState<typename std::tuple_element_t<index, PoliciesTuple>>;
  template <size_t index>
  using SchemaType = typename std::tuple_element_t<index, PoliciesTuple>::StateType;
  template <size_t index>
  using SchemaRefType = std::reference_wrapper<SchemaType<index>>;
  template <size_t index>
  using RecordPtrType = std::shared_ptr<CogStateDataImpl<SchemaType<index>>>;

  /// Constructor.
  /// @param[in] resource Memory resource
  explicit CogStates(jewels::memory::MemoryResource resource);

  /// Set the state handle.
  /// @tparam StateType The state type setting.
  template <typename CogType>
  [[nodiscard]] jewels::expected<void, jewels::MonoError> set_handle(
    jewels::Uuid<common::EndpointClassId> endpoint_id,
    std::shared_ptr<CogStateData> record,
    bool is_shared,
    jewels::memory::ObjectPtr<CogType> cog);

  /// Store a publisher handle for later snapshot configuration
  /// @param[in] endpoint_id The endpoint id
  /// @param[in] publisher The publisher handle to store
  /// @return Success if the endpoint matches a state, failure otherwise
  jewels::BinaryOutcome
  set_publisher_handle(jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle&& publisher);

  /// Set a snapshot configuration for a state endpoint
  /// @param[in] endpoint_id The endpoint id to snapshot
  /// @param[in] snapshot_config The snapshot configuration
  jewels::BinaryOutcome set_snapshot_config(
    jewels::Uuid<common::EndpointClassId> endpoint_id, const Tappy<common::SnapshotConfig>& snapshot_config);

  /// Take snapshots for any states with snapshot configurations
  /// @param current_time Current execution time
  jewels::BinaryOutcome publish_snapshots(jewels::time::SyncTime current_time);

  /// Validate that all internal types are set.
  /// @return True if all internal types are set.
  [[nodiscard]] bool validate() const;

  /// Attempt to acquire the locks for all state.
  /// @see std::shared_mutex::try_lock for usage limitations
  /// @return True if all locks were successfully acquired. If false any temporarily acquired locks are released.
  [[nodiscard]] bool try_lock();

  /// Are the locks acquired?
  /// @return True if all locks are acquired.
  [[nodiscard]] bool is_locked() const;

  /// Release all locks.
  /// @see std::shared_mutex::try_lock for usage limitations
  void unlock();

  /// Construct the states tuple.
  /// @pre The locks have been acquired.
  /// @return Tuple of stateuration objects.
  [[nodiscard]] StatesTuple make_states();

  /// Test whether the state at the specified index has been set
  /// @tparam index Record tuple index
  /// @return True if the state has been set
  template <size_t index>
  [[nodiscard]] bool is_state_set() const;

  /// Get the state at the specified index
  /// @tparam index
  /// @param[out] state State reference
  /// @return Success or failure if the state has not been set
  template <size_t index>
  jewels::BinaryOutcome get_state(jewels::FactoryOut<SchemaRefType<index>> state);

  /// Get the state handle at the specified index
  /// @tparam index
  /// @param[out] state_handle State handle
  /// @return Success or failure if the state has not been set
  template <size_t index>
  jewels::BinaryOutcome get_state_handle(jewels::Out<RecordPtrType<index>> state_handle);

  /// Set the state handle at the specified index
  /// @tparam index
  /// @param[in] state_handle State handle
  /// @return Success or failure if the state has already been set
  template <size_t index>
  jewels::BinaryOutcome set_state_handle(RecordPtrType<index> state_handle);

  /// Set the state data pointer at the specified index with a pinion backed state
  /// @tparam index
  /// @param[in] publisher_handle Pinion publisher handler
  /// @return Success or failure if the state has already been set
  template <size_t index>
  jewels::BinaryOutcome initialize_state(pinion::PublisherHandle publisher_handle)
    requires TappyType<SchemaType<index>>;

  /// Set the state data pointer at the specified index with a default constructed state
  /// @tparam index
  /// @return Success or failure if the state has already been set
  template <size_t index>
  jewels::BinaryOutcome initialize_state()
    requires(!TappyType<SchemaType<index>>);

private:
  /// Memory resource
  jewels::memory::MemoryResource resource_;
  /// The states
  CogStatesTuple cog_states_;
};

} // namespace clockwork

#include "clockwork/cog/cog_states.inl"
