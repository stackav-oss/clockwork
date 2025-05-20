// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/cog_state.hh"
#include "clockwork/cog/interface.hh"
#include "clockwork/common/process_description.hh"
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

  /// Validate that all internal types are set.
  /// @return True if all internal types are set.
  [[nodiscard]] bool validate() const;

  /// Attempt to acquire the locks for all state.
  /// @see std::shared_mutex::try_lock for usage limitations
  /// @return True if all locks were successfully acquired. If false any temporarially acquired locks are released.
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

private:
  /// Memory resource
  jewels::memory::MemoryResource resource_;
  /// The states
  CogStatesTuple cog_states_;
};

} // namespace clockwork

#include "clockwork/cog/cog_states.inl"
