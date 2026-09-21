# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for cppdial."""

from pathlib import Path

import pytest
from clockwork.dsl.cog import cppdial
from clockwork.dsl.cog.tests.support import test_helpers
from clockwork.dsl.cpp.context import Header
from clockwork.dsl.ir import cog, compiler, importer, statement
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


def test_hellocog_dial_render() -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellocog.clk")),
        importer=importer.FilesystemImporter(compile_fn=compiler.compile_source_file),
    )
    cog_ir = module.inner_scope.lookup("HelloCog")
    assert cog_ir is not None
    assert isinstance(cog_ir, cog.Cog)
    dial = cppdial.Dial(
        cog_ir=cog_ir,
        class_name="HelloCogDial",
        cpp_namespace="clockwork::hellocog",
        dial_header=Header(CLK_REPO, Path("clockwork/dsl/tests/support/hellocog_dial.hh")),
    )
    cpp_mod = dial.render()

    assert (
        cpp_mod.header_chunk.render_str(render_includes=True).strip()
        == """
#include "clockwork/diagnostics/report_definitions.hh"
#include "clockwork/diagnostics/reporter.hh"
#include "clockwork/dial/include_common.hh"
#include "clockwork/dsl/tests/support/cxx_state.hh" // IWYU pragma: export
#include "clockwork/dsl/tests/support/hello_msg_onboard.hh"
#include <array>
#include <cstdint>
#include <functional>
namespace clockwork { template <class> struct Tachyon; } // IWYU pragma: keep
/// HelloCogDialResources
struct HelloCogDialResources
{
public:
    /// Constructor.
    explicit HelloCogDialResources(::jewels::memory::ObjectPtr<::jewels::memory::MemoryResource> mem_hello);
    /// Get mem_hello.
    [[nodiscard]] ::jewels::memory::MemoryResource& get_mem_hello();
private:
    /// mem_hello.
    ::jewels::memory::ObjectPtr<::jewels::memory::MemoryResource> mem_hello_;
};
/// HelloCogDialConfigs
struct HelloCogDialConfigs
{
public:
    /// Constructor.
    explicit HelloCogDialConfigs(::jewels::memory::ObjectPtr<const ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>> cfg_hello);
    /// Get cfg_hello.
    [[nodiscard]] const ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>& get_cfg_hello() const;
private:
    /// cfg_hello.
    ::jewels::memory::ObjectPtr<const ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>> cfg_hello_;
};
/// HelloCogDialStates
struct HelloCogDialStates
{
public:
    /// Constructor.
    HelloCogDialStates(::jewels::memory::ObjectPtr<const ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>> ro_hello, ::jewels::memory::ObjectPtr<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>> rw_hello, ::jewels::memory::ObjectPtr<const ::clockwork::testing::CxxState> extern_hello);
    /// Get ro_hello.
    [[nodiscard]] const ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>& get_ro_hello() const;
    /// Get rw_hello.
    [[nodiscard]] ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>& get_rw_hello();
    /// Get rw_hello.
    [[nodiscard]] const ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>& get_rw_hello() const;
    /// Get extern_hello.
    [[nodiscard]] const ::clockwork::testing::CxxState& get_extern_hello() const;
private:
    /// ro_hello.
    ::jewels::memory::ObjectPtr<const ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>> ro_hello_;
    /// rw_hello.
    ::jewels::memory::ObjectPtr<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>> rw_hello_;
    /// extern_hello.
    ::jewels::memory::ObjectPtr<const ::clockwork::testing::CxxState> extern_hello_;
};
/// HelloCogDialConditions
struct HelloCogDialConditions
{
public:
    /// Constructor.
    HelloCogDialConditions(::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>> any_msg, ::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 2U>> new_msg, ::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>> new_multi_connect_hello__0, ::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>> new_multi_connect_hello__1, ::jewels::memory::ObjectPtr<const ::clockwork::TimeSinceLastExecCondition<500'000'000U>> periodic);
    /// Get any_msg.
    [[nodiscard]] const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>& get_any_msg() const;
    /// Get new_msg.
    [[nodiscard]] const ::clockwork::MessagePresentCondition<1U, 2U>& get_new_msg() const;
    /// Get new_multi_connect_hello__0.
    [[nodiscard]] const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>& get_new_multi_connect_hello__0() const;
    /// Get new_multi_connect_hello__1.
    [[nodiscard]] const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>& get_new_multi_connect_hello__1() const;
    /// Get periodic.
    [[nodiscard]] const ::clockwork::TimeSinceLastExecCondition<500'000'000U>& get_periodic() const;
private:
    /// any_msg.
    ::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>> any_msg_;
    /// new_msg.
    ::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 2U>> new_msg_;
    /// new_multi_connect_hello__0.
    ::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>> new_multi_connect_hello__0_;
    /// new_multi_connect_hello__1.
    ::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>> new_multi_connect_hello__1_;
    /// periodic.
    ::jewels::memory::ObjectPtr<const ::clockwork::TimeSinceLastExecCondition<500'000'000U>> periodic_;
};
/// HelloCogDialInputs
struct HelloCogDialInputs
{
public:
    /// Constructor.
    HelloCogDialInputs(::jewels::memory::ObjectPtr<const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, 0U, 0U, false, false, false>> latest_hello, ::jewels::memory::ObjectPtr<const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, 0U, 0U, false, false, false>> multi_connect_hello__0, ::jewels::memory::ObjectPtr<const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, 0U, 0U, false, false, false>> multi_connect_hello__1, ::jewels::memory::ObjectPtr<::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 5U, 0U, 0U, true, false, false>> history_of_hellos);
    /// Get latest_hello.
    [[nodiscard]] const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, 0U, 0U, false, false, false>& get_latest_hello() const;
    /// Get history_of_hellos.
    [[nodiscard]] ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 5U, 0U, 0U, true, false, false>& get_history_of_hellos();
    /// Get history_of_hellos.
    [[nodiscard]] const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 5U, 0U, 0U, true, false, false>& get_history_of_hellos() const;
    /// Get multi_connect_hello.
    [[nodiscard]] const ::std::array<::std::reference_wrapper<const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, 0U, 0U, false, false, false>>, 2> get_multi_connect_hello() const;
private:
    /// latest_hello.
    ::jewels::memory::ObjectPtr<const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, 0U, 0U, false, false, false>> latest_hello_;
    /// multi_connect_hello__0.
    ::jewels::memory::ObjectPtr<const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, 0U, 0U, false, false, false>> multi_connect_hello__0_;
    /// multi_connect_hello__1.
    ::jewels::memory::ObjectPtr<const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, 0U, 0U, false, false, false>> multi_connect_hello__1_;
    /// history_of_hellos.
    ::jewels::memory::ObjectPtr<::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 5U, 0U, 0U, true, false, false>> history_of_hellos_;
};
/// HelloCogDialOutputs
struct HelloCogDialOutputs
{
public:
    /// Constructor.
    HelloCogDialOutputs(::jewels::memory::ObjectPtr<::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>> out_world, ::jewels::memory::ObjectPtr<::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>> out_goodbye, ::jewels::memory::ObjectPtr<::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>> out_multi1, ::jewels::memory::ObjectPtr<::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>> out_multi2);
    /// Get out_world.
    [[nodiscard]] ::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>& get_out_world();
    /// Get out_world.
    [[nodiscard]] const ::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>& get_out_world() const;
    /// Get out_goodbye.
    [[nodiscard]] ::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>& get_out_goodbye();
    /// Get out_goodbye.
    [[nodiscard]] const ::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>& get_out_goodbye() const;
    /// Get out_multi1.
    [[nodiscard]] ::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>& get_out_multi1();
    /// Get out_multi1.
    [[nodiscard]] const ::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>& get_out_multi1() const;
    /// Get out_multi2.
    [[nodiscard]] ::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>& get_out_multi2();
    /// Get out_multi2.
    [[nodiscard]] const ::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>& get_out_multi2() const;
private:
    /// out_world.
    ::jewels::memory::ObjectPtr<::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>> out_world_;
    /// out_goodbye.
    ::jewels::memory::ObjectPtr<::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>> out_goodbye_;
    /// out_multi1.
    ::jewels::memory::ObjectPtr<::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>> out_multi1_;
    /// out_multi2.
    ::jewels::memory::ObjectPtr<::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>> out_multi2_;
};
/// Empty SignalApi (no signals).
struct HelloCogDialSignalApi
{
};
/// HelloCogDial
struct HelloCogDial
{
public:
    /// Indicates if the infra fault thresholds header was found and thus if the cog is sending infra faults.
    [[nodiscard]] static constexpr bool has_infra_faults();
    /// Constructor.
    HelloCogDial(::jewels::time::SyncTime start_time, HelloCogDialResources resources, HelloCogDialConfigs configs, HelloCogDialStates states, HelloCogDialConditions conditions, HelloCogDialInputs inputs, HelloCogDialOutputs outputs, ::jewels::memory::ObjectPtr<::clockwork::diagnostics::ClockworkReporter<::clockwork::diagnostics::SignalGroupId::fault_injector_b>> diagnostics, HelloCogDialSignalApi& signals);
    /// Get start_time.
    [[nodiscard]] ::jewels::time::SyncTime& get_start_time();
    /// Get start_time.
    [[nodiscard]] const ::jewels::time::SyncTime& get_start_time() const;
    /// Get resources.
    [[nodiscard]] HelloCogDialResources& get_resources();
    /// Get resources.
    [[nodiscard]] const HelloCogDialResources& get_resources() const;
    /// Get configs.
    [[nodiscard]] HelloCogDialConfigs& get_configs();
    /// Get configs.
    [[nodiscard]] const HelloCogDialConfigs& get_configs() const;
    /// Get states.
    [[nodiscard]] HelloCogDialStates& get_states();
    /// Get states.
    [[nodiscard]] const HelloCogDialStates& get_states() const;
    /// Get conditions.
    [[nodiscard]] HelloCogDialConditions& get_conditions();
    /// Get conditions.
    [[nodiscard]] const HelloCogDialConditions& get_conditions() const;
    /// Get inputs.
    [[nodiscard]] HelloCogDialInputs& get_inputs();
    /// Get inputs.
    [[nodiscard]] const HelloCogDialInputs& get_inputs() const;
    /// Get outputs.
    [[nodiscard]] HelloCogDialOutputs& get_outputs();
    /// Get outputs.
    [[nodiscard]] const HelloCogDialOutputs& get_outputs() const;
    /// Get diagnostics.
    [[nodiscard]] ::clockwork::diagnostics::ClockworkReporter<::clockwork::diagnostics::SignalGroupId::fault_injector_b>& get_diagnostics();
    /// Get diagnostics.
    [[nodiscard]] const ::clockwork::diagnostics::ClockworkReporter<::clockwork::diagnostics::SignalGroupId::fault_injector_b>& get_diagnostics() const;
    /// Get signals.
    [[nodiscard]] HelloCogDialSignalApi& get_signals();
    /// Get signals.
    [[nodiscard]] const HelloCogDialSignalApi& get_signals() const;
private:
    /// start_time.
    ::jewels::time::SyncTime start_time_;
    /// resources.
    HelloCogDialResources resources_;
    /// configs.
    HelloCogDialConfigs configs_;
    /// states.
    HelloCogDialStates states_;
    /// conditions.
    HelloCogDialConditions conditions_;
    /// inputs.
    HelloCogDialInputs inputs_;
    /// outputs.
    HelloCogDialOutputs outputs_;
    /// diagnostics.
    ::jewels::memory::ObjectPtr<::clockwork::diagnostics::ClockworkReporter<::clockwork::diagnostics::SignalGroupId::fault_injector_b>> diagnostics_;
    /// signals.
    HelloCogDialSignalApi& signals_;
};
/// Forward declare ///
void execute_cog(HelloCogDial& /*dial*/);
""".strip()
    )
    expected_inline_chunk_body = """
constexpr auto HelloCogDial::has_infra_faults() -> bool
{
    return false;
}
"""
    assert cpp_mod.inline_chunk.render_str(render_includes=True).strip() == expected_inline_chunk_body.strip()
    assert cpp_mod.header_chunk.produce
    assert cpp_mod.inline_chunk.produce
    assert cpp_mod.implementation_chunk.produce


def test_output_multi_message_per_cycle_is_expressible_in_schema() -> None:
    source = """
use clockwork::dsl::tests::support::hellomsg;

// Multi-message output test cog
cog MultiOutputCog
{
    outputs
    {
        out_batch: Tappy<hellomsg::HelloMsg>
        {
            max_msgs_per_exec: 4;
        }
    }

    execution
    {
        execute when: init;
    }
}

cpp_target multioutputcog
{
  options
  {
    namespace clockwork::testing::cogs;
    generate_cog_metrics false;
  }
  cog MultiOutputCog;
}
"""

    module = compiler.compile_source_text(
        source,
        ModuleID(CLK_REPO, "multi_output_schema_test"),
        importer=importer.FilesystemImporter(compile_fn=compiler.compile_source_file),
    )
    cog_ir = module.inner_scope.lookup("MultiOutputCog")
    assert cog_ir is not None
    assert isinstance(cog_ir, cog.Cog)
    cog_ir.resolve()

    dial = cppdial.Dial(
        cog_ir=cog_ir,
        class_name="MultiOutputCogDial",
        cpp_namespace="clockwork::testing::cogs",
        dial_header=Header(CLK_REPO, Path("clockwork/dsl/tests/support/multi_output_cog_dial.hh")),
    )
    cpp_mod = dial.render()

    assert (
        cpp_mod.header_chunk.render_str(render_includes=True).strip()
        == """
#include "clockwork/dial/include_common.hh"
#include "clockwork/dsl/tests/support/hello_msg_onboard.hh"
namespace clockwork { template <class> struct Tachyon; } // IWYU pragma: keep
/// MultiOutputCogDialResources
struct MultiOutputCogDialResources
{
public:
    /// Constructor.
    MultiOutputCogDialResources();
private:
};
/// MultiOutputCogDialConfigs
struct MultiOutputCogDialConfigs
{
public:
    /// Constructor.
    MultiOutputCogDialConfigs();
private:
};
/// MultiOutputCogDialStates
struct MultiOutputCogDialStates
{
public:
    /// Constructor.
    MultiOutputCogDialStates();
private:
};
/// MultiOutputCogDialConditions
struct MultiOutputCogDialConditions
{
public:
    /// Constructor.
    MultiOutputCogDialConditions();
private:
};
/// MultiOutputCogDialInputs
struct MultiOutputCogDialInputs
{
public:
    /// Constructor.
    MultiOutputCogDialInputs();
private:
};
/// MultiOutputCogDialOutputs
struct MultiOutputCogDialOutputs
{
public:
    /// Constructor.
    explicit MultiOutputCogDialOutputs(::jewels::memory::ObjectPtr<::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 4>> out_batch);
    /// Get out_batch.
    [[nodiscard]] ::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 4>& get_out_batch();
    /// Get out_batch.
    [[nodiscard]] const ::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 4>& get_out_batch() const;
private:
    /// out_batch.
    ::jewels::memory::ObjectPtr<::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 4>> out_batch_;
};
/// MultiOutputCogDialDiagnostics
struct MultiOutputCogDialDiagnostics
{
public:
    /// Constructor.
    MultiOutputCogDialDiagnostics();
private:
};
/// Empty SignalApi (no signals).
struct MultiOutputCogDialSignalApi
{
};
/// MultiOutputCogDial
struct MultiOutputCogDial
{
public:
    /// Indicates if the infra fault thresholds header was found and thus if the cog is sending infra faults.
    [[nodiscard]] static constexpr bool has_infra_faults();
    /// Constructor.
    MultiOutputCogDial(::jewels::time::SyncTime start_time, MultiOutputCogDialResources resources, MultiOutputCogDialConfigs configs, MultiOutputCogDialStates states, MultiOutputCogDialConditions conditions, MultiOutputCogDialInputs inputs, MultiOutputCogDialOutputs outputs, MultiOutputCogDialDiagnostics diagnostics, MultiOutputCogDialSignalApi& signals);
    /// Get start_time.
    [[nodiscard]] ::jewels::time::SyncTime& get_start_time();
    /// Get start_time.
    [[nodiscard]] const ::jewels::time::SyncTime& get_start_time() const;
    /// Get resources.
    [[nodiscard]] MultiOutputCogDialResources& get_resources();
    /// Get resources.
    [[nodiscard]] const MultiOutputCogDialResources& get_resources() const;
    /// Get configs.
    [[nodiscard]] MultiOutputCogDialConfigs& get_configs();
    /// Get configs.
    [[nodiscard]] const MultiOutputCogDialConfigs& get_configs() const;
    /// Get states.
    [[nodiscard]] MultiOutputCogDialStates& get_states();
    /// Get states.
    [[nodiscard]] const MultiOutputCogDialStates& get_states() const;
    /// Get conditions.
    [[nodiscard]] MultiOutputCogDialConditions& get_conditions();
    /// Get conditions.
    [[nodiscard]] const MultiOutputCogDialConditions& get_conditions() const;
    /// Get inputs.
    [[nodiscard]] MultiOutputCogDialInputs& get_inputs();
    /// Get inputs.
    [[nodiscard]] const MultiOutputCogDialInputs& get_inputs() const;
    /// Get outputs.
    [[nodiscard]] MultiOutputCogDialOutputs& get_outputs();
    /// Get outputs.
    [[nodiscard]] const MultiOutputCogDialOutputs& get_outputs() const;
    /// Get diagnostics.
    [[nodiscard]] MultiOutputCogDialDiagnostics& get_diagnostics();
    /// Get diagnostics.
    [[nodiscard]] const MultiOutputCogDialDiagnostics& get_diagnostics() const;
    /// Get signals.
    [[nodiscard]] MultiOutputCogDialSignalApi& get_signals();
    /// Get signals.
    [[nodiscard]] const MultiOutputCogDialSignalApi& get_signals() const;
private:
    /// start_time.
    ::jewels::time::SyncTime start_time_;
    /// resources.
    MultiOutputCogDialResources resources_;
    /// configs.
    MultiOutputCogDialConfigs configs_;
    /// states.
    MultiOutputCogDialStates states_;
    /// conditions.
    MultiOutputCogDialConditions conditions_;
    /// inputs.
    MultiOutputCogDialInputs inputs_;
    /// outputs.
    MultiOutputCogDialOutputs outputs_;
    /// diagnostics.
    MultiOutputCogDialDiagnostics diagnostics_;
    /// signals.
    MultiOutputCogDialSignalApi& signals_;
};
/// Forward declare ///
void execute_cog(MultiOutputCogDial& /*dial*/);
""".strip()
    )


@pytest.mark.parametrize("cog_name", ["HelloCogMinMessages", "HelloCogMinNewMessages"])
def test_hellocog_min_messages_dial_render(cog_name: str) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellocog.clk")),
        importer=importer.FilesystemImporter(compile_fn=compiler.compile_source_file),
    )

    cog_ir = module.inner_scope.lookup(cog_name)
    assert cog_ir is not None
    assert isinstance(cog_ir, cog.Cog)

    dial = cppdial.Dial(
        cog_ir=cog_ir,
        class_name=f"{cog_name}Dial",
        cpp_namespace="clockwork::hellocog",
        dial_header=Header(CLK_REPO, Path("clockwork/dsl/tests/support/hellocog_dial.hh")),
    )
    cpp_mod = dial.render()

    min_messages = 1
    min_new_messages = 1 if cog_name == "HelloCogMinNewMessages" else 0
    execution_condition = "new_hello" if cog_name == "HelloCogMinNewMessages" else "any_hello"

    assert (
        cpp_mod.header_chunk.render_str(render_includes=True).strip()
        == f"""
#include "clockwork/dial/include_common.hh"
#include "clockwork/dsl/tests/support/hello_msg_onboard.hh"
#include <cstdint>
namespace clockwork {{ template <class> struct Tachyon; }} // IWYU pragma: keep
/// {cog_name}DialResources
struct {cog_name}DialResources
{{
public:
    /// Constructor.
    {cog_name}DialResources();
private:
}};
/// {cog_name}DialConfigs
struct {cog_name}DialConfigs
{{
public:
    /// Constructor.
    {cog_name}DialConfigs();
private:
}};
/// {cog_name}DialStates
struct {cog_name}DialStates
{{
public:
    /// Constructor.
    {cog_name}DialStates();
private:
}};
/// {cog_name}DialConditions
struct {cog_name}DialConditions
{{
public:
    /// Constructor.
    explicit {cog_name}DialConditions(::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>> {execution_condition});
    /// Get {execution_condition}.
    [[nodiscard]] const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>& get_{execution_condition}() const;
private:
    /// {execution_condition}.
    ::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>> {execution_condition}_;
}};
/// {cog_name}DialInputs
struct {cog_name}DialInputs
{{
public:
    /// Constructor.
    explicit {cog_name}DialInputs(::jewels::memory::ObjectPtr<const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, {min_messages}U, {min_new_messages}U, false, false, false>> hello);
    /// Get hello.
    [[nodiscard]] const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, {min_messages}U, {min_new_messages}U, false, false, false>& get_hello() const;
private:
    /// hello.
    ::jewels::memory::ObjectPtr<const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, {min_messages}U, {min_new_messages}U, false, false, false>> hello_;
}};
/// {cog_name}DialOutputs
struct {cog_name}DialOutputs
{{
public:
    /// Constructor.
    {cog_name}DialOutputs();
private:
}};
/// {cog_name}DialDiagnostics
struct {cog_name}DialDiagnostics
{{
public:
    /// Constructor.
    {cog_name}DialDiagnostics();
private:
}};
/// Empty SignalApi (no signals).
struct {cog_name}DialSignalApi
{{
}};
/// {cog_name}Dial
struct {cog_name}Dial
{{
public:
    /// Indicates if the infra fault thresholds header was found and thus if the cog is sending infra faults.
    [[nodiscard]] static constexpr bool has_infra_faults();
    /// Constructor.
    {cog_name}Dial(::jewels::time::SyncTime start_time, {cog_name}DialResources resources, {cog_name}DialConfigs configs, {cog_name}DialStates states, {cog_name}DialConditions conditions, {cog_name}DialInputs inputs, {cog_name}DialOutputs outputs, {cog_name}DialDiagnostics diagnostics, {cog_name}DialSignalApi& signals);
    /// Get start_time.
    [[nodiscard]] ::jewels::time::SyncTime& get_start_time();
    /// Get start_time.
    [[nodiscard]] const ::jewels::time::SyncTime& get_start_time() const;
    /// Get resources.
    [[nodiscard]] {cog_name}DialResources& get_resources();
    /// Get resources.
    [[nodiscard]] const {cog_name}DialResources& get_resources() const;
    /// Get configs.
    [[nodiscard]] {cog_name}DialConfigs& get_configs();
    /// Get configs.
    [[nodiscard]] const {cog_name}DialConfigs& get_configs() const;
    /// Get states.
    [[nodiscard]] {cog_name}DialStates& get_states();
    /// Get states.
    [[nodiscard]] const {cog_name}DialStates& get_states() const;
    /// Get conditions.
    [[nodiscard]] {cog_name}DialConditions& get_conditions();
    /// Get conditions.
    [[nodiscard]] const {cog_name}DialConditions& get_conditions() const;
    /// Get inputs.
    [[nodiscard]] {cog_name}DialInputs& get_inputs();
    /// Get inputs.
    [[nodiscard]] const {cog_name}DialInputs& get_inputs() const;
    /// Get outputs.
    [[nodiscard]] {cog_name}DialOutputs& get_outputs();
    /// Get outputs.
    [[nodiscard]] const {cog_name}DialOutputs& get_outputs() const;
    /// Get diagnostics.
    [[nodiscard]] {cog_name}DialDiagnostics& get_diagnostics();
    /// Get diagnostics.
    [[nodiscard]] const {cog_name}DialDiagnostics& get_diagnostics() const;
    /// Get signals.
    [[nodiscard]] {cog_name}DialSignalApi& get_signals();
    /// Get signals.
    [[nodiscard]] const {cog_name}DialSignalApi& get_signals() const;
private:
    /// start_time.
    ::jewels::time::SyncTime start_time_;
    /// resources.
    {cog_name}DialResources resources_;
    /// configs.
    {cog_name}DialConfigs configs_;
    /// states.
    {cog_name}DialStates states_;
    /// conditions.
    {cog_name}DialConditions conditions_;
    /// inputs.
    {cog_name}DialInputs inputs_;
    /// outputs.
    {cog_name}DialOutputs outputs_;
    /// diagnostics.
    {cog_name}DialDiagnostics diagnostics_;
    /// signals.
    {cog_name}DialSignalApi& signals_;
}};
/// Forward declare ///
void execute_cog({cog_name}Dial& /*dial*/);
""".strip()
    )


def test_goodbyecog_dial_render() -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/goodbyecog.clk")),
        importer=importer.FilesystemImporter(compile_fn=compiler.compile_source_file),
    )
    cog_ir = module.inner_scope.lookup("GoodbyeCog")
    assert cog_ir is not None
    assert isinstance(cog_ir, cog.Cog)
    dial = cppdial.Dial(
        cog_ir=cog_ir,
        class_name="GoodbyeCogDial",
        cpp_namespace="clockwork::hellocog",
        dial_header=Header(CLK_REPO, Path("clockwork/dsl/tests/support/goodbyecog_dial.hh")),
    )
    cpp_mod = dial.render()

    assert (
        cpp_mod.header_chunk.render_str(render_includes=True).strip()
        == """
#include "clockwork/dial/include_common.hh"
/// GoodbyeCogDialResources
struct GoodbyeCogDialResources
{
public:
    /// Constructor.
    GoodbyeCogDialResources();
private:
};
/// GoodbyeCogDialConfigs
struct GoodbyeCogDialConfigs
{
public:
    /// Constructor.
    GoodbyeCogDialConfigs();
private:
};
/// GoodbyeCogDialStates
struct GoodbyeCogDialStates
{
public:
    /// Constructor.
    GoodbyeCogDialStates();
private:
};
/// GoodbyeCogDialConditions
struct GoodbyeCogDialConditions
{
public:
    /// Constructor.
    GoodbyeCogDialConditions();
private:
};
/// GoodbyeCogDialInputs
struct GoodbyeCogDialInputs
{
public:
    /// Constructor.
    GoodbyeCogDialInputs();
private:
};
/// GoodbyeCogDialOutputs
struct GoodbyeCogDialOutputs
{
public:
    /// Constructor.
    GoodbyeCogDialOutputs();
private:
};
/// GoodbyeCogDialDiagnostics
struct GoodbyeCogDialDiagnostics
{
public:
    /// Constructor.
    GoodbyeCogDialDiagnostics();
private:
};
/// Empty SignalApi (no signals).
struct GoodbyeCogDialSignalApi
{
};
/// GoodbyeCogDial
struct GoodbyeCogDial
{
public:
    /// Indicates if the infra fault thresholds header was found and thus if the cog is sending infra faults.
    [[nodiscard]] static constexpr bool has_infra_faults();
    /// Constructor.
    GoodbyeCogDial(::jewels::time::SyncTime start_time, GoodbyeCogDialResources resources, GoodbyeCogDialConfigs configs, GoodbyeCogDialStates states, GoodbyeCogDialConditions conditions, GoodbyeCogDialInputs inputs, GoodbyeCogDialOutputs outputs, GoodbyeCogDialDiagnostics diagnostics, GoodbyeCogDialSignalApi& signals);
    /// Get start_time.
    [[nodiscard]] ::jewels::time::SyncTime& get_start_time();
    /// Get start_time.
    [[nodiscard]] const ::jewels::time::SyncTime& get_start_time() const;
    /// Get resources.
    [[nodiscard]] GoodbyeCogDialResources& get_resources();
    /// Get resources.
    [[nodiscard]] const GoodbyeCogDialResources& get_resources() const;
    /// Get configs.
    [[nodiscard]] GoodbyeCogDialConfigs& get_configs();
    /// Get configs.
    [[nodiscard]] const GoodbyeCogDialConfigs& get_configs() const;
    /// Get states.
    [[nodiscard]] GoodbyeCogDialStates& get_states();
    /// Get states.
    [[nodiscard]] const GoodbyeCogDialStates& get_states() const;
    /// Get conditions.
    [[nodiscard]] GoodbyeCogDialConditions& get_conditions();
    /// Get conditions.
    [[nodiscard]] const GoodbyeCogDialConditions& get_conditions() const;
    /// Get inputs.
    [[nodiscard]] GoodbyeCogDialInputs& get_inputs();
    /// Get inputs.
    [[nodiscard]] const GoodbyeCogDialInputs& get_inputs() const;
    /// Get outputs.
    [[nodiscard]] GoodbyeCogDialOutputs& get_outputs();
    /// Get outputs.
    [[nodiscard]] const GoodbyeCogDialOutputs& get_outputs() const;
    /// Get diagnostics.
    [[nodiscard]] GoodbyeCogDialDiagnostics& get_diagnostics();
    /// Get diagnostics.
    [[nodiscard]] const GoodbyeCogDialDiagnostics& get_diagnostics() const;
    /// Get signals.
    [[nodiscard]] GoodbyeCogDialSignalApi& get_signals();
    /// Get signals.
    [[nodiscard]] const GoodbyeCogDialSignalApi& get_signals() const;
private:
    /// start_time.
    ::jewels::time::SyncTime start_time_;
    /// resources.
    GoodbyeCogDialResources resources_;
    /// configs.
    GoodbyeCogDialConfigs configs_;
    /// states.
    GoodbyeCogDialStates states_;
    /// conditions.
    GoodbyeCogDialConditions conditions_;
    /// inputs.
    GoodbyeCogDialInputs inputs_;
    /// outputs.
    GoodbyeCogDialOutputs outputs_;
    /// diagnostics.
    GoodbyeCogDialDiagnostics diagnostics_;
    /// signals.
    GoodbyeCogDialSignalApi& signals_;
};
/// Forward declare ///
void execute_cog(GoodbyeCogDial& /*dial*/);
""".strip()
    )
    expected_inline_chunk_body = """
constexpr auto GoodbyeCogDial::has_infra_faults() -> bool
{
    return false;
}
"""
    assert cpp_mod.inline_chunk.render_str(render_includes=True).strip() == expected_inline_chunk_body.strip()
    assert cpp_mod.header_chunk.produce
    assert cpp_mod.inline_chunk.produce
    assert cpp_mod.implementation_chunk.produce


@pytest.fixture(scope="module")
def fs_importer() -> importer.FilesystemImporter:
    """Create a filesystem importer for compiling .clk files."""
    return importer.FilesystemImporter(compile_fn=compiler.compile_source_file)


@pytest.fixture(scope="module")
def param_test_cog_ir(fs_importer: importer.FilesystemImporter) -> cog.Cog:
    """clk_parameterized_box.clk and return the ParamtestCog IR."""
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/clk_parameterized_box.clk")),
        importer=fs_importer,
    )
    cog_ir = module.inner_scope.lookup("ParamTestCog")
    assert isinstance(cog_ir, cog.Cog)
    return cog_ir


def test_param_dial_header(param_test_cog_ir: cog.Cog) -> None:
    """Golden file test: dial header with parameterized cog."""
    dial_header = Header(CLK_REPO, "clockwork/dsl/tests/support/clk_parameterized_box_clk_cc_dial.hh")
    dial = cppdial.Dial(
        cog_ir=param_test_cog_ir,
        class_name="ParamTestCog",
        cpp_namespace="clockwork::testing",
        dial_header=dial_header,
    )
    cpp_mod = dial.render()
    actual = cpp_mod.header_chunk.render_str(render_includes=True).strip()

    test_helpers.write_expected("expected_param_dial_header", actual)
    assert actual == test_helpers.load_expected("expected_param_dial_header")


def test_param_dial_source(param_test_cog_ir: cog.Cog) -> None:
    """Golden file test: dial source with parameterized cog."""
    dial_header = Header(CLK_REPO, "clockwork/dsl/tests/support/clk_parameterized_box_clk_cc_dial.hh")
    dial = cppdial.Dial(
        cog_ir=param_test_cog_ir,
        class_name="ParamTestCog",
        cpp_namespace="clockwork::testing",
        dial_header=dial_header,
    )
    cpp_mod = dial.render()
    actual = cpp_mod.implementation_chunk.render_str(render_includes=True).strip()

    test_helpers.write_expected("expected_param_dial_source", actual)
    assert actual == test_helpers.load_expected("expected_param_dial_source")


def test_param_dial_inline(param_test_cog_ir: cog.Cog) -> None:
    """Golden file test: dial inlinw with parameterized cog."""
    dial_header = Header(CLK_REPO, "clockwork/dsl/tests/support/clk_parameterized_box_clk_cc_dial.hh")
    dial = cppdial.Dial(
        cog_ir=param_test_cog_ir,
        class_name="ParamTestCog",
        cpp_namespace="clockwork::testing",
        dial_header=dial_header,
    )
    cpp_mod = dial.render()
    actual = cpp_mod.inline_chunk.render_str(render_includes=True).strip()

    test_helpers.write_expected("expected_param_dial_inline", actual)
    assert actual == test_helpers.load_expected("expected_param_dial_inline")


@pytest.fixture(scope="module")
def param_test_instantiated_cog_ir(fs_importer: importer.FilesystemImporter) -> cog.InstantiatedCog:
    """clk_parameterized_box.clk and return the ParamtestCog IR."""
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/clk_parameterized_box.clk")),
        importer=fs_importer,
    )
    instantiate_stmt = module.inner_scope.lookup("ParamTestCog1")
    assert isinstance(instantiate_stmt, statement.InstantiateStmt)
    assert isinstance(instantiate_stmt.instantiated, cog.InstantiatedCog)
    return instantiate_stmt.instantiated


def test_param_inst_dial_header(param_test_instantiated_cog_ir: cog.InstantiatedCog) -> None:
    """Golden file test: dial source with parameterized cog."""
    dial_header = Header(CLK_REPO, "clockwork/dsl/tests/support/clk_parameterized_box_clk_cc_dial.hh")
    instantiated_dial = cppdial.InstantiatedDial(
        instantiation=param_test_instantiated_cog_ir,
        class_name="ParamTestCog",
        cpp_namespace="clockwork::testing",
        dial_header=dial_header,
    )
    cpp_mod = instantiated_dial.render()
    actual = cpp_mod.header_chunk.render_str(render_includes=True).strip()

    assert actual == ""


def test_param_inst_dial_source(param_test_instantiated_cog_ir: cog.InstantiatedCog) -> None:
    """Golden file test: dial source with parameterized cog."""
    dial_header = Header(CLK_REPO, "clockwork/dsl/tests/support/clk_parameterized_box_clk_cc_dial.hh")
    instantiated_dial = cppdial.InstantiatedDial(
        instantiation=param_test_instantiated_cog_ir,
        class_name="ParamTestCog",
        cpp_namespace="clockwork::testing",
        dial_header=dial_header,
    )
    cpp_mod = instantiated_dial.render()
    actual = cpp_mod.implementation_chunk.render_str(render_includes=True).strip()

    test_helpers.write_expected("expected_param_inst_dial_source", actual)
    assert actual == test_helpers.load_expected("expected_param_inst_dial_source")


def test_param_inst_dial_inline(param_test_instantiated_cog_ir: cog.InstantiatedCog) -> None:
    """Golden file test: dial inline with parameterized cog."""
    dial_header = Header(CLK_REPO, "clockwork/dsl/tests/support/clk_parameterized_box_clk_cc_dial.hh")
    instantiated_dial = cppdial.InstantiatedDial(
        instantiation=param_test_instantiated_cog_ir,
        class_name="ParamTestCog",
        cpp_namespace="clockwork::testing",
        dial_header=dial_header,
    )
    cpp_mod = instantiated_dial.render()
    actual = cpp_mod.inline_chunk.render_str(render_includes=True).strip()

    assert actual == ""
