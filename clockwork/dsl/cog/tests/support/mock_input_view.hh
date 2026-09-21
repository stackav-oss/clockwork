// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/cog/input_view.hh"
#include "jewels/memory/memory_resource.hh"

#include <memory_resource>

namespace clockwork::testing::cogs
{

/// Mock implementation of InputView that returns caller-supplied aggregated metrics.
///
/// Uses the offline (channelless) InputView constructor, so no real channel data is needed.
/// Set the metrics to return via set_input_metrics() before exercising the system under test.
template <typename PolicyType>
class MockInputView : public InputView<PolicyType>
{
public:
  explicit MockInputView(jewels::memory::MemoryResource resource) noexcept
    : InputView<PolicyType>(10, resource, false)
  {
  }

  void set_input_metrics(const AggregatedInputMetrics& metrics)
  {
    mocked_input_metrics_ = metrics;
  }

  [[nodiscard]] const AggregatedInputMetrics& get_aggregated_input_metrics() const override
  {
    return mocked_input_metrics_;
  }

  AggregatedInputMetrics mocked_input_metrics_{};

  ~MockInputView() override = default;
  MockInputView(const MockInputView&) = delete;
  MockInputView& operator=(const MockInputView&) = delete;
  MockInputView(MockInputView&&) = delete;
  MockInputView& operator=(MockInputView&&) = delete;
};

} // namespace clockwork::testing::cogs
