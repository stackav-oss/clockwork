// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/set_cog_metrics.hh"
namespace clockwork
{

void set_input_channel_event_metrics(
  const InputEventMetrics& input_metrics, Tap<Tachyon<cog::metrics::InputChannelEventMetrics>>& target)
{
  target.set_unseen_messages(input_metrics.num_unseen_messages);
  target.set_staleness(input_metrics.message_staleness);
  target.set_dropped_messages(input_metrics.messages_dropped);
}

void set_input_channel_telemetry_metrics(
  const InputTelemetryMetrics& input_metrics, Tap<Tachyon<cog::metrics::InputChannelTelemetryMetrics>>& target)
{
  populate_tachyon_min_max_mean(input_metrics.num_unseen_messages, target.get_mutable_unseen_messages());
  populate_tachyon_min_max_mean(input_metrics.message_staleness, target.get_mutable_staleness());
  populate_tachyon_min_max_mean(input_metrics.messages_dropped, target.get_mutable_dropped_messages());
}
} // namespace clockwork
