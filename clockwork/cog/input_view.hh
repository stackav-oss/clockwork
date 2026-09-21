// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/device_ptr.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <wise_enum.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <type_traits>
#include <variant>

namespace clockwork
{

// Result of an aligned input lookup — distinguishes successful resolution from
// stale (data evicted from view) and pending (data not yet arrived) misses.
WISE_ENUM_CLASS(
  (AlignedLookupResult, int8_t),
  // The target seqno was found in the view
  resolved,
  // The target seqno is older than the oldest message in the view (unrecoverable)
  stale,
  // The target seqno is newer than the newest message in the view (may arrive later)
  pending)

// Outcome type for aligned lookups. `resolved` is the success value.
using AlignedLookupOutcome = jewels::Outcome<AlignedLookupResult, AlignedLookupResult::resolved>;

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
///     // The minimum number of messages guaranteed to be in the view.
///     static constexpr size_t min_msgs;
///     // The minimum number of new messages guaranteed to be in the view.
///     static constexpr size_t min_new_msgs;
///     // The safety margin required by the input.
///     static constexpr size_t safety_margin;
///     // The threshold for preemptively skipping ahead.
///     static constexpr std::optional<size_t> skip_threshold;
///     // Flag indicating if the inputs should be copied.
///     static constexpr auto copy_inputs = false;
///     // Flag indicating if the cursor is manually controlled.
///     static constexpr auto manual_cursor = false;
///     // Flag indicating if the sequence number should be exposed in the input view.
///     static constexpr auto expose_seqno = false;
///     // Flag indicating if getting the DevicePtr should be possible for the input view.
///     static constexpr auto use_device_ptr = false;
///   };
template <typename PolicyT>
class InputView
{
public:
  using Policy = PolicyT;
  using MsgType = typename Policy::MsgType;
  using MsgDevicePtr = pinion::DevicePtr<const MsgType>;
  using PinionDifferenceType = typename std::iterator_traits<pinion::SlotRef>::difference_type;
  static constexpr auto endpoint_id = Policy::endpoint_id;
  static constexpr auto max_view_size = Policy::max_view_size;
  static constexpr auto min_msgs = Policy::min_msgs;
  static constexpr auto min_new_msgs = Policy::min_new_msgs;
  static constexpr auto safety_margin = Policy::safety_margin;
  static constexpr auto skip_threshold = Policy::skip_threshold;
  static constexpr auto copy_inputs = Policy::copy_inputs;
  static constexpr auto manual_cursor = Policy::manual_cursor;
  static constexpr auto expose_seqno = Policy::expose_seqno;
  static constexpr auto use_device_ptr = Policy::use_device_ptr;

  using InputDialType =
    MessageInputDial<MsgType, max_view_size, min_msgs, min_new_msgs, manual_cursor, expose_seqno, use_device_ptr>;

  using ViewType = typename InputDialType::ViewType;
  using ViewItem = typename InputDialType::ViewItem;
  using ViewIteratorType = typename InputDialType::IteratorType;
  using LastViewedTuple = std::tuple<jewels::Uuid<common::EndpointClassId>, pinion::SlotRef>;

  /// Construct from a pinion subscriber handle.
  /// @param subscriber The subscriber handle
  /// @param resource The memory resource to use for allocations.
  explicit InputView(
    std::shared_ptr<pinion::AbstractChannel> channel, jewels::memory::MemoryResource resource) noexcept;

  InputView(const InputView&) = delete;
  InputView& operator=(const InputView&) = delete;
  InputView(InputView&&) = delete;
  InputView& operator=(InputView&&) = delete;

  /// Construct from a pinion subscriber handle.
  /// @param subscriber The subscriber handle
  /// @param metrics_batch_size Metrics signal batch size
  /// @param resource The memory resource to use for allocations.
  /// @param running_offline True if this view is to used offline.
  explicit InputView(
    std::shared_ptr<pinion::AbstractChannel> channel,
    size_t metrics_batch_size,
    jewels::memory::MemoryResource resource,
    bool running_offline) noexcept;

  /// Construct an input view for a non-connected channel
  /// @param metrics_batch_size Metrics signal batch size
  /// @param resource The memory resource to use for allocations.
  /// @param running_offline True if this view is to used offline.
  explicit InputView(size_t metrics_batch_size, jewels::memory::MemoryResource resource, bool running_offline) noexcept;

  /// Validate that all internal types are set correctly.
  [[nodiscard]] bool validate() const;

  /// Construct the MessageInputDial for this input view.
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

  /// Check for published once channels that have been published more than once
  /// @return true if any of published once channels have been published more than once
  [[nodiscard]] bool is_published_once_channel_invalid() const;

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

  /// Construct a single-message view spanning only the message with the given sequence number.
  /// Populates dial_out on success. Returns stale if the seqno has been evicted, pending if it
  /// hasn't arrived yet.
  /// @pre make_dial_input() must have been called first to populate the buffer.
  [[nodiscard]] AlignedLookupOutcome prepare_aligned_view(jewels::Out<InputDialType> dial_out, uint64_t target_seqno)
    requires(expose_seqno);

  /// Create a dial containing all messages in the closed seqno range [begin_seq, end_seq].
  /// Populates dial_out on success. Returns stale if begin_seq has been evicted, pending if
  /// end_seq hasn't arrived yet.
  /// @pre make_dial_input() must have been called first to populate the buffer.
  [[nodiscard]] AlignedLookupOutcome
  prepare_aligned_range(jewels::Out<InputDialType> dial_out, uint64_t begin_seq, uint64_t end_seq)
    requires(expose_seqno);

  /// Return a dial with an empty view.
  /// @pre make_dial_input() must have been called first to populate the buffer.
  [[nodiscard]] InputDialType prepare_empty_aligned_view()
    requires(expose_seqno);

  /// Advance the aligned cursor seqno to track which messages have been shown to user code.
  /// Only advances forward (max of current and new value).
  void advance_aligned_cursor(uint64_t seqno)
    requires(expose_seqno);

  /// Get the sequence numbers of messages from the most recent execution's view.
  /// @return Span of sequence numbers (valid until the next call to make_dial_input).
  [[nodiscard]] std::span<const uint64_t> get_metrics_sequence_numbers() const;

  /// Get the cursor position (as a view index) from the most recent execution.
  /// @return The cursor position index within the view.
  [[nodiscard]] uint64_t get_metrics_cursor_position() const;

  /// Clear saved_begin_ and saved_end_ without advancing last_viewed_.
  /// Used after an alignment miss to undo the side effects of make_dial_inputs()
  /// without consuming any messages.
  void reset_saved_state();

  /// @note The destructor is virtual so that we can effectively mock the InputView in tests.
  virtual ~InputView() = default;

private:
  using CopyStorageType = std::conditional_t<copy_inputs, std::array<MsgType, max_view_size>, std::array<MsgType, 0>>;

  bool apply_safety_margin(const auto& available, pinion::SlotRef& begin, pinion::SlotRef& end) const;
  std::optional<size_t> apply_skip_threshold(const auto& available, pinion::SlotRef& begin, pinion::SlotRef& end) const;

  /// The underlying subscriber handle.
  std::shared_ptr<pinion::AbstractChannel> subscriber_;
  /// Iterator tracking the input cursor.
  pinion::SlotRef input_cursor_;
  /// Iterator for the last viewed message.
  pinion::SlotRef last_viewed_;
  /// Iterator for the last begin iterator (used for overrun checks)
  pinion::SlotRef saved_begin_;
  /// Iterator for the last end iterator (used for keeping track of new messages)
  pinion::SlotRef saved_end_;
  /// Storage for the message view buffer, used to construct the dial inputs.
  std::array<ViewItem, max_view_size> msg_view_storage_{};
  /// Number of valid message pointers in msg_view_storage_ (filled linearly from index 0).
  size_t msg_count_{0};
  /// Storage for messages when copied inputs are required.
  [[no_unique_address]] CopyStorageType copy_inputs_storage_;

  /// Tracks the highest seqno of a message successfully shown to user code via alignment resolution.
  /// Used to compute first_new for reuse inputs.
  /// Uses std::optional for a proper sentinel: std::nullopt means "no messages have been shown".
  /// Only allocated when expose_seqno is true.
  using AlignedCursorType = std::conditional_t<expose_seqno, std::optional<uint64_t>, std::monostate>;
  [[no_unique_address]] AlignedCursorType aligned_cursor_seqno_{};

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

  /// Sequence numbers of messages in the view, stored during make_dial_input for cog metrics.
  std::array<uint64_t, max_view_size> metrics_seqnos_{};
  /// Number of valid entries in metrics_seqnos_.
  size_t metrics_seqno_count_{0};
  /// Cursor position within the view at time of last make_dial_input, for cog metrics.
  uint64_t metrics_cursor_position_{0};

  /// Construct a disconnected InputDialType (no subscriber available).
  [[nodiscard]] InputDialType make_disconnected_dial(const ViewType& view);

  /// Construct an InputDialType, attaching seqno span when expose_seqno is enabled.
  [[nodiscard]] InputDialType
  make_dial_result(const ViewType& view, ViewIteratorType cursor, ViewIteratorType first_new);

  /// Construct an InputDialType with skip count, attaching seqno span when expose_seqno is enabled.
  [[nodiscard]] InputDialType
  make_dial_result(const ViewType& view, ViewIteratorType cursor, ViewIteratorType first_new, size_t skip_count);

  /// Construct the MessageInputDial for the given range.
  [[nodiscard]] jewels::expected<InputDialType, pinion::ProgressError>
  make_dial_input_from_range(pinion::SlotRef begin, pinion::SlotRef end);

  /// Update the input metrics;
  void update_input_metrics(int new_msg_count, PinionDifferenceType num_dropped_messages, int64_t message_staleness);
};

} // namespace clockwork

#include "clockwork/cog/input_view.inl"
