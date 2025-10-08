# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for cppcog."""

from pathlib import Path
from typing import Final

from clockwork.dsl.cog import cppcog
from clockwork.dsl.cpp import context
from clockwork.dsl.ir import cog, compiler, importer
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


def test_hellocog_render() -> None:
    diags_include = ""
    diags_enum = ""
    diags_group = ""
    diags_ids = ""
    target_header_str: Final = f"""
#include "clockwork/cog/include_common.hh"
#include "clockwork/diagnostics/report_definitions.hh"
#include "clockwork/diagnostics/reporter.hh"
#include "clockwork/dsl/tests/support/hello_msg_onboard.hh"
#include "clockwork/dsl/tests/support/hellocog.hh"{diags_include}
#include "clockwork/dsl/tests/support/hellocog_dial.hh"
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string> // IWYU pragma: keep
#include <string_view>
#include <sys/types.h>
#include <utility> // IWYU pragma: keep
namespace clockwork {{ template <class> struct Tachyon; }} // IWYU pragma: keep
namespace clockwork {{ template <class> struct Tap; }} // IWYU pragma: keep
struct HelloCogPolicy
{{
    static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.HelloCogPolicy";
    static constexpr auto simulated_execution_duration = ::std::chrono::milliseconds(2);
    static constexpr size_t event_metrics_batch_size = 10;
    using CogDial = HelloCogDial;
    static constexpr auto cog_id = ::jewels::Uuid<::clockwork::common::CogClassId>::from_string("ba6ead69-1442-50c4-a0f0-b5b95b69eb28").value();
    /// MemoryResources ///
    struct MemHelloPolicy
    {{
        using MemoryResourceType = ::jewels::memory::MemoryResource;
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("f8e52d4d-5f5f-53de-bdb3-8a3fa1e39b18").value();
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.MemHelloPolicy";
    }};
    using MemoryResourcesType = ::clockwork::CogMemoryResources<MemHelloPolicy>;
    /// Configs ///
    struct CfgHelloPolicy
    {{
        using ConfigType = Tap<Tachyon<::clockwork::demo::HelloMsg>>;
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("52850b6a-1583-538c-9d37-415a5ff829a1").value();
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.CfgHelloPolicy";
    }};
    using ConfigsType = ::clockwork::CogConfigs<CfgHelloPolicy>;
    /// States ///
    struct RoHelloPolicy
    {{
        using StateType = Tap<Tachyon<::clockwork::demo::HelloMsg>>;
        struct Factory;
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("3f5258ab-0013-5b9a-8923-7f6301cef4af").value();
        static constexpr bool read_only = true;
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.RoHelloPolicy";
    }};
    struct RwHelloPolicy
    {{
        using StateType = Tap<Tachyon<::clockwork::demo::HelloMsg>>;
        struct Factory;
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("edbbd66d-5405-5b03-8bf2-dc69961e8ba0").value();
        static constexpr bool read_only = false;
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.RwHelloPolicy";
    }};
    struct ExternHelloPolicy
    {{
        using StateType = ::clockwork::testing::CxxState;
        struct Factory;
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("d03c7844-88d1-57b2-8ff9-9d57502917f1").value();
        static constexpr bool read_only = true;
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.ExternHelloPolicy";
    }};
    using StatesType = ::clockwork::CogStates<RoHelloPolicy, RwHelloPolicy, ExternHelloPolicy>;
    /// Timers ///
    struct PeriodicPolicy
    {{
        static constexpr int64_t threshold_ns = 500000000;
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("f91ed99f-cc13-50bb-bab6-c99afd1c2972").value();
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.PeriodicPolicy";
    }};
    using TimersType = ::clockwork::CogTimers<PeriodicPolicy>;
    /// Inputs ///
    struct LatestHelloPolicy
    {{
        using MsgType = ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>;
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("a968ab5e-709c-59e8-a5ee-0828f4579a68").value();
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.LatestHelloPolicy";
        static constexpr auto max_view_size = 1U;
        static constexpr std::optional<::ssize_t> safety_margin = std::nullopt;
        static constexpr std::optional<size_t> skip_threshold = std::nullopt;
        static constexpr auto copy_inputs = false;
        static constexpr auto manual_cursor = false;
    }};
    struct HistoryOfHellosPolicy
    {{
        using MsgType = ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>;
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("9330a5aa-0b55-5e02-85e0-8c375452e4a4").value();
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.HistoryOfHellosPolicy";
        static constexpr auto max_view_size = 5U;
        static constexpr std::optional<::ssize_t> safety_margin = -1;
        static constexpr std::optional<size_t> skip_threshold = std::nullopt;
        static constexpr auto copy_inputs = false;
        static constexpr auto manual_cursor = true;
    }};
    using InputsType = ::clockwork::CogInputs<LatestHelloPolicy, HistoryOfHellosPolicy>;
    /// InputConditions ///
    struct AnyMsgPolicy
    {{
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("a968ab5e-709c-59e8-a5ee-0828f4579a68").value();
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.AnyMsgPolicy";
        static constexpr auto bounds_min = 1U;
        static constexpr auto bounds_max = ::std::numeric_limits<uint32_t>::max();
        static constexpr auto condition_type = ::clockwork::InputConditionType::any_message;
    }};
    struct NewMsgPolicy
    {{
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("9330a5aa-0b55-5e02-85e0-8c375452e4a4").value();
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.NewMsgPolicy";
        static constexpr auto bounds_min = 1U;
        static constexpr auto bounds_max = 2U;
        static constexpr auto condition_type = ::clockwork::InputConditionType::new_message;
    }};
    using ConditionsType = ::clockwork::CogConditions<AnyMsgPolicy, NewMsgPolicy>;
    /// Publishers ///
    struct OutWorldPolicy
    {{
        using MsgType = ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>;
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("2d43f6e9-7d99-5c3d-a94b-5a177f24e59b").value();
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.OutWorldPolicy";
        static constexpr bool has_diagnostics = true;
        static constexpr std::optional<::clockwork::RateLimitParameters> rate_limit_params{{}};
    }};
    struct OutGoodbyePolicy
    {{
        using MsgType = ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>;
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("476ad931-244b-5a06-9642-759fa3fee676").value();
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.OutGoodbyePolicy";
        static constexpr bool has_diagnostics = true;
        static constexpr std::optional<::clockwork::RateLimitParameters> rate_limit_params{{}};
    }};
    struct OutMulti1Policy
    {{
        using MsgType = ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>;
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("78a9e421-2dc1-59f1-876c-0757c32d6231").value();
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.OutMulti1Policy";
        static constexpr bool has_diagnostics = true;
        static constexpr std::optional<::clockwork::RateLimitParameters> rate_limit_params{{}};
    }};
    struct OutMulti2Policy
    {{
        using MsgType = ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>;
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("f223f264-3ef4-51b1-8cd2-6dbb3f7ce56b").value();
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.OutMulti2Policy";
        static constexpr bool has_diagnostics = true;
        static constexpr std::optional<::clockwork::RateLimitParameters> rate_limit_params{{}};
    }};
    struct CogTelemetryMetricsPolicy
    {{
        using MsgType = ::clockwork::Tap<::clockwork::Tachyon<::clockwork::testing::cogs::HelloCogTelemetryMetrics>>;
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("53badc19-4431-5db2-b66b-5d2d50a58a38").value();
        static constexpr ::std::string_view name = "@clockwork::clockwork::dsl::tests::support::hellocog.HelloCog.CogTelemetryMetricsPolicy";
        static constexpr bool has_diagnostics = false;
        static constexpr std::optional<::clockwork::RateLimitParameters> rate_limit_params{{}};
    }};
    struct CogEventMetricsPolicy
    {{
        using MsgType = ::clockwork::Tap<::clockwork::Tachyon<::clockwork::testing::cogs::HelloCogEventMetricsBatch>>;
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("f0bf10a4-7bd0-5d92-83a6-f753b3415f33").value();
        static constexpr ::std::string_view name = "@clockwork::clockwork::dsl::tests::support::hellocog.HelloCog.CogEventMetricsPolicy";
        static constexpr bool has_diagnostics = false;
        static constexpr std::optional<::clockwork::RateLimitParameters> rate_limit_params{{}};
    }};
    using PublishersType = ::clockwork::CogPublishers<OutWorldPolicy, OutGoodbyePolicy, OutMulti1Policy, OutMulti2Policy, CogTelemetryMetricsPolicy, CogEventMetricsPolicy>;
    static constexpr size_t telemetry_metrics_index = 4;
    static constexpr size_t event_metrics_index = 5;
    using OutputPublishersType = ::clockwork::CogPublishers<OutWorldPolicy, OutGoodbyePolicy, OutMulti1Policy, OutMulti2Policy>;
    using MetricsPublishersType = ::clockwork::CogPublishers<CogTelemetryMetricsPolicy, CogEventMetricsPolicy>;
    static constexpr auto publish_metrics = true;
    /// Diagnostics ///
    struct DiagnosticsPolicy
    {{
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("04046f47-eaf8-564c-b29b-ee156f2d2eba").value();
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.diagnostics";
        static constexpr ::std::string_view member_name = "diagnostics";
        static constexpr ::std::string_view group_name = "fault_injector_b";
        static constexpr ::std::string_view instance_name = "a";
        using ManagerType = ::clockwork::diagnostics::ClockworkManager<::clockwork::diagnostics::SignalGroupId::fault_injector_b>;
    }};
    using DiagnosticsType = ::clockwork::CogDiagnostics<DiagnosticsPolicy>;
    /// Infra Diagnostics ///{diags_enum}
    struct CogInfraDiagnosticsSignalDefs
    {{{diags_group}
    }};
    struct CogInfraDiagnosticsPolicy
    {{
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("1daaba92-ef09-53ca-a5f1-aabdf7d7cce5").value();
        static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::hellocog.HelloCog.cog_infra_diagnostics";
        static constexpr ::std::string_view member_name = "cog_infra_diagnostics";
        static constexpr ::std::string_view group_name = "cog_infra_diagnostics";
        static constexpr ::std::string_view instance_name{{}};
        template<typename GroupType = CogInfraDiagnosticsSignalDefs>
        using ManagerType = ::clockwork::diagnostics::ClockworkManagerStruct<GroupType>;{diags_ids}
    }};
    using InfraDiagnosticsType = ::clockwork::CogInfraDiagnostics<CogInfraDiagnosticsPolicy>;
    static bool is_ready(CogStatistics& /*statistics*/, typename TimersType::ConditionsTuple& timers, typename ConditionsType::ConditionsTuple& conditions);
    [[nodiscard]] static HelloCogDial make_dial(const CogExecuteParams& params, typename MemoryResourcesType::MemoryResourcesTuple& resources, typename ConfigsType::ConfigsTuple& configs, typename StatesType::StatesTuple& states, typename InputsType::InputDialTuple& inputs, typename PublishersType::PublishablesTuple publishables, typename TimersType::ConditionsTuple& timer_conditions, typename ConditionsType::ConditionsTuple& message_conditions, typename DiagnosticsType::ReporterType& diagnostics);
    static void execute(CogDial& dial);
    static void populate_input_event_metrics(const typename InputsType::SubscribersTuple& inputs, ::clockwork::Tap<::clockwork::Tachyon<HelloCogEventMetricsBatch>>& event_metrics_tachyon);
    static void populate_output_event_metrics(const std::pmr::vector<::clockwork::EventMetrics>& event_metrics, ::clockwork::Tap<::clockwork::Tachyon<HelloCogEventMetricsBatch>>& event_metrics_tachyon);
    static void populate_trigger_mask(const std::pmr::vector<::clockwork::EventMetrics>& event_metrics_vec, ::clockwork::Tap<::clockwork::Tachyon<HelloCogEventMetricsBatch>>& event_metrics_tachyon);
    static void populate_condition_trigger_vals(const std::pmr::vector<uint64_t>& event_metrics_vec, ::clockwork::Tap<::clockwork::Tachyon<HelloCogTelemetryMetrics>>& telemetry_metrics_tachyon);
    static void populate_input_telemetry_metrics(const typename InputsType::SubscribersTuple& inputs, ::clockwork::Tap<::clockwork::Tachyon<HelloCogTelemetryMetrics>>& telemetry_metrics_tachyon);
    static void populate_output_telemetry_metrics(const ::clockwork::TelemetryMetrics& telemetry_metrics, ::clockwork::Tap<::clockwork::Tachyon<HelloCogTelemetryMetrics>>& telemetry_metrics_tachyon);
    [[nodiscard]] static uint8_t get_conditions_mask(const typename TimersType::ConditionsTuple& timers, const typename ConditionsType::ConditionsTuple& input_conditions);
    static void populate_telemetry_triggers(const ::clockwork::TelemetryMetrics& telemetry_metrics, ::clockwork::Tap<::clockwork::Tachyon<HelloCogTelemetryMetrics>>& telemetry_metrics_tachyon);
    static void populate_telemetry_metrics(const ::clockwork::TelemetryMetrics& telemetry_metrics, const typename InputsType::SubscribersTuple& inputs, typename PublishersType::PublishablesTuple& publishables);
    static void populate_event_metrics(const std::pmr::vector<::clockwork::EventMetrics>& event_metrics, const typename InputsType::SubscribersTuple& inputs, typename PublishersType::PublishablesTuple& publishables);
}};
using HelloCog = ::clockwork::SimpleCog<HelloCogPolicy>;
struct HelloCogFactory : ::clockwork::CogFactory
{{
    static constexpr auto type_id = ::jewels::Uuid<::clockwork::common::CogClassId>::from_string("ba6ead69-1442-50c4-a0f0-b5b95b69eb28").value();
    [[nodiscard]] const ::clockwork::CogFactory::IdType &id() const override;
    [[nodiscard]] ::clockwork::CogFactory::Ptr make(::jewels::memory::MemoryResource resource, const ::jewels::Uuid<::clockwork::common::CogInstanceId>& instance_id, ::jewels::memory::ObjectPtr<::clockwork::AbstractCogQueue> queue) const override;
}};
struct HelloCogPolicy::RoHelloPolicy::Factory : ::clockwork::CogStateFactory
{{
    static constexpr auto type_id = ::jewels::Uuid<::clockwork::RepresentationTag>::from_string("bfca902e-3ed9-5764-83b2-773d406cb418").value();
    [[nodiscard]] const ::clockwork::CogStateFactory::IdType &id() const override;
    [[nodiscard]] ::clockwork::CogStateFactory::Ptr make(::jewels::memory::MemoryResource memres_sys, ::clockwork::pinion::PublisherHandle publisher) const override;
}};
struct HelloCogPolicy::RwHelloPolicy::Factory : ::clockwork::CogStateFactory
{{
    static constexpr auto type_id = ::jewels::Uuid<::clockwork::RepresentationTag>::from_string("bfca902e-3ed9-5764-83b2-773d406cb418").value();
    [[nodiscard]] const ::clockwork::CogStateFactory::IdType &id() const override;
    [[nodiscard]] ::clockwork::CogStateFactory::Ptr make(::jewels::memory::MemoryResource memres_sys, ::clockwork::pinion::PublisherHandle publisher) const override;
}};
struct HelloCogPolicy::ExternHelloPolicy::Factory : ::clockwork::CogStateFactory
{{
    static constexpr auto type_id = ::jewels::Uuid<::clockwork::RepresentationTag>::from_string("83c85e31-5b74-5262-b320-11e4fa27bcfd").value();
    [[nodiscard]] const ::clockwork::CogStateFactory::IdType &id() const override;
    [[nodiscard]] ::clockwork::CogStateFactory::Ptr make(::jewels::memory::MemoryResource memres_sys, ::jewels::memory::MemoryResource memres_state) const override;
}};
"""

    target_source_str: Final = """
#include "clockwork/cog/include_common.hh"
#include "clockwork/dsl/tests/support/hellocog_dial.hh"
#include "hello_cog.hh"
#include <tuple>
#include <utility>
auto HelloCogPolicy::is_ready(CogStatistics& /*statistics*/, typename TimersType::ConditionsTuple& timers, typename ConditionsType::ConditionsTuple& conditions) -> bool
{
    return ((static_cast<bool>(::std::get<0>(timers)) && static_cast<bool>(::std::get<1>(conditions))) || static_cast<bool>(::std::get<0>(conditions)));
}
auto HelloCogPolicy::make_dial(const CogExecuteParams& params, typename MemoryResourcesType::MemoryResourcesTuple& resources, typename ConfigsType::ConfigsTuple& configs, typename StatesType::StatesTuple& states, typename InputsType::InputDialTuple& inputs, typename PublishersType::PublishablesTuple publishables, typename TimersType::ConditionsTuple& timer_conditions, typename ConditionsType::ConditionsTuple& message_conditions, typename DiagnosticsType::ReporterType& diagnostics) -> HelloCogDial
{
    return HelloCogDial(
        params.start_time,
        HelloCogDialResources(
            ::jewels::memory::make_non_null_from_ref(::std::get<0>(resources))
        ),
        HelloCogDialConfigs(
            ::jewels::memory::make_non_null_from_ref(::std::get<0>(configs))
        ),
        HelloCogDialStates(
            ::std::get<0>(states),
            ::std::get<1>(states),
            ::std::get<2>(states)
        ),
        HelloCogDialConditions(
            ::jewels::memory::make_non_null_from_ref(::std::get<0>(message_conditions)),
            ::jewels::memory::make_non_null_from_ref(::std::get<1>(message_conditions)),
            ::jewels::memory::make_non_null_from_ref(::std::get<0>(timer_conditions))
        ),
        HelloCogDialInputs(
            ::jewels::memory::make_non_null_from_ref(::std::get<0>(inputs)),
            ::jewels::memory::make_non_null_from_ref(::std::get<1>(inputs))
        ),
        HelloCogDialOutputs(
            ::jewels::memory::make_non_null_from_ref(::std::get<0>(publishables)),
            ::jewels::memory::make_non_null_from_ref(::std::get<1>(publishables)),
            ::jewels::memory::make_non_null_from_ref(::std::get<2>(publishables)),
            ::jewels::memory::make_non_null_from_ref(::std::get<3>(publishables))
        ),
        ::jewels::memory::make_non_null_from_ref(diagnostics)
    );
}
auto HelloCogPolicy::execute(CogDial& dial) -> void
{
    execute_cog(dial);
}
auto HelloCogPolicy::populate_input_event_metrics(const typename InputsType::SubscribersTuple& inputs, ::clockwork::Tap<::clockwork::Tachyon<HelloCogEventMetricsBatch>>& event_metrics_tachyon) -> void
{
    auto& latest_hello_event_metrics = std::get<0>(inputs)->get_aggregated_input_metrics().event_metrics;
    if (event_metrics_tachyon.get_underlying_event_metrics().size() < latest_hello_event_metrics.size())
    {
        event_metrics_tachyon.get_underlying_event_metrics().resize(latest_hello_event_metrics.size());
    }
    for (size_t i = 0; i < latest_hello_event_metrics.size(); ++i)
    {
        set_input_channel_event_metrics(latest_hello_event_metrics.at(i), event_metrics_tachyon.get_mutable_event_metrics()[i].get_mutable_latest_hello());
    }
    std::get<0>(inputs)->reset_metrics();
    auto& history_of_hellos_event_metrics = std::get<1>(inputs)->get_aggregated_input_metrics().event_metrics;
    if (event_metrics_tachyon.get_underlying_event_metrics().size() < history_of_hellos_event_metrics.size())
    {
        event_metrics_tachyon.get_underlying_event_metrics().resize(history_of_hellos_event_metrics.size());
    }
    for (size_t i = 0; i < history_of_hellos_event_metrics.size(); ++i)
    {
        set_input_channel_event_metrics(history_of_hellos_event_metrics.at(i), event_metrics_tachyon.get_mutable_event_metrics()[i].get_mutable_history_of_hellos());
    }
    std::get<1>(inputs)->reset_metrics();
}
auto HelloCogPolicy::populate_output_event_metrics(const std::pmr::vector<::clockwork::EventMetrics>& event_metrics, ::clockwork::Tap<::clockwork::Tachyon<HelloCogEventMetricsBatch>>& event_metrics_tachyon) -> void
{
    if (event_metrics_tachyon.get_underlying_event_metrics().size() < event_metrics.size())
    {
        event_metrics_tachyon.get_underlying_event_metrics().resize(event_metrics.size());
    }
    for (size_t i = 0; i < event_metrics.size(); ++i)
    {
        if (event_metrics.at(i).output_metrics.contains(0))
        {
            event_metrics_tachyon.get_mutable_event_metrics()[i].set_out_world_num_messages(event_metrics.at(i).output_metrics.at(0));
        }
        else
        {
            event_metrics_tachyon.get_mutable_event_metrics()[i].set_out_world_num_messages(0);
        }
        if (event_metrics.at(i).output_metrics.contains(1))
        {
            event_metrics_tachyon.get_mutable_event_metrics()[i].set_out_goodbye_num_messages(event_metrics.at(i).output_metrics.at(1));
        }
        else
        {
            event_metrics_tachyon.get_mutable_event_metrics()[i].set_out_goodbye_num_messages(0);
        }
        if (event_metrics.at(i).output_metrics.contains(2))
        {
            event_metrics_tachyon.get_mutable_event_metrics()[i].set_out_multi1_num_messages(event_metrics.at(i).output_metrics.at(2));
        }
        else
        {
            event_metrics_tachyon.get_mutable_event_metrics()[i].set_out_multi1_num_messages(0);
        }
        if (event_metrics.at(i).output_metrics.contains(3))
        {
            event_metrics_tachyon.get_mutable_event_metrics()[i].set_out_multi2_num_messages(event_metrics.at(i).output_metrics.at(3));
        }
        else
        {
            event_metrics_tachyon.get_mutable_event_metrics()[i].set_out_multi2_num_messages(0);
        }
    }
}
auto HelloCogPolicy::populate_trigger_mask(const std::pmr::vector<::clockwork::EventMetrics>& event_metrics_vec, ::clockwork::Tap<::clockwork::Tachyon<HelloCogEventMetricsBatch>>& event_metrics_tachyon) -> void
{
    if (event_metrics_tachyon.get_underlying_event_metrics().size() < event_metrics_vec.size())
    {
        event_metrics_tachyon.get_underlying_event_metrics().resize(event_metrics_vec.size());
    }
    for (size_t i = 0; i < event_metrics_vec.size(); ++i)
    {
        event_metrics_tachyon.get_mutable_event_metrics()[i].set_trigger_flags(static_cast<HelloCogConditionsMask>(event_metrics_vec.at(i).conditions_mask));
    }
}
auto HelloCogPolicy::populate_condition_trigger_vals(const std::pmr::vector<uint64_t>& event_metrics_vec, ::clockwork::Tap<::clockwork::Tachyon<HelloCogTelemetryMetrics>>& telemetry_metrics_tachyon) -> void
{
    uint16_t periodic_count = 0;
    uint16_t any_msg_count = 0;
    uint16_t new_msg_count = 0;
    for (const auto& condition_mask : event_metrics_vec)
    {
        auto trigger_flags = static_cast<HelloCogConditionsMask>(condition_mask);
        if ((trigger_flags & HelloCogConditionsMask::periodic) != HelloCogConditionsMask::no_conditions_active)
        {
            ++periodic_count;
        }
        if ((trigger_flags & HelloCogConditionsMask::any_msg) != HelloCogConditionsMask::no_conditions_active)
        {
            ++any_msg_count;
        }
        if ((trigger_flags & HelloCogConditionsMask::new_msg) != HelloCogConditionsMask::no_conditions_active)
        {
            ++new_msg_count;
        }
    }
    telemetry_metrics_tachyon.set_periodic_trigger_vals(periodic_count);
    telemetry_metrics_tachyon.set_any_msg_trigger_vals(any_msg_count);
    telemetry_metrics_tachyon.set_new_msg_trigger_vals(new_msg_count);
}
auto HelloCogPolicy::populate_input_telemetry_metrics(const typename InputsType::SubscribersTuple& inputs, ::clockwork::Tap<::clockwork::Tachyon<HelloCogTelemetryMetrics>>& telemetry_metrics_tachyon) -> void
{
    set_input_channel_telemetry_metrics(std::get<0>(inputs)->get_aggregated_input_metrics().telemetry_metrics, telemetry_metrics_tachyon.get_mutable_latest_hello());
    set_input_channel_telemetry_metrics(std::get<1>(inputs)->get_aggregated_input_metrics().telemetry_metrics, telemetry_metrics_tachyon.get_mutable_history_of_hellos());
}
auto HelloCogPolicy::populate_output_telemetry_metrics(const ::clockwork::TelemetryMetrics& telemetry_metrics, ::clockwork::Tap<::clockwork::Tachyon<HelloCogTelemetryMetrics>>& telemetry_metrics_tachyon) -> void
{
    if (auto it = telemetry_metrics.output_metrics.find(0); it != telemetry_metrics.output_metrics.end())
    {
        populate_tachyon_min_max_mean(it->second, telemetry_metrics_tachyon.get_mutable_out_world_num_messages());
    }
    if (auto it = telemetry_metrics.output_metrics.find(1); it != telemetry_metrics.output_metrics.end())
    {
        populate_tachyon_min_max_mean(it->second, telemetry_metrics_tachyon.get_mutable_out_goodbye_num_messages());
    }
    if (auto it = telemetry_metrics.output_metrics.find(2); it != telemetry_metrics.output_metrics.end())
    {
        populate_tachyon_min_max_mean(it->second, telemetry_metrics_tachyon.get_mutable_out_multi1_num_messages());
    }
    if (auto it = telemetry_metrics.output_metrics.find(3); it != telemetry_metrics.output_metrics.end())
    {
        populate_tachyon_min_max_mean(it->second, telemetry_metrics_tachyon.get_mutable_out_multi2_num_messages());
    }
}
auto HelloCogPolicy::get_conditions_mask(const typename TimersType::ConditionsTuple& timers, const typename ConditionsType::ConditionsTuple& input_conditions) -> uint8_t
{
    uint8_t condition_mask{};
    auto& periodic_handle = std::get<0>(timers);
    if (periodic_handle.is_active())
    {
        condition_mask = condition_mask | static_cast<uint8_t>( HelloCogConditionsMask::periodic);
    }
    auto& any_msg_handle = std::get<0>(input_conditions);
    if (any_msg_handle.is_active())
    {
        condition_mask = condition_mask | static_cast<uint8_t>(HelloCogConditionsMask::any_msg);
    }
    auto& new_msg_handle = std::get<1>(input_conditions);
    if (new_msg_handle.is_active())
    {
        condition_mask = condition_mask | static_cast<uint8_t>(HelloCogConditionsMask::new_msg);
    }
    return condition_mask;
}
auto HelloCogPolicy::populate_telemetry_triggers(const ::clockwork::TelemetryMetrics& telemetry_metrics, ::clockwork::Tap<::clockwork::Tachyon<HelloCogTelemetryMetrics>>& telemetry_metrics_tachyon) -> void
{
    uint16_t periodic_count = 0;
    uint16_t any_msg_count = 0;
    uint16_t new_msg_count = 0;
    for (const auto& condition_mask : telemetry_metrics.conditions_mask_vector)
    {
        auto trigger_flags = static_cast<HelloCogConditionsMask>(condition_mask);
        if ((trigger_flags & HelloCogConditionsMask::periodic) != HelloCogConditionsMask::no_conditions_active)
        {
            ++periodic_count;
        }
        if ((trigger_flags & HelloCogConditionsMask::any_msg) != HelloCogConditionsMask::no_conditions_active)
        {
            ++any_msg_count;
        }
        if ((trigger_flags & HelloCogConditionsMask::new_msg) != HelloCogConditionsMask::no_conditions_active)
        {
            ++new_msg_count;
        }
    }
    telemetry_metrics_tachyon.set_periodic_trigger_vals(periodic_count);
    telemetry_metrics_tachyon.set_any_msg_trigger_vals(any_msg_count);
    telemetry_metrics_tachyon.set_new_msg_trigger_vals(new_msg_count);
}
auto HelloCogPolicy::populate_telemetry_metrics(const ::clockwork::TelemetryMetrics& telemetry_metrics, const typename InputsType::SubscribersTuple& inputs, typename PublishersType::PublishablesTuple& publishables) -> void
{
    auto telemetry_publishable = std::get<telemetry_metrics_index>(publishables);
    auto & telemetry_metrics_msg = telemetry_publishable.message();
    set_common_telemetry_metrics(telemetry_metrics_msg, telemetry_metrics);
    populate_input_telemetry_metrics(inputs, telemetry_metrics_msg);
    populate_output_telemetry_metrics(telemetry_metrics, telemetry_metrics_msg);
    populate_telemetry_triggers(telemetry_metrics, telemetry_metrics_msg);
    telemetry_publishable.mark_for_publish();
}
auto HelloCogPolicy::populate_event_metrics(const std::pmr::vector<::clockwork::EventMetrics>& event_metrics, const typename InputsType::SubscribersTuple& inputs, typename PublishersType::PublishablesTuple& publishables) -> void
{
    auto event_publishable = std::get<event_metrics_index>(publishables);
    auto & event_metrics_msg = event_publishable.message();
    set_common_event_metrics(event_metrics, event_metrics_msg);
    populate_input_event_metrics(inputs, event_metrics_msg);
    populate_output_event_metrics(event_metrics, event_metrics_msg);
    populate_trigger_mask(event_metrics, event_metrics_msg);
    event_publishable.mark_for_publish();
}
const ::clockwork::CogFactory::IdType &HelloCogFactory::id() const
{
    return type_id;
}
::clockwork::CogFactory::Ptr HelloCogFactory::make(::jewels::memory::MemoryResource resource, const ::jewels::Uuid<::clockwork::common::CogInstanceId>& instance_id, ::jewels::memory::ObjectPtr<::clockwork::AbstractCogQueue> queue) const
{
    return ::jewels::memory::make_pmr_shared<::clockwork::SimpleCog<HelloCogPolicy>>(resource, resource, instance_id, queue);
}
static HelloCogFactory hello_cog_factory_inst;
const ::clockwork::CogStateFactory::IdType &HelloCogPolicy::RoHelloPolicy::Factory::id() const
{
    return type_id;
}
::clockwork::CogStateFactory::Ptr HelloCogPolicy::RoHelloPolicy::Factory::make(::jewels::memory::MemoryResource memres_sys, ::clockwork::pinion::PublisherHandle publisher) const
{
    return ::jewels::memory::make_pmr_shared<::clockwork::CogStateDataImpl<Tap<Tachyon<::clockwork::demo::HelloMsg>>>>(memres_sys, std::move(publisher));
}
static HelloCogPolicy::RoHelloPolicy::Factory hello_cog_policy__ro_hello_policy___factory_inst;
const ::clockwork::CogStateFactory::IdType &HelloCogPolicy::RwHelloPolicy::Factory::id() const
{
    return type_id;
}
::clockwork::CogStateFactory::Ptr HelloCogPolicy::RwHelloPolicy::Factory::make(::jewels::memory::MemoryResource memres_sys, ::clockwork::pinion::PublisherHandle publisher) const
{
    return ::jewels::memory::make_pmr_shared<::clockwork::CogStateDataImpl<Tap<Tachyon<::clockwork::demo::HelloMsg>>>>(memres_sys, std::move(publisher));
}
static HelloCogPolicy::RwHelloPolicy::Factory hello_cog_policy__rw_hello_policy___factory_inst;
const ::clockwork::CogStateFactory::IdType &HelloCogPolicy::ExternHelloPolicy::Factory::id() const
{
    return type_id;
}
::clockwork::CogStateFactory::Ptr HelloCogPolicy::ExternHelloPolicy::Factory::make(::jewels::memory::MemoryResource memres_sys, ::jewels::memory::MemoryResource memres_state) const
{
    return ::jewels::memory::make_pmr_shared<::clockwork::CogStateDataImpl<::clockwork::testing::CxxState>>(memres_sys, std::move(memres_state));
}
static HelloCogPolicy::ExternHelloPolicy::Factory hello_cog_policy__extern_hello_policy___factory_inst;
"""
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellocog.clk")),
        importer=importer.FilesystemImporter(compile_fn=compiler.compile_source_file),
    )
    cog_ir = module.inner_scope.lookup("HelloCog")
    dial_header = context.Header(CLK_REPO, "clockwork/dsl/tests/support/hellocog_dial.hh")
    assert cog_ir is not None
    assert isinstance(cog_ir, cog.Cog)
    cpp_cog = cppcog.Cog.make(
        cog_ir=cog_ir,
        class_name="HelloCog",
        dial_name=None,
        header_name="hello_cog.hh",
        cpp_namespace="clockwork",
        dial_header=dial_header,
    )
    cpp_mod = cpp_cog.render()
    assert cpp_mod.header_chunk.render_str(render_includes=True).strip() == target_header_str.strip()
    assert cpp_mod.implementation_chunk.render_str(render_includes=True).strip() == target_source_str.strip()


def test_goodbyecog_render() -> None:
    diags_include = ""
    diags_enum = ""
    diags_group = ""
    diags_ids = ""
    target_header_str: Final = f"""
#include "clockwork/cog/include_common.hh"{diags_include}
#include "clockwork/dsl/tests/support/goodbyecog_dial.hh"
#include <chrono>
#include <string> // IWYU pragma: keep
#include <string_view>
#include <utility> // IWYU pragma: keep
struct GoodbyeCogPolicy
{{
    static constexpr ::std::string_view name = "@{CLK_REPO}::clockwork::dsl::tests::support::goodbyecog.GoodbyeCog.GoodbyeCogPolicy";
    static constexpr auto simulated_execution_duration = ::std::chrono::milliseconds(1);
    static constexpr size_t event_metrics_batch_size = 10;
    using CogDial = GoodbyeCogDial;
    static constexpr auto cog_id = ::jewels::Uuid<::clockwork::common::CogClassId>::from_string("357596a4-e400-5585-8c5d-26c7bcb6b5f7").value();
    /// MemoryResources ///
    using MemoryResourcesType = ::clockwork::CogMemoryResources<>;
    /// Configs ///
    using ConfigsType = ::clockwork::CogConfigs<>;
    /// States ///
    using StatesType = ::clockwork::CogStates<>;
    /// Timers ///
    using TimersType = ::clockwork::CogTimers<>;
    /// Inputs ///
    using InputsType = ::clockwork::CogInputs<>;
    /// InputConditions ///
    using ConditionsType = ::clockwork::CogConditions<>;
    /// Publishers ///
    using PublishersType = ::clockwork::CogPublishers<>;
    using OutputPublishersType = ::clockwork::CogPublishers<>;
    using MetricsPublishersType = ::clockwork::CogPublishers<>;
    static constexpr auto publish_metrics = false;
    /// Diagnostics ///
    using DiagnosticsType = ::clockwork::CogDiagnostics<>;
    /// Infra Diagnostics ///{diags_enum}
    struct CogInfraDiagnosticsSignalDefs
    {{{diags_group}
    }};
    struct CogInfraDiagnosticsPolicy
    {{
        static constexpr auto endpoint_id = ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("f798b6d0-48cc-5acf-8ca8-bb0eb601968f").value();
        static constexpr ::std::string_view name = "@clockwork::clockwork::dsl::tests::support::goodbyecog.GoodbyeCog.cog_infra_diagnostics";
        static constexpr ::std::string_view member_name = "cog_infra_diagnostics";
        static constexpr ::std::string_view group_name = "cog_infra_diagnostics";
        static constexpr ::std::string_view instance_name{{}};
        template<typename GroupType = CogInfraDiagnosticsSignalDefs>
        using ManagerType = ::clockwork::diagnostics::ClockworkManagerStruct<GroupType>;{diags_ids}
    }};
    using InfraDiagnosticsType = ::clockwork::CogInfraDiagnostics<CogInfraDiagnosticsPolicy>;
    static bool is_ready(CogStatistics& statistics, typename TimersType::ConditionsTuple& /*timers*/, typename ConditionsType::ConditionsTuple& /*conditions*/);
    [[nodiscard]] static GoodbyeCogDial make_dial(const CogExecuteParams& params, typename MemoryResourcesType::MemoryResourcesTuple& /*resources*/, typename ConfigsType::ConfigsTuple& /*configs*/, typename StatesType::StatesTuple& /*states*/, typename InputsType::InputDialTuple& /*inputs*/, typename PublishersType::PublishablesTuple /*publishables*/, typename TimersType::ConditionsTuple& /*timer_conditions*/, typename ConditionsType::ConditionsTuple& /*message_conditions*/, typename DiagnosticsType::ReporterType& /*diagnostics*/);
    static void execute(CogDial& dial);
}};
using GoodbyeCog = ::clockwork::SimpleCog<GoodbyeCogPolicy>;
struct GoodbyeCogFactory : ::clockwork::CogFactory
{{
    static constexpr auto type_id = ::jewels::Uuid<::clockwork::common::CogClassId>::from_string("357596a4-e400-5585-8c5d-26c7bcb6b5f7").value();
    [[nodiscard]] const ::clockwork::CogFactory::IdType &id() const override;
    [[nodiscard]] ::clockwork::CogFactory::Ptr make(::jewels::memory::MemoryResource resource, const ::jewels::Uuid<::clockwork::common::CogInstanceId>& instance_id, ::jewels::memory::ObjectPtr<::clockwork::AbstractCogQueue> queue) const override;
}};
"""

    target_source_str: Final = """
#include "clockwork/cog/include_common.hh"
#include "clockwork/dsl/tests/support/goodbyecog_dial.hh"
#include "goodbye_cog.hh"
#include <tuple>
#include <utility>
auto GoodbyeCogPolicy::is_ready(CogStatistics& statistics, typename TimersType::ConditionsTuple& /*timers*/, typename ConditionsType::ConditionsTuple& /*conditions*/) -> bool
{
    return (statistics.num_executions_ == 0);
}
auto GoodbyeCogPolicy::make_dial(const CogExecuteParams& params, typename MemoryResourcesType::MemoryResourcesTuple& /*resources*/, typename ConfigsType::ConfigsTuple& /*configs*/, typename StatesType::StatesTuple& /*states*/, typename InputsType::InputDialTuple& /*inputs*/, typename PublishersType::PublishablesTuple /*publishables*/, typename TimersType::ConditionsTuple& /*timer_conditions*/, typename ConditionsType::ConditionsTuple& /*message_conditions*/, typename DiagnosticsType::ReporterType& /*diagnostics*/) -> GoodbyeCogDial
{
    return GoodbyeCogDial(
        params.start_time,
        GoodbyeCogDialResources(
        ),
        GoodbyeCogDialConfigs(
        ),
        GoodbyeCogDialStates(
        ),
        GoodbyeCogDialConditions(
        ),
        GoodbyeCogDialInputs(
        ),
        GoodbyeCogDialOutputs(
        ),
        GoodbyeCogDialDiagnostics(
        )
    );
}
auto GoodbyeCogPolicy::execute(CogDial& dial) -> void
{
    execute_cog(dial);
}
const ::clockwork::CogFactory::IdType &GoodbyeCogFactory::id() const
{
    return type_id;
}
::clockwork::CogFactory::Ptr GoodbyeCogFactory::make(::jewels::memory::MemoryResource resource, const ::jewels::Uuid<::clockwork::common::CogInstanceId>& instance_id, ::jewels::memory::ObjectPtr<::clockwork::AbstractCogQueue> queue) const
{
    return ::jewels::memory::make_pmr_shared<::clockwork::SimpleCog<GoodbyeCogPolicy>>(resource, resource, instance_id, queue);
}
static GoodbyeCogFactory goodbye_cog_factory_inst;
"""
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/goodbyecog.clk")),
        importer=importer.FilesystemImporter(compile_fn=compiler.compile_source_file),
    )
    cog_ir = module.inner_scope.lookup("GoodbyeCog")
    dial_header = context.Header(CLK_REPO, "clockwork/dsl/tests/support/goodbyecog_dial.hh")
    assert cog_ir is not None
    assert isinstance(cog_ir, cog.Cog)
    cpp_cog = cppcog.Cog.make(
        cog_ir=cog_ir,
        class_name="GoodbyeCog",
        dial_name=None,
        header_name="goodbye_cog.hh",
        cpp_namespace="clockwork",
        dial_header=dial_header,
    )
    cpp_mod = cpp_cog.render()
    assert cpp_mod.header_chunk.render_str(render_includes=True).strip() == target_header_str.strip()
    assert cpp_mod.implementation_chunk.render_str(render_includes=True).strip() == target_source_str.strip()
