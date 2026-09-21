// IWYU pragma: private, include "clockwork/cog/input_view.hh"
#pragma once
#include "clockwork/cog/input_view.hh"

#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/dsl/cog/ten_nanosecond_type.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/conversions.hh"
#include "jewels/time/sync_time.hh"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include <typeinfo>
#include <utility>
#include <vector>

namespace clockwork
{

template <typename Policy>
InputView<Policy>::InputView(
  std::shared_ptr<pinion::AbstractChannel> channel, jewels::memory::MemoryResource resource) noexcept
  : InputView<Policy>::InputView(std::move(channel), false, resource)
{
}

template <typename Policy>
InputView<Policy>::InputView(
  std::shared_ptr<pinion::AbstractChannel> channel,
  size_t metrics_batch_size,
  jewels::memory::MemoryResource resource,
  bool running_offline) noexcept
  : subscriber_(std::move(channel)),
    running_offline_(running_offline),
    input_metrics_{.event_metrics = std::pmr::vector<InputEventMetrics>(resource), .telemetry_metrics{}},
    skipped_safety_{false},
    skipped_count_{0UL},
    metrics_batch_size_(metrics_batch_size)
{
}

template <typename Policy>
InputView<Policy>::InputView(
  size_t metrics_batch_size, jewels::memory::MemoryResource resource, bool running_offline) noexcept
  : subscriber_(nullptr),
    running_offline_(running_offline),
    input_metrics_{.event_metrics = std::pmr::vector<InputEventMetrics>(resource), .telemetry_metrics{}},
    skipped_safety_{false},
    skipped_count_{0UL},
    metrics_batch_size_(metrics_batch_size)
{
}
template <typename Policy>
bool InputView<Policy>::validate() const
{
  if (!subscriber_)
  {
    return true;
  }
  const auto& layout = subscriber_->layout();
  const auto expected_msg_size = sizeof(typename Policy::MsgType);
  const auto configured_msg_size = layout.message_size;
  if (expected_msg_size != configured_msg_size)
  {
    jewels::log_cerr_error(
      "Subscriber '{}' for type '{}'  has incorrect message size: got {}, expected {}",
      Policy::name,
      typeid(Policy).name(),
      configured_msg_size,
      expected_msg_size);
    return false;
  }
  const auto& channel_size = layout.num_slots;
  if (channel_size < max_view_size)
  {
    jewels::log_cerr_warn("'{}' buffer {} < view {} size.", Policy::name, max_view_size, channel_size);
  }
  return true;
}

template <typename Policy>
void InputView<Policy>::update_input_metrics(
  int new_msg_count, PinionDifferenceType num_dropped_messages, int64_t message_staleness)
{

  std::ignore = input_metrics_.telemetry_metrics.message_staleness.update(
    std::chrono::duration_cast<TenNanoseconds>(std::chrono::nanoseconds(message_staleness)));
  std::ignore = input_metrics_.telemetry_metrics.num_unseen_messages.update(static_cast<uint16_t>(new_msg_count));
  std::ignore = input_metrics_.telemetry_metrics.messages_dropped.update(static_cast<uint16_t>(num_dropped_messages));

  // Not necessarily an error because metrics collection might be disabled.
  if (input_metrics_.event_metrics.size() >= metrics_batch_size_)
  {
    return;
  }
  input_metrics_.event_metrics.emplace_back(
    InputEventMetrics{
      .num_unseen_messages = static_cast<uint16_t>(new_msg_count),
      .message_staleness = std::chrono::duration_cast<TenNanoseconds>(std::chrono::nanoseconds(message_staleness)),
      .messages_dropped = static_cast<uint16_t>(num_dropped_messages),

    });
}

template <typename Policy>
const AggregatedInputMetrics& InputView<Policy>::get_aggregated_input_metrics() const
{
  return input_metrics_;
}

template <typename Policy>
void InputView<Policy>::reset_metrics()
{
  input_metrics_.event_metrics.clear();
  input_metrics_.telemetry_metrics.message_staleness.clear();
  input_metrics_.telemetry_metrics.num_unseen_messages.clear();
  input_metrics_.telemetry_metrics.messages_dropped.clear();
}

template <typename Policy>
// NOLINTNEXTLINE(readability-function-cognitive-complexity) This just has a lot of steps
auto InputView<Policy>::make_dial_input(PinionDifferenceType max_new_msgs, jewels::time::SyncTime current_time)
  -> jewels::expected<InputDialType, pinion::ProgressError>
{
  // Populate the view buffer with the desired range. Copying the data if necessary.
  if (!subscriber_)
  {
    const ViewType view{msg_view_storage_.data(), msg_count_};
    return make_disconnected_dial(view);
  }
  auto available = subscriber_->available();

  // Determine the end of the view taking into account the max new messages to be added.

  auto bounded_last_viewed =
    (last_viewed_.is_sentinel() || last_viewed_ < available.begin()) ? available.begin() : last_viewed_;
  auto new_msg_count = std::distance(bounded_last_viewed, available.end());
  auto end = std::next(bounded_last_viewed, std::min(max_new_msgs, new_msg_count));

  auto num_dropped_messages = new_msg_count > Policy::max_view_size ? new_msg_count - Policy::max_view_size : 0;
  int64_t message_staleness = 0;
  if (new_msg_count != 0)
  {
    auto latest_message = std::prev(end);
    message_staleness = jewels::time::get_ns(current_time) - latest_message->header()->publish_timestamp;
  }
  if (available.begin() != available.end())
  {
    auto latest_message = std::prev(available.end());
    latest_message_time_ =
      jewels::time::SyncTime(std::chrono::nanoseconds(latest_message->header()->publish_timestamp));
  }

  update_input_metrics(static_cast<int>(new_msg_count), num_dropped_messages, message_staleness);

  // Determine the view range based on the views max msg count.

  auto bounded_available_size = std::distance(available.begin(), end);
  auto begin = std::prev(end, std::min<PinionDifferenceType>(max_view_size, bounded_available_size));

  // Skip to the latest three messages on the first execution.

  if (!running_offline_ && last_viewed_.is_sentinel())
  {
    end = available.end();
    begin = std::prev(end, std::min<PinionDifferenceType>(max_view_size, bounded_available_size));
  }

  // If this input is configured with a safety marign, check to see if we've
  // violated it. If so, skip forward to the most recent messages.

  skipped_safety_ = apply_safety_margin(available, begin, end);

  // If configured to preemptively skip, check to see if we've fallen far enough
  // behind to jump forward.
  auto num_skipped = apply_skip_threshold(available, begin, end);
  skipped_count_ = num_skipped.value_or(0UL);

  auto range = pinion::to_message_slot_range<const MsgType>(std::ranges::subrange<pinion::SlotRef>(begin, end));
  if (!range)
  {
    return jewels::unexpected{pinion::ProgressError{}};
  }

  msg_count_ = 0;

  {
    size_t index = 0;
    for (const auto& msg_slot : *range)
    {
      if constexpr (copy_inputs)
      {

        auto& copy = copy_inputs_storage_.at(index);
        copy = msg_slot.msg;
        msg_view_storage_[index].message = &copy;
      }
      else
      {
        msg_view_storage_[index].message = &msg_slot.msg;
      }
      if constexpr (expose_seqno)
      {
        msg_view_storage_[index].seqno = msg_slot.slot.header()->sequence_number;
      }
      if constexpr (use_device_ptr)
      {
        // The reinterpret cast is valid since to_message_slot_range already passed.
        msg_view_storage_[index].device_ptr = reinterpret_pointer_cast<const MsgType>(msg_slot.slot.device_ptr());
      }
      metrics_seqnos_[index] = msg_slot.slot.header()->sequence_number;
      ++index;
    }
    msg_count_ = index;
    metrics_seqno_count_ = index;
  }

  const ViewType view{msg_view_storage_.data(), msg_count_};

  // Ensure the cursor is in the view.

  ViewIteratorType input_cursor_it;
  if (input_cursor_.is_sentinel() || input_cursor_ < begin)
  {
    input_cursor_it = view.begin();
  }
  else if (input_cursor_ > end)
  {
    input_cursor_it = view.end();
  }
  else
  {
    input_cursor_it = std::next(view.begin(), std::distance(begin, input_cursor_));
  }

  // Determine the first new message iterator

  ViewIteratorType first_new_it;
  if (last_viewed_.is_sentinel() || last_viewed_ < begin)
  {
    first_new_it = view.begin();
  }
  else if (last_viewed_ > end)
  {
    first_new_it = view.end();
  }
  else
  {
    first_new_it = std::next(view.begin(), std::distance(begin, last_viewed_));
  }

  metrics_cursor_position_ = static_cast<uint64_t>(std::distance(view.begin(), first_new_it));

  // Store the end iterator as the temporary last viewed value. This will be the value
  // we update the last_viewed_ iterator to when `commit()` is called.

  saved_begin_ = begin;
  saved_end_ = end;

  // Create the message dial input

  if (num_skipped)
  {
    return make_dial_result(view, input_cursor_it, first_new_it, *num_skipped);
  }

  return make_dial_result(view, input_cursor_it, first_new_it);
}

template <typename Policy>
auto InputView<Policy>::make_disconnected_dial(const ViewType& view) -> InputDialType
{
  return InputDialType(view, view.end(), view.end(), false);
}

template <typename Policy>
auto InputView<Policy>::make_dial_result(const ViewType& view, ViewIteratorType cursor, ViewIteratorType first_new)
  -> InputDialType
{
  return InputDialType(view, cursor, first_new);
}

template <typename Policy>
auto InputView<Policy>::make_dial_result(
  const ViewType& view, ViewIteratorType cursor, ViewIteratorType first_new, size_t skip_count) -> InputDialType
{
  return InputDialType(view, cursor, first_new, skip_count);
}

template <typename Policy>
bool InputView<Policy>::apply_safety_margin(const auto& available, pinion::SlotRef& begin, pinion::SlotRef& end) const
{
  if constexpr (!safety_margin)
  {
    return false;
  }
  if (!subscriber_)
  {
    return false;
  }
  // Don't apply this policy on first exec or when running offline.
  if (last_viewed_.is_sentinel() || running_offline_)
  {
    return false;
  }

  // Haven't executed yet.
  if (begin.is_sentinel())
  {
    return false;
  }

  // The channel size isn't known when `Policy` is created, we only know it at
  // runtime. So, if the margin is set to -1, clk is requesting the default: we
  // should pick the greater of the view size and half the buffer size.
  auto adjusted_margin = static_cast<PinionDifferenceType>(
    *safety_margin == -1 ? std::max(subscriber_->layout().num_slots / 2U, static_cast<size_t>(max_view_size))
                         : static_cast<size_t>(*safety_margin));

  // We know the producer will never be further ahead of us than the buffer
  // size, unless we've been overrun(which would have caused us to terminate).
  // So, the distance from it to the beginning of this view is just the rest of
  // the buffer.

  auto distance =
    std::distance(available.end(), begin) + static_cast<PinionDifferenceType>(subscriber_->layout().num_slots);
  if (distance < adjusted_margin)
  {
    end = available.end();
    begin = std::prev(end, std::min<PinionDifferenceType>(max_view_size, std::distance(available.begin(), end)));
    return true;
  }
  return false;
}

template <typename Policy>
std::optional<size_t>
InputView<Policy>::apply_skip_threshold(const auto& available, pinion::SlotRef& begin, pinion::SlotRef& end) const
{
  if constexpr (!skip_threshold)
  {
    return std::nullopt;
  }

  // Don't apply this policy on first exec.
  if (last_viewed_.is_sentinel())
  {
    return {0U};
  }

  auto distance = std::distance(end, available.end());
  if (distance >= static_cast<PinionDifferenceType>(*skip_threshold))
  {
    // TODO(OI-3224): we should have a metric that tracks these skips, so we can analyze it offline.
    end = available.end();
    auto old_begin = begin;
    begin = std::prev(
      end,
      std::min<PinionDifferenceType>(
        max_view_size, std::min<PinionDifferenceType>(max_view_size, std::distance(available.begin(), end))));
    return {static_cast<size_t>(begin - old_begin)};
  }

  return {0U};
}

template <typename Policy>
auto InputView<Policy>::commit(const InputDialType& input) -> LastViewedTuple
{
  // Update the cursor input based on the policy.

  if constexpr (Policy::manual_cursor)
  {
    input_cursor_ = std::next(saved_begin_, std::distance(input.get_view().begin(), input.get_cursor()));
  }
  else
  {
    std::ignore = input;
    input_cursor_ = saved_end_;
  }

  // Update the last viewed iterator to the end, everything on the view is considered "seen".

  last_viewed_ = saved_end_;

  // Reset saved state.

  saved_begin_ = {};
  saved_end_ = {};

  return {endpoint_id, last_viewed_};
}

template <typename Policy>
bool InputView<Policy>::is_overrun() const
{
  if constexpr (copy_inputs)
  {
    return false;
  }

  if (saved_begin_.is_sentinel() || !subscriber_)
  {
    return false;
  }
  auto available = subscriber_->available();
  if (saved_begin_ < available.begin())
  {
    jewels::log_cerr_error(
      "Channel '{}' has been overrun. Cog is at {}, producer is at {}",
      Policy::name,
      saved_begin_.index(),
      available.end().index());
    return true;
  }
  return false;
}

template <typename Policy>
bool InputView<Policy>::almost_overrun() const
{
  if constexpr (copy_inputs)
  {
    return false;
  }

  if (saved_begin_.is_sentinel() || !subscriber_ || subscriber_->layout().is_published_once)
  {
    return false;
  }

  auto available = subscriber_->available();
  if (saved_begin_ < available.begin())
  {
    // We've already been overrun.
    jewels::log_cerr_error(
      "Channel '{}' has been overrun. Cog is at {}, producer is at {}",
      Policy::name,
      saved_begin_.index(),
      available.end().index());
    return true;
  }

  // Consider the cog fatally behind if the producer is within ten percent of the
  // buffer size or two times the producer's maximum messages per cycle of the cog's view,
  // whichever is greater.
  static constexpr double buffer_margin_factor{0.1};
  const auto min_margin = static_cast<PinionDifferenceType>(subscriber_->layout().max_msgs_per_exec * 2U);
  const auto margin = std::max(
    static_cast<PinionDifferenceType>(static_cast<double>(subscriber_->layout().num_slots) * buffer_margin_factor),
    min_margin);

  // NOTE: We care about the distance from the producer's position to the
  // beginning of this view. So, we calculate the distance using the end of the
  // available range. Measuring from the beginning of the range can only tell us
  // if we've already been overrun, which we checked above.
  //
  // Given the overrun check above, we know the producer will never be further
  // ahead of us than the buffer size. So, the distance from it to the
  // beginning of this view is just the rest of the buffer.

  auto distance =
    std::distance(available.end(), saved_begin_) + static_cast<PinionDifferenceType>(subscriber_->layout().num_slots);
  if (distance < margin)
  {
    jewels::log_cerr_error(
      "Channel '{}' is dangerously close to being overrun. Producer at {} has exceeded margin of {} to cog at {}",
      Policy::name,
      available.end().index(),
      margin,
      saved_begin_.index());
    return true;
  }
  return false;
}

template <typename Policy>
bool InputView<Policy>::is_published_once_channel_invalid() const
{
  if (subscriber_ && subscriber_->layout().is_published_once)
  {
    if (subscriber_->get_publish_count() > 1)
    {
      return true;
    }
  }
  return false;
}

template <typename Policy>
jewels::time::SyncTime InputView<Policy>::latest_message_time() const
{
  return latest_message_time_;
}

template <typename Policy>
bool InputView<Policy>::did_safety_skip() const
{
  return skipped_safety_;
}

template <typename Policy>
size_t InputView<Policy>::last_skipped_count() const
{
  return skipped_count_;
}

template <typename Policy>
auto InputView<Policy>::prepare_aligned_view(jewels::Out<InputDialType> dial_out, uint64_t target_seqno)
  -> AlignedLookupOutcome
  requires(expose_seqno)
{
  // Binary search of seqno (sorted, monotonically increasing).
  auto seqno_span =
    std::views::transform(std::span(msg_view_storage_.data(), msg_count_), [](const auto& item) { return item.seqno; });
  auto seqno_it = std::lower_bound(seqno_span.begin(), seqno_span.end(), target_seqno);

  if (seqno_it == seqno_span.end() || *seqno_it != target_seqno)
  {
    if (msg_count_ == 0 || target_seqno > seqno_span.back())
    {
      return AlignedLookupResult::pending;
    }
    if constexpr (requires { Policy::name; })
    {
      jewels::log_cerr_warn(
        "Stale aligned_view '{}' target={} stored=[{},{}] count={}",
        Policy::name,
        target_seqno,
        seqno_span.front(),
        seqno_span.back(),
        msg_count_);
    }
    return AlignedLookupResult::stale;
  }

  auto offset = static_cast<size_t>(std::distance(seqno_span.begin(), seqno_it));

  const ViewType view{msg_view_storage_.data() + offset, 1};

  auto first_new =
    (!aligned_cursor_seqno_.has_value() || target_seqno > *aligned_cursor_seqno_) ? view.begin() : view.end();

  *dial_out = InputDialType(view, view.begin(), first_new);
  return AlignedLookupResult::resolved;
}

template <typename Policy>
auto InputView<Policy>::prepare_aligned_range(jewels::Out<InputDialType> dial_out, uint64_t begin_seq, uint64_t end_seq)
  -> AlignedLookupOutcome
  requires(expose_seqno)
{
  if (msg_count_ == 0)
  {
    return AlignedLookupResult::pending;
  }

  auto seqno_span =
    std::views::transform(std::span(msg_view_storage_.data(), msg_count_), [](const auto& item) { return item.seqno; });

  auto begin_it = std::lower_bound(seqno_span.begin(), seqno_span.end(), begin_seq);
  if (begin_it == seqno_span.end() || *begin_it != begin_seq)
  {
    // Classify: if begin_seq is beyond the newest stored seqno, data hasn't arrived yet (pending).
    // Otherwise it was evicted or is a gap within the stored range (stale).
    if (begin_seq > seqno_span.back())
    {
      return AlignedLookupResult::pending;
    }
    if constexpr (requires { Policy::name; })
    {
      jewels::log_cerr_warn(
        "Stale aligned_range_begin '{}' range=[{},{}] stored=[{},{}] count={}",
        Policy::name,
        begin_seq,
        end_seq,
        seqno_span.front(),
        seqno_span.back(),
        msg_count_);
    }
    return AlignedLookupResult::stale;
  }

  auto end_it = std::lower_bound(begin_it, seqno_span.end(), end_seq);
  if (end_it == seqno_span.end() || *end_it != end_seq)
  {
    // begin is present but end is beyond the newest stored seqno (pending)
    // or was evicted/gap (stale).
    if (end_seq > seqno_span.back())
    {
      return AlignedLookupResult::pending;
    }
    if constexpr (requires { Policy::name; })
    {
      jewels::log_cerr_warn(
        "Stale aligned_range_end '{}' range=[{},{}] stored=[{},{}] count={}",
        Policy::name,
        begin_seq,
        end_seq,
        seqno_span.front(),
        seqno_span.back(),
        msg_count_);
    }
    return AlignedLookupResult::stale;
  }
  auto past_end_it = std::next(end_it);
  // past_end_it is one past the last inclusive element.
  // Messages in our range are [begin_it, past_end_it), i.e., [begin_seq, end_seq].

  auto offset = static_cast<size_t>(std::distance(seqno_span.begin(), begin_it));
  auto count = static_cast<size_t>(std::distance(begin_it, past_end_it));

  const ViewType view{msg_view_storage_.data() + offset, count};

  auto first_new_seqno_it =
    aligned_cursor_seqno_.has_value() ? std::upper_bound(begin_it, past_end_it, *aligned_cursor_seqno_) : begin_it;
  auto first_new_offset = std::distance(begin_it, first_new_seqno_it);
  auto first_new = std::next(view.begin(), first_new_offset);

  *dial_out = InputDialType(view, view.begin(), first_new);
  return AlignedLookupResult::resolved;
}

template <typename Policy>
auto InputView<Policy>::prepare_empty_aligned_view() -> InputDialType
  requires(expose_seqno)
{
  const ViewType view{msg_view_storage_.data(), 0};
  return make_dial_result(view, view.begin(), view.end());
}

template <typename Policy>
void InputView<Policy>::advance_aligned_cursor(uint64_t seqno)
  requires(expose_seqno)
{
  if (!aligned_cursor_seqno_.has_value() || seqno > *aligned_cursor_seqno_)
  {
    aligned_cursor_seqno_ = seqno;
  }
}

template <typename Policy>
void InputView<Policy>::reset_saved_state()
{
  saved_begin_ = {};
  saved_end_ = {};
}

template <typename Policy>
std::span<const uint64_t> InputView<Policy>::get_metrics_sequence_numbers() const
{
  return {metrics_seqnos_.data(), metrics_seqno_count_};
}

template <typename Policy>
uint64_t InputView<Policy>::get_metrics_cursor_position() const
{
  return metrics_cursor_position_;
}

} // namespace clockwork
