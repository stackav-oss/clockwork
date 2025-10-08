// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <tuple>
#include <type_traits>

namespace clockwork
{

/// Helper class to handle cog message input views. Handles optional copying of inputs and checking for overruns.
///
/// @tparam Policy structure as follows:
///   struct Policy
///   {
///     // The input message type
///     using MsgType;
///     // The endpoint id of the subscriber
///     static constexpr EndpointClassId endpoint_id;
///     // The maximum view size for the dial input.
///     static constexpr size_t max_view_size;
///     // The safety margin required by the input.
///     static constexpr size_t safety_margin;
///     // The threshold for preemptively skipping ahead.
///     static constexpr std::optional<size_t> skip_threshold;
///     // Flag indicating if the inputs should be copied.
///     static constexpr auto copy_inputs = false;
///     // Flag indicating if the cursor is manually controlled.
///     static constexpr auto manual_cursor = false;
///   };
template <typename PolicyT>
class InputView
{
public:
  using Policy = PolicyT;
  using MsgType = typename Policy::MsgType;
  using PinionDifferenceType = typename std::iterator_traits<pinion::BufferIterator>::difference_type;
  static constexpr auto endpoint_id = Policy::endpoint_id;
  static constexpr auto max_view_size = Policy::max_view_size;
  static constexpr auto safety_margin = Policy::safety_margin;
  static constexpr auto skip_threshold = Policy::skip_threshold;
  static constexpr auto copy_inputs = Policy::copy_inputs;
  static constexpr auto manual_cursor = Policy::manual_cursor;

  using InputDialType = std::conditional_t<
    manual_cursor,
    MessageInputDialWithCursorControl<MsgType, max_view_size>,
    MessageInputDial<MsgType, max_view_size>>;

  using ViewType = typename MessageInputDial<MsgType, max_view_size>::ViewType;
  using ViewIteratorType = typename MessageInputDial<MsgType, max_view_size>::IteratorType;
  using LastViewedTuple = std::tuple<jewels::Uuid<common::EndpointClassId>, pinion::BufferIterator>;

  /// Construct from a pinion subscriber handle.
  /// @param subscriber The subscriber handle
  /// @param resource The memory resource to use for allocations.
  explicit InputView(pinion::SubscriberHandle subscriber, jewels::memory::MemoryResource resource) noexcept;

  InputView(const InputView&) = delete;
  InputView& operator=(const InputView&) = delete;
  InputView(InputView&&) = delete;
  InputView& operator=(InputView&&) = delete;

  /// Construct from a pinion subscriber handle.
  /// @param subscriber The subscriber handle
  /// @param running_offline True if this view is to used offline.
  /// @param resource The memory resource to use for allocations.
  explicit InputView(
    pinion::SubscriberHandle subscriber,
    size_t metrics_batch_size,
    jewels::memory::MemoryResource resource,
    bool running_offline) noexcept;

  /// Construct from a pinion subscriber handle.
  /// @param subscriber The subscriber handle
  /// @param running_offline True if this view is to used offline.
  /// @param resource The memory resource to use for allocations.
  explicit InputView(size_t metrics_batch_size, jewels::memory::MemoryResource resource, bool running_offline) noexcept;

  /// Validate that all internal types are set correctly.
  [[nodiscard]] bool validate() const;

  /// Construct the MessageInputDial for this subscriber.
  /// @note The cursor and last viewed message will not change until `commit` is called. At which point the end
  /// iterator used during this call will replace the current last used iterator.
  /// @note While this function will not change the last viewed value used to generate the inputs, subsequent calls to
  /// this function, without calling `commit`, can still return different values as the underlying
  /// subscriber may have changed.
  /// @param[in] max_new_msgs The maximum number of new messages to add to the input view. This value is determined from
  /// the minimum of max_bounds of all the message conditions on this input.
  /// @param[in] current_time The current time to use for the input view.
  /// @return Message dial input or unexpected if the conditions are not met.
  [[nodiscard]] jewels::expected<InputDialType, pinion::ProgressError>
  make_dial_input(PinionDifferenceType max_new_msgs, jewels::time::SyncTime current_time);

  /// Update the last viewed value with the saved iterator from make input.
  /// @pre The input is a reference to the input returned from the last call to `make_dial_input()`.
  /// @return input The dial input. Used by manual cursor inputs to get the current input cursor.
  /// @return the last viewed tuple.
  [[nodiscard]] LastViewedTuple commit(const InputDialType& input);

  /// Check for channel overruns.
  /// @return true if any of the saved dial inputs are no longer availabe.
  [[nodiscard]] bool is_overrun() const;

  /// Check if were in danger of being overrun.
  /// @return true if any of the saved dial inputs are close to be overrun by the producer.
  [[nodiscard]] bool almost_overrun() const;

  /// Get the input metrics
  /// @return The aggregated input metrics.
  /// @note This is only virtual so that we can effectively mock it in tests.
  [[nodiscard]] virtual const AggregatedInputMetrics& get_aggregated_input_metrics() const;

  /// Reset the input metrics.
  void reset_metrics();

  /// Gets the publish timestamp of the most recent message in the buffer during the last call to
  /// make_dial_input().  Note this may be more recent than what the cog saw
  [[nodiscard]] jewels::time::SyncTime latest_message_time() const;

  /// Gets whether the safety margin triggered a skip during the last call to make_dial_input().
  [[nodiscard]] bool did_safety_skip() const;

  /// Number of messages skipped due to configured skip threshold during the last call to make_dial_input().
  /// Will always be 0 unless the cog was configured with preemptive skipping
  [[nodiscard]] size_t last_skipped_count() const;

  /// @note The destructor is virtual so that we can effectively mock the InputView in tests.
  virtual ~InputView() = default;

private:
  using MessageInputCircularBuffer =
    jewels::container::CircularBuffer<detail::MsgPolicy<MsgType>, std::span<const MsgType*, max_view_size>>;
  using CopyStorageType = std::conditional_t<copy_inputs, std::array<MsgType, max_view_size>, std::array<MsgType, 0>>;

  bool apply_safety_margin(const auto& available, pinion::BufferIterator& begin, pinion::BufferIterator& end) const;
  std::optional<size_t>
  apply_skip_threshold(const auto& available, pinion::BufferIterator& begin, pinion::BufferIterator& end) const;

  /// The underlying subscriber handle.
  std::optional<pinion::SubscriberHandle> subscriber_;
  /// Iterator tracking the input cursor.
  pinion::BufferIterator input_cursor_;
  /// Iterator for the last viewed message.
  pinion::BufferIterator last_viewed_;
  /// Iterator for the last begin iterator (used for overrun checks)
  pinion::BufferIterator saved_begin_;
  /// Iterator for the last end iterator (used for keeping track of new messages)
  pinion::BufferIterator saved_end_;
  /// Storage for the message view buffer, used to construct the dial inputs.
  std::array<const MsgType*, max_view_size> msg_view_storage_;
  /// The circular message view buffer, used to construct the dial inputs.
  MessageInputCircularBuffer msg_view_buffer_;
  /// Storage for messages when copied inputs are required.
  CopyStorageType copy_inputs_storage_;
  /// True if running offline.
  bool running_offline_;

  /// Input metrics
  AggregatedInputMetrics input_metrics_;
  /// Most recent message as of the last make_dial_input()
  jewels::time::SyncTime latest_message_time_;
  /// Indicates if the safety margin triggered a skip
  bool skipped_safety_;
  /// Number of messages skipped due to configured skip threshold (independent of safety skip)
  size_t skipped_count_;

  size_t metrics_batch_size_;
  /// Construct the MessageInputDial for the given range.
  [[nodiscard]] jewels::expected<InputDialType, pinion::ProgressError>
  make_dial_input_from_range(pinion::BufferIterator begin, pinion::BufferIterator end);

  /// Update the input metrics;
  void update_input_metrics(int new_msg_count, PinionDifferenceType num_dropped_messages, int64_t message_staleness);
};

} // namespace clockwork

#include "clockwork/cog/input_view.inl"
