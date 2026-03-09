# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for cppdial."""

from pathlib import Path

import pytest
from clockwork.dsl.cog import cppdial
from clockwork.dsl.cpp.context import Header
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
    HelloCogDialConditions(::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>> any_msg, ::jewels::memory::ObjectPtr<const ::clockwork::MessagePresentCondition<1U, 2U>> new_msg, ::jewels::memory::ObjectPtr<const ::clockwork::TimeSinceLastExecCondition<500'000'000U>> periodic);
    /// Get any_msg.
    [[nodiscard]] const ::clockwork::MessagePresentCondition<1U, 4'294'967'295U>& get_any_msg() const;
    /// Get new_msg.
    [[nodiscard]] const ::clockwork::MessagePresentCondition<1U, 2U>& get_new_msg() const;
    /// Get periodic.
    [[nodiscard]] const ::clockwork::TimeSinceLastExecCondition<500'000'000U>& get_periodic() const;
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
    HelloCogDialInputs(::jewels::memory::ObjectPtr<const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, 0U, 0U>> latest_hello, ::jewels::memory::ObjectPtr<::clockwork::MessageInputDialWithCursorControl<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 5U, 0U, 0U>> history_of_hellos);
    /// Get latest_hello.
    [[nodiscard]] const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, 0U, 0U>& get_latest_hello() const;
    /// Get history_of_hellos.
    [[nodiscard]] ::clockwork::MessageInputDialWithCursorControl<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 5U, 0U, 0U>& get_history_of_hellos();
private:
    /// latest_hello.
    ::jewels::memory::ObjectPtr<const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, 0U, 0U>> latest_hello_;
    /// history_of_hellos.
    ::jewels::memory::ObjectPtr<::clockwork::MessageInputDialWithCursorControl<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 5U, 0U, 0U>> history_of_hellos_;
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
    /// Get signals.
    [[nodiscard]] HelloCogDialSignalApi& get_signals();
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
    explicit {cog_name}DialInputs(::jewels::memory::ObjectPtr<const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, {min_messages}U, {min_new_messages}U>> hello);
    /// Get hello.
    [[nodiscard]] const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, {min_messages}U, {min_new_messages}U>& get_hello() const;
private:
    /// hello.
    ::jewels::memory::ObjectPtr<const ::clockwork::MessageInputDial<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, 1U, {min_messages}U, {min_new_messages}U>> hello_;
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
    /// Get resources.
    [[nodiscard]] {cog_name}DialResources& get_resources();
    /// Get configs.
    [[nodiscard]] {cog_name}DialConfigs& get_configs();
    /// Get states.
    [[nodiscard]] {cog_name}DialStates& get_states();
    /// Get conditions.
    [[nodiscard]] {cog_name}DialConditions& get_conditions();
    /// Get inputs.
    [[nodiscard]] {cog_name}DialInputs& get_inputs();
    /// Get outputs.
    [[nodiscard]] {cog_name}DialOutputs& get_outputs();
    /// Get diagnostics.
    [[nodiscard]] {cog_name}DialDiagnostics& get_diagnostics();
    /// Get signals.
    [[nodiscard]] {cog_name}DialSignalApi& get_signals();
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
    /// Get signals.
    [[nodiscard]] GoodbyeCogDialSignalApi& get_signals();
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
