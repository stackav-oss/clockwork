# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for cppdial."""

from pathlib import Path

from clockwork.dsl.cog import cppdial
from clockwork.dsl.ir import cog, compiler, importer
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


def test_hellocog_dial_render() -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellocog.clk")),
        importer=importer.FilesystemImporter(compile_fn=compiler.compile_source_file),
    )
    cog_ir = module.inner_scope.lookup("HelloCog")
    assert cog_ir is not None
    assert isinstance(cog_ir, cog.Cog)
    dial = cppdial.Dial(cog_ir=cog_ir, class_name="HelloCogDial", cpp_namespace="clockwork::hellocog")
    cpp_mod = dial.render()

    assert (
        cpp_mod.header_chunk.render_str(render_includes=True).strip()
        == """
#include "clockwork/diagnostics/report_definitions.hh"
#include "clockwork/diagnostics/reporter.hh"
#include "clockwork/dial/include_common.hh"
#include "clockwork/dsl/tests/support/cxx_state.hh" // IWYU pragma: export
#include "clockwork/dsl/tests/support/hello_msg_onboard.hh"
#include <cstdint>
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
    [[nodiscard]] const ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>& get_cfg_hello();
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
    [[nodiscard]] const ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>& get_ro_hello();
    /// Get rw_hello.
    [[nodiscard]] ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>& get_rw_hello();
    /// Get extern_hello.
    [[nodiscard]] const ::clockwork::testing::CxxState& get_extern_hello();
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
    HelloCogDialConditions(::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>> any_msg, ::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 2U>> new_msg, ::jewels::memory::ObjectPtr<const ::clockwork::TimeSinceLastExecCondition<500'000'000U>> periodic);
    /// Get any_msg.
    [[nodiscard]] const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>& get_any_msg();
    /// Get new_msg.
    [[nodiscard]] const ::clockwork::MessagePresentCondition<1U, 2U>& get_new_msg();
    /// Get periodic.
    [[nodiscard]] const ::clockwork::TimeSinceLastExecCondition<500'000'000U>& get_periodic();
private:
    /// any_msg.
    ::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>> any_msg_;
    /// new_msg.
    ::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 2U>> new_msg_;
    /// periodic.
    ::jewels::memory::ObjectPtr<const ::clockwork::TimeSinceLastExecCondition<500'000'000U>> periodic_;
};
/// HelloCogDialInputs
struct HelloCogDialInputs
{
public:
    /// Constructor.
    HelloCogDialInputs(::jewels::memory::ObjectPtr<const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U>> latest_hello, ::jewels::memory::ObjectPtr<::clockwork::MessageInputDialWithCursorControl<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 5U>> history_of_hellos);
    /// Get latest_hello.
    [[nodiscard]] const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U>& get_latest_hello();
    /// Get history_of_hellos.
    [[nodiscard]] ::clockwork::MessageInputDialWithCursorControl<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 5U>& get_history_of_hellos();
private:
    /// latest_hello.
    ::jewels::memory::ObjectPtr<const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U>> latest_hello_;
    /// history_of_hellos.
    ::jewels::memory::ObjectPtr<::clockwork::MessageInputDialWithCursorControl<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 5U>> history_of_hellos_;
};
/// HelloCogDialOutputs
struct HelloCogDialOutputs
{
public:
    /// Constructor.
    HelloCogDialOutputs(::jewels::memory::ObjectPtr<::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>> out_world, ::jewels::memory::ObjectPtr<::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>> out_goodbye, ::jewels::memory::ObjectPtr<::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>> out_multi1, ::jewels::memory::ObjectPtr<::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>> out_multi2);
    /// Get out_world.
    [[nodiscard]] ::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>& get_out_world();
    /// Get out_goodbye.
    [[nodiscard]] ::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>& get_out_goodbye();
    /// Get out_multi1.
    [[nodiscard]] ::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>& get_out_multi1();
    /// Get out_multi2.
    [[nodiscard]] ::clockwork::pinion::Publishable<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>& get_out_multi2();
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
/// HelloCogDial
struct HelloCogDial
{
public:
    /// Constructor.
    HelloCogDial(::jewels::time::SyncTime start_time, HelloCogDialResources resources, HelloCogDialConfigs configs, HelloCogDialStates states, HelloCogDialConditions conditions, HelloCogDialInputs inputs, HelloCogDialOutputs outputs, ::jewels::memory::ObjectPtr<::clockwork::diagnostics::ClockworkReporter<::clockwork::diagnostics::SignalGroupId::fault_injector_b>> diagnostics);
    /// Get start_time.
    [[nodiscard]] ::jewels::time::SyncTime& get_start_time();
    /// Get resources.
    [[nodiscard]] HelloCogDialResources& get_resources();
    /// Get configs.
    [[nodiscard]] HelloCogDialConfigs& get_configs();
    /// Get states.
    [[nodiscard]] HelloCogDialStates& get_states();
    /// Get conditions.
    [[nodiscard]] HelloCogDialConditions& get_conditions();
    /// Get inputs.
    [[nodiscard]] HelloCogDialInputs& get_inputs();
    /// Get outputs.
    [[nodiscard]] HelloCogDialOutputs& get_outputs();
    /// Get diagnostics.
    [[nodiscard]] ::clockwork::diagnostics::ClockworkReporter<::clockwork::diagnostics::SignalGroupId::fault_injector_b>& get_diagnostics();
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
};
/// Forward declare ///
void execute_cog(HelloCogDial& /*dial*/);
""".strip()
    )
    assert cpp_mod.header_chunk.produce
    assert not cpp_mod.inline_chunk.produce
    assert cpp_mod.implementation_chunk.produce


def test_goodbyecog_dial_render() -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/goodbyecog.clk")),
        importer=importer.FilesystemImporter(compile_fn=compiler.compile_source_file),
    )
    cog_ir = module.inner_scope.lookup("GoodbyeCog")
    assert cog_ir is not None
    assert isinstance(cog_ir, cog.Cog)
    dial = cppdial.Dial(cog_ir=cog_ir, class_name="GoodbyeCogDial", cpp_namespace="clockwork::hellocog")
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
/// GoodbyeCogDial
struct GoodbyeCogDial
{
public:
    /// Constructor.
    GoodbyeCogDial(::jewels::time::SyncTime start_time, GoodbyeCogDialResources resources, GoodbyeCogDialConfigs configs, GoodbyeCogDialStates states, GoodbyeCogDialConditions conditions, GoodbyeCogDialInputs inputs, GoodbyeCogDialOutputs outputs, GoodbyeCogDialDiagnostics diagnostics);
    /// Get start_time.
    [[nodiscard]] ::jewels::time::SyncTime& get_start_time();
    /// Get resources.
    [[nodiscard]] GoodbyeCogDialResources& get_resources();
    /// Get configs.
    [[nodiscard]] GoodbyeCogDialConfigs& get_configs();
    /// Get states.
    [[nodiscard]] GoodbyeCogDialStates& get_states();
    /// Get conditions.
    [[nodiscard]] GoodbyeCogDialConditions& get_conditions();
    /// Get inputs.
    [[nodiscard]] GoodbyeCogDialInputs& get_inputs();
    /// Get outputs.
    [[nodiscard]] GoodbyeCogDialOutputs& get_outputs();
    /// Get diagnostics.
    [[nodiscard]] GoodbyeCogDialDiagnostics& get_diagnostics();
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
};
/// Forward declare ///
void execute_cog(GoodbyeCogDial& /*dial*/);
""".strip()
    )
    assert cpp_mod.header_chunk.produce
    assert not cpp_mod.inline_chunk.produce
    assert cpp_mod.implementation_chunk.produce
