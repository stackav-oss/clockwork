// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/cog/cog_conditions.hh"
#include "clockwork/cog/cog_configs.hh"
#include "clockwork/cog/cog_diagnostics.hh"
#include "clockwork/cog/cog_infra_diagnostics.hh"
#include "clockwork/cog/cog_inputs.hh"
#include "clockwork/cog/cog_memory_resources.hh"
#include "clockwork/cog/cog_publishers.hh"
#include "clockwork/cog/cog_states.hh"
#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/cog/cog_timers.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/diagnostics/reporter.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/meta/integer_sequence.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <gsl/util>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>

namespace clockwork::testing
{

template <size_t n_input, size_t n_output>
struct FakeCogInfraDiagnostics
{
  struct CogInfraDiagnosticsSignalDefs
  {
  };
  struct CogInfraDiagnosticsPolicy
  {
    static constexpr auto endpoint_id =
      ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("6dbf64ae-9805-46e2-85f8-ff8e7093e17a").value();
    static constexpr ::std::string_view name = "FakeCog.cog_infra_diagnostics";
    static constexpr ::std::string_view member_name = "cog_infra_diagnostics";
    static constexpr ::std::string_view group_name = "cog_infra_diagnostics";
    static constexpr ::std::string_view instance_name{};
    template <typename GroupType = CogInfraDiagnosticsSignalDefs>
    using ManagerType = ::clockwork::diagnostics::ClockworkManagerStruct<GroupType>;
  };
};

/// Simple "empty" cog policy cogs ca use by overriding only what they need
template <size_t n_input, size_t n_output>
struct FakeCogPolicy
{
  static constexpr size_t event_metrics_batch_size = 10U;
  static constexpr auto simulated_execution_duration = std::chrono::milliseconds(0);
  static constexpr auto publish_metrics = false;

  using MemoryResourcesType = CogMemoryResources<>;
  using ConfigsType = CogConfigs<>;
  using StatesType = CogStates<>;
  using TimersType = CogTimers<>;
  using InputsType = CogInputs<>;
  using ConditionsType = CogConditions<>;
  using PublishersType = CogPublishers<>;
  using DiagnosticsType = CogDiagnostics<>;
  using InfraDiagnosticsType =
    CogInfraDiagnostics<typename FakeCogInfraDiagnostics<n_input, n_output>::CogInfraDiagnosticsPolicy>;
  struct SignalApiType
  {
  };

  [[nodiscard]] static bool is_ready(
    CogStatistics& /*statistics*/,
    typename TimersType::ConditionsTuple& /*timers*/,
    typename ConditionsType::ConditionsTuple& /*conditions*/)
  {
    return true;
  }

  template <typename PublishablesTuple>
  [[nodiscard]] static auto publish_report_groups(SignalApiType& /*signals*/, PublishablesTuple& /*publishables*/)
    -> jewels::BinaryOutcome
  {
    return jewels::success;
  }
};

} // namespace clockwork::testing
