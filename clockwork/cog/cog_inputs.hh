// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/input_view.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <array>
#include <memory>
#include <mutex>
#include <tuple>

namespace clockwork
{

/// Helper class to handle the set of cog subscribers. Maintains the necessary bookkeeping and
/// conversion to dial input types based on the templated policies.
///
/// @tparam Policies see InputView::Policy
template <typename... Policies>
class CogInputs
{
public:
  static constexpr auto policy_count = sizeof...(Policies);
  using PoliciesTuple = std::tuple<Policies...>;
  template <typename Policy>
  using SubscriberType = std::shared_ptr<InputView<Policy>>;
  using SubscribersTuple = std::tuple<SubscriberType<Policies>...>;
  using InputDialTuple = std::tuple<typename InputView<Policies>::InputDialType...>;
  using LastViewedTuple = std::tuple<jewels::Uuid<common::EndpointClassId>, pinion::SlotRef>;
  using LastViewedArray = std::array<LastViewedTuple, policy_count>;
  template <size_t index>
  using PolicyType = typename std::tuple_element_t<index, PoliciesTuple>;

  /// Construct from a pinion subscriber handle.
  /// @param running_offline Whether or not this cog is running offline.
  explicit CogInputs(jewels::memory::MemoryResource resource, bool running_offline) noexcept;

  /// Validate that all internal types are set.
  [[nodiscard]] bool validate() const;

  /// Set the subscriber handle.
  /// @tparam CogType The cog type setting the handle, it is expected to have a `notify()` call to be invoked on
  /// updates.
  template <typename CogType>
  [[nodiscard]] jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError> set_handle(
    jewels::Uuid<common::EndpointClassId> endpoint_id,
    std::shared_ptr<pinion::AbstractChannel> channel,
    jewels::memory::ObjectPtr<CogType> cog);

  /// Set up an input endpoint without a subscriber handle for non-connected endpoints
  /// @tparam CogType The cog type setting up the input, it is expected to have a `notify()` call
  /// @param[in] endpoint_id UUID of the endpoint to set up
  /// @return Success if input was set up successfully, error otherwise
  template <typename CogType>
  jewels::expected<void, jewels::MonoError> set_input(jewels::Uuid<common::EndpointClassId> endpoint_id);

  /// Construct the InputDial views for this set of inputs.
  /// @note The last consumed message will not change until `commit` is called. At which point the end
  /// iterator used during this call will replace the current last used iterator.
  /// @note While this function will not change the last consumed value used to generate the inputs, subsequent calls
  /// to this function, without calling `commit`, can still return different values as the underlying subscriber may
  /// have changed.
  /// @param[in] conditions The set of conditions associated with the cog inputs.
  /// @return Message dial inputs or unexpected if there was an error.
  template <typename ConditionsType>
  [[nodiscard]] jewels::expected<InputDialTuple, pinion::ProgressError>
  make_dial_inputs(const typename ConditionsType::ConditionsTuple& conditions, jewels::time::SyncTime current_time);

  /// Update the last viewed and input cursors for all the inputs.
  /// @pre The inputs tuple is a reference to the input returned from the last call to `make_dial_input()`.
  /// @return input The dial inputs. Used by manual cursor inputs to get the current input cursor.
  /// @return The tuple of last consumed iterators.
  [[nodiscard]] LastViewedArray commit(const InputDialTuple& inputs);

  /// Check for channel overruns.
  /// @return true if any of the saved dial inputs are no longer availabe.
  [[nodiscard]] bool is_overrun() const;

  /// Check if any inputs are _about_ to be overrun.
  /// @return true if any of the saved dial inputs are close to be overrun by their producers.
  [[nodiscard]] bool almost_overrun() const;

  /// Check for published once channels that have been published more than once
  /// @return true If any of the dial inputs are published once and have been published more than once
  [[nodiscard]] bool is_published_once_channel_invalid() const;

  /// Set diagnostics for each input
  template <typename Report, typename Enum, Enum... missing_ids, Enum... safety_skip_ids>
  void set_infra_diagnostics(
    Report& report,
    jewels::time::SyncTime start_time,
    std::integer_sequence<Enum, missing_ids...> /*missing*/,
    std::integer_sequence<Enum, safety_skip_ids...> /*safety_skip*/) const;

  /// Get the subscribers.
  /// @return The tuple of subscribers.
  [[nodiscard]] SubscribersTuple& subscribers();

  /// Narrow the InputView at the given index to a single message identified by sequence number.
  /// Used by generated alignment resolution code.
  /// @tparam index Index into the subscribers tuple.
  /// @param dial_out Output: the narrowed dial, populated on success.
  /// @param target_seqno The sequence number to resolve.
  /// @return AlignedLookupOutcome: resolved, stale, or pending.
  template <size_t index>
  [[nodiscard]] auto prepare_aligned_input(
    jewels::Out<typename std::tuple_element_t<index, SubscribersTuple>::element_type::InputDialType> dial_out,
    uint64_t target_seqno);

  /// Narrow the InputView at the given index to a range of messages identified by closed seqno range.
  /// @tparam index Index into the subscribers tuple.
  /// @param dial_out Output: the narrowed dial, populated on success.
  /// @param begin_seq The first sequence number in the batch (inclusive).
  /// @param end_seq The last sequence number in the batch (inclusive).
  /// @return AlignedLookupOutcome: resolved, stale, or pending.
  template <size_t index>
  [[nodiscard]] auto prepare_aligned_input_range(
    jewels::Out<typename std::tuple_element_t<index, SubscribersTuple>::element_type::InputDialType> dial_out,
    uint64_t begin_seq,
    uint64_t end_seq);

  /// Clear the InputView at the given index so it contains no messages.
  /// Returns a dial with an empty view for assignment into the InputDialTuple.
  /// @tparam index Index into the subscribers tuple.
  template <size_t index>
  [[nodiscard]] auto prepare_empty_aligned_input();

  /// Advance the aligned cursor for the InputView at the given index.
  /// Called after successful cog execution for aligned upstream inputs.
  /// @tparam index Index into the subscribers tuple.
  /// @param seqno The sequence number to advance the cursor to.
  template <size_t index>
  void advance_aligned_cursor(uint64_t seqno);

  /// Set the input subscriber handle at the specified index
  ///
  /// Used by unit test cogs to initialize the unit test input channels
  ///
  /// @tparam<index> Input index
  /// @tparam<CogType> Cog type
  /// @param[in] handle Subscriber handle
  /// @param[in] cog Cog pointer
  template <size_t index, typename CogType>
  void set_unit_test_input(std::shared_ptr<pinion::AbstractChannel> channel);

  /// Get the default number of slots in the pinion buffer for a unit test input channel
  ///
  /// Used by unit test cogs to get the default slot counts for input channels
  ///
  /// @tparam ConditionsType Cog input conditions
  template <typename ConditionsType>
  [[nodiscard]] static constexpr std::array<uint32_t, policy_count> get_default_unit_test_slot_counts();

  /// Commit only the input at the given index. Returns the {endpoint_id, last_viewed} tuple
  /// for passing to CogConditions::commit().
  /// Used for selective commit on alignment miss (advance past stale alignment messages
  /// without consuming other triggers).
  template <size_t index>
  [[nodiscard]] auto commit_single(const InputDialTuple& inputs);

  /// Commit only the input at the given runtime index.
  /// @param commit_result_out Output: the {endpoint_id, last_viewed} tuple for the committed input.
  /// @param inputs The dial inputs returned by the last call to make_dial_inputs().
  /// @param index Index into the subscribers tuple.
  /// @return Success if the index identifies an input, or failure otherwise.
  [[nodiscard]] jewels::BinaryOutcome
  commit_single(jewels::Out<LastViewedTuple> commit_result_out, const InputDialTuple& inputs, size_t index);

  /// Clear saved_begin_/saved_end_ on all input views without advancing last_viewed_.
  /// Used after an alignment miss to undo the side effects of make_dial_inputs().
  void reset_saved_state();

private:
  /// Memory resource
  jewels::memory::MemoryResource resource_;
  /// True if running offline.
  bool running_offline_;
  /// The subscribers
  SubscribersTuple subscribers_;
  /// Mutex to coordinate access to the underlying subscribers
  mutable std::mutex subscribers_mutex_;
};

} // namespace clockwork

#include "clockwork/cog/cog_inputs.inl"
