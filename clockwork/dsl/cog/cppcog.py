# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Facilities for generating C++ Cog structs."""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal
from typing import TYPE_CHECKING, Final

from clockwork.dsl.cog.codegen_helpers import (
    COG_CLASS_UUID_TYPE,
    ENDPOINT_UUID_TYPE,
    UUID_TEMPLATE,
    UuidHandler,
    formatted_struct,
    gen_const_str,
    get_cog_instantiation_args,
    get_rendered_cog_instantiation_args,
    get_template_args,
    get_template_params,
    to_camel,
)
from clockwork.dsl.cog.cppdial import DynamicTimerCodegenHandler
from clockwork.dsl.cog.cppdial_signals import (
    BatchedReportGroupInfo,
    PostAggregatedReportGroupInfo,
    collect_batched_report_groups,
    collect_post_aggregated_report_groups,
)
from clockwork.dsl.cog.pycog import (
    AnyMessageCondition,
    ConditionBase,
    ConditionsStruct,
    Config,
    ConfigsStruct,
    Diagnostics,
    DiagnosticsStruct,
    DynamicTimerCondition,
    Input,
    InputsStruct,
    MessagesPresentCondition,
    NewMessageCondition,
    Output,
    OutputsStruct,
    Resource,
    ResourcesStruct,
    SignalsStruct,
    State,
    StatesStruct,
    TimeSinceLastExecCondition,
)
from clockwork.dsl.cpp import typereg
from clockwork.dsl.cpp.context import CppChunk, CppModuleChunks, FwdDecl, Header, SystemHeader
from clockwork.dsl.cpp.typereg import CLOCKWORK_NAMESPACE, get_cpp_type
from clockwork.dsl.cpp.types import (
    VOID,
    CppMethod,
    CppNamedType,
    CppTemplate,
    CppTemplateType,
    CppType,
    Ref,
    const_qualify,
)
from clockwork.dsl.ir import aligner as aligner_ir
from clockwork.dsl.ir import cog, primitive, schema_reg, typesys, units, uuid_reg
from clockwork.dsl.ir.clkbuiltins import REPRESENTATION_TAG_TYPE, TAPPY
from clockwork.dsl.ir.cog_components import DynamicTimer
from clockwork.dsl.ir.cog_metrics_report_groups import (
    EVENT_METRICS_GROUP_NAME,
    TELEMETRY_METRICS_GROUP_NAME,
    TELEMETRY_SIGNAL_PREFIX,
)
from clockwork.dsl.ir.cog_metrics_schema_generation import get_underlying_enum_type
from clockwork.dsl.ir.diagnostics import (
    COG_INFRA_DIAGS_GROUP_DEF_NAME,
    DiagnosticsSignalDef,
    InfraDiagnosticsDef,
    infra_defs_header_from_dial_header,
)
from clockwork.dsl.ir.diagnostics import NAMESPACE as DIAGNOSTICS_NAMESPACE
from clockwork.dsl.ir.extern_type import ExternType
from clockwork.dsl.ir.module_id import CLK_REPO, JEWELS_REPO
from pydantic.alias_generators import to_snake

if TYPE_CHECKING:
    from collections.abc import Iterable, Iterator

    from clockwork.dsl.compiler_context import CompilerContext
    from clockwork.dsl.ir.cog_parameters import CogParameterRef


_UUID_TEMPLATE = CppTemplate([Header(JEWELS_REPO, "jewels/uuid/uuid/hh")], "Uuid", "jewels")

_OUT_TEMPLATE = CppTemplate(
    [Header(JEWELS_REPO, "jewels/callsig/outparam.hh")],
    "Out",
    "jewels",
)
_STALE_ALIGNMENT_INDEX_OUT: Final = "stale_alignment_index_out"


_COG_CLASS_TYPE = CppType(
    [Header(CLK_REPO, "clockwork/common/process_description_clk_cc.hh")],
    "CogClassId",
    CLOCKWORK_NAMESPACE + "::common",
)
_ENDPOINT_TYPE = CppType(
    [Header(CLK_REPO, "clockwork/common/process_description_clk_cc.hh")],
    "EndpointClassId",
    CLOCKWORK_NAMESPACE + "::common",
)
_COG_CLASS_UUID_TYPE = _UUID_TEMPLATE.instantiate([_COG_CLASS_TYPE])
_ENDPOINT_UUID_TYPE = _UUID_TEMPLATE.instantiate([_ENDPOINT_TYPE])


def _is_cog_metrics_group(name: str) -> bool:
    """Return True if the report group name is an infrastructure cog metrics group."""
    return name in (EVENT_METRICS_GROUP_NAME, TELEMETRY_METRICS_GROUP_NAME)


def _get_representation_uuid_type(context: CompilerContext) -> CppTemplateType:
    """Get the UUID type for representations."""
    return UUID_TEMPLATE.instantiate([get_cpp_type(context, REPRESENTATION_TAG_TYPE)])


def _cond_type_present(
    root: cog.ConditionExpr,
    looking_for: type[
        cog.LogConditionExpr | cog.InitConditionExpr | cog.TimeSinceLastExec | cog.MessagesPresent | DynamicTimer
    ],
) -> bool:
    """Recursively traverse conditon expr tree to search for instance of a type."""
    if isinstance(root, cog.InitConditionExpr):
        return looking_for is cog.InitConditionExpr
    if isinstance(root, cog.LogConditionExpr):
        return looking_for is cog.LogConditionExpr
    if isinstance(root, cog.SimpleConditionExpr):
        if not isinstance(root.condition, cog.ConditionDef):
            msg = f"{root} may not have been resolved."
            raise TypeError(msg)
        return isinstance(root.condition.condition, looking_for)
    if isinstance(root, cog.BinaryConditionExpr):
        return _cond_type_present(root.lhs, looking_for) or _cond_type_present(root.rhs, looking_for)
    msg = f"{root} is of type {type(root)}, which is not supported."
    raise TypeError(msg)


def _execute_expr_render_helper(
    root: cog.ConditionExpr,
    timer_handle_map: dict[str, TimerHandler],
    arg_timers: str,
    input_condition_handler_map: dict[str, InputConditionHandler],
    arg_input_conditions: str,
) -> str:
    """Recursively traverse the execute_expr tree and generate code."""
    if isinstance(root, cog.InitConditionExpr):
        return "(statistics.num_executions_ == 0)"
    if isinstance(root, cog.LogConditionExpr):
        return "true"
    if isinstance(root, cog.SimpleConditionExpr):
        if not isinstance(root.condition, cog.ConditionDef):
            msg = f"{root} may not have been resolved."
            raise TypeError(msg)

        # To generate code, search both timer and input condition maps to retrieve appropriate handler and render
        def static_cast(s: str) -> str:
            return f"static_cast<bool>({s})"

        name = root.condition.name
        if name in timer_handle_map:
            return static_cast(timer_handle_map[name].render_get(arg_timers))
        if name in input_condition_handler_map:
            return static_cast(input_condition_handler_map[name].render_get(arg_input_conditions))
        msg = f"{name} is not a named condition."
        raise RuntimeError(msg)
    if isinstance(root, cog.BinaryConditionExpr):
        op_str = {
            cog.ConditionOp.AND: "&&",
            cog.ConditionOp.OR: "||",
        }
        lhs_eval = _execute_expr_render_helper(
            root.lhs, timer_handle_map, arg_timers, input_condition_handler_map, arg_input_conditions
        )
        rhs_eval = _execute_expr_render_helper(
            root.rhs, timer_handle_map, arg_timers, input_condition_handler_map, arg_input_conditions
        )
        if root.op not in op_str:
            msg = f"{root.op} is not supported."
            raise RuntimeError(msg)
        op_eval = op_str[root.op]
        return f"({lhs_eval} {op_eval} {rhs_eval})"
    msg = f"{root} is of type {type(root)}, which is not supported."
    raise TypeError(msg)


def _gen_const_size(value: int, name: str) -> str:
    return f"static constexpr size_t {name} = {value};"


def _gen_const_milliseconds(value: primitive.UnitValue, name: str) -> str:
    return f"static constexpr auto {name} = ::std::chrono::milliseconds({value.value});"


def _gen_factory(  # noqa: PLR0912, PLR0913 (Factory generation needs these branches and parameters)
    uuid: UuidHandler,
    class_name: str,
    base_name: str,
    make_params: str,
    make_body: str,
    template_args: str = "",
    is_specialization: bool = False,
    snapshot_make: tuple[str, list[str]] | None = None,
) -> CppModuleChunks:
    fq_base_name = f"::{CLOCKWORK_NAMESPACE}::{base_name}"
    cpp_mod = CppModuleChunks()
    if is_specialization:
        cpp_mod.header_chunk.append("template<>")
    if template_args:
        cpp_mod.header_chunk.append(f"struct {class_name}Factory<{template_args}> : {fq_base_name}\n{{")
    else:
        cpp_mod.header_chunk.append(f"struct {class_name}Factory : {fq_base_name}\n{{")
    factory_header = [
        f"static constexpr auto type_id = {uuid.render_from_string_func()};",
        f"[[nodiscard]] const {fq_base_name}::IdType &id() const override;",
        f"[[nodiscard]] {fq_base_name}::Ptr make({make_params}) const override;",
    ]
    if snapshot_make:
        snapshot_make_params, _ = snapshot_make
        factory_header.append(
            f"[[nodiscard]] {fq_base_name}::StateRestoreOutcome make({snapshot_make_params}) const override;"
        )
    factory_header.append(f"static {class_name}Factory instance;")
    cpp_mod.header_chunk.append(factory_header, indent=1)
    cpp_mod.header_chunk.append("};")

    if template_args:
        cpp_mod.implementation_chunk.append(
            f"const {fq_base_name}::IdType &{class_name}Factory<{template_args}>::id() const",
        )
    else:
        cpp_mod.implementation_chunk.append(
            f"const {fq_base_name}::IdType &{class_name}Factory::id() const",
        )
    cpp_mod.implementation_chunk.append("{")
    cpp_mod.implementation_chunk.append("return type_id;", indent=1)
    cpp_mod.implementation_chunk.append("}")
    if template_args:
        cpp_mod.implementation_chunk.append(
            f"{fq_base_name}::Ptr {class_name}Factory<{template_args}>::make({make_params}) const",
        )
    else:
        cpp_mod.implementation_chunk.append(f"{fq_base_name}::Ptr {class_name}Factory::make({make_params}) const")
    cpp_mod.implementation_chunk.append("{")
    cpp_mod.implementation_chunk.append(
        f"{make_body}",
        indent=1,
    )
    cpp_mod.implementation_chunk.append("}")
    if snapshot_make:
        snapshot_make_params, snapshot_make_body = snapshot_make
        if template_args:
            cpp_mod.implementation_chunk.append(
                f"{fq_base_name}::StateRestoreOutcome {class_name}Factory<{template_args}>::make({snapshot_make_params}) const"
            )
        else:
            cpp_mod.implementation_chunk.append(
                f"{fq_base_name}::StateRestoreOutcome {class_name}Factory::make({snapshot_make_params}) const"
            )
        cpp_mod.implementation_chunk.append("{")
        cpp_mod.implementation_chunk.append(snapshot_make_body, indent=1)
        cpp_mod.implementation_chunk.append("}")
    if template_args:
        cpp_mod.implementation_chunk.append(
            f"{class_name}Factory<{template_args}> {class_name}Factory<{template_args}>::instance;"
        )
    else:
        cpp_mod.implementation_chunk.append(f"{class_name}Factory {class_name}Factory::instance;")
    return cpp_mod


def _serializable_state_factory(
    state_type: str, serialized_type: str, snapshot_uuid: UuidHandler
) -> tuple[str, list[str]]:
    """Generate the restoration overload for a serializable external state."""
    restore_result = f"::{CLOCKWORK_NAMESPACE}::CogStateFactory::StateRestoreResult"
    make_params = (
        f"::jewels::Out<::{CLOCKWORK_NAMESPACE}::CogStateFactory::Ptr> state_out, "
        "::jewels::memory::MemoryResource memres_sys, "
        "::jewels::memory::MemoryResource memres_state, "
        f"::jewels::Uuid<::{CLOCKWORK_NAMESPACE}::RepresentationTag> snapshot_representation_id, "
        "::std::span<const ::std::byte> snapshot_data"
    )
    make_body = [
        f"if (snapshot_representation_id != {snapshot_uuid.render_from_string_func()})",
        "{",
        f"    return {restore_result}::invalid_class_uuid;",
        "}",
        f"if (snapshot_data.size() != sizeof({serialized_type}))",
        "{",
        f"    return {restore_result}::buffer_error;",
        "}",
        f"::jewels::memory::AlignedStorage<{serialized_type}> snapshot_storage{{}};",
        "::std::memcpy(snapshot_storage.bytes, snapshot_data.data(), snapshot_data.size());",
        f"const auto snapshot = ::{CLOCKWORK_NAMESPACE}::start_lifetime_as<{serialized_type}>(",
        f"  ::std::span<std::byte, sizeof({serialized_type})>{{snapshot_storage.bytes}});",
        f"auto state = ::jewels::memory::make_pmr_shared<::{CLOCKWORK_NAMESPACE}::CogStateDataImpl<{state_type}>>(",
        "  memres_sys, std::move(memres_state));",
        "if (!state)",
        "{",
        f"    return {restore_result}::init_failure;",
        "}",
        f"if (::jewels::fails(::{CLOCKWORK_NAMESPACE}::Serializable<{state_type}>::deserialize(",
        "      ::jewels::Out{state->state}, *snapshot)))",
        "{",
        f"    return {restore_result}::init_failure;",
        "}",
        "*state_out = std::move(state);",
        f"return {restore_result}::success;",
    ]
    return make_params, make_body


def _add_serializable_state_factory_includes(cpp_mod: CppModuleChunks) -> None:
    """Add implementation-only headers required by serialized state restoration."""
    cpp_mod.implementation_chunk.context.add_includes(
        [
            Header(CLK_REPO, "clockwork/memory/start_lifetime_as.hh"),
            Header(CLK_REPO, "clockwork/serializable.hh"),
            Header(JEWELS_REPO, "jewels/callsig/outcome.hh"),
            Header(JEWELS_REPO, "jewels/callsig/outparam.hh"),
            Header(JEWELS_REPO, "jewels/memory/aligned_storage.hh"),
            Header(JEWELS_REPO, "jewels/memory/pmr_shared_ptr.hh"),
            SystemHeader("cstring"),
            SystemHeader("span"),
            SystemHeader("utility"),
        ]
    )


def _get_serializable_state_factory(
    compiler_context: CompilerContext,
    message_type: object,
    state_type: str,
    enclosing_namespace: str,
) -> tuple[str, list[str]] | None:
    """Return a snapshot restoration overload for a serializable extern type."""
    if not isinstance(message_type, ExternType) or message_type.serialized_form is None:
        return None
    assert isinstance(message_type.serialized_form, typesys.Instantiation)
    schema_type = message_type.serialized_form.arguments["schema"]
    serialized_type = typereg.get_cpp_template(message_type.module.context, TAPPY).instantiate(
        [typereg.get_cpp_type(message_type.module.context, schema_type)]
    )
    return _serializable_state_factory(
        state_type,
        serialized_type.render(enclosing_namespace, with_qualifiers=False),
        UuidHandler(
            _get_representation_uuid_type(compiler_context),
            uuid_reg.lookup_uuid(compiler_context, message_type.serialized_form),
        ),
    )


def _comma_append(lines: list[str], end: str = "") -> list[str]:
    """Append a comma to all but the last line, where an optional end is appended instead. Used to help code gen lists."""
    return [i + j for i, j in zip(lines, (",",) * (len(lines) - 1) + (end,), strict=False)]


@dataclass
class ResourceHandle:
    """Manage all aspects of a Resource."""

    resource_name: str
    endpoint_id: UuidHandler
    policy_name: str
    cog_name: str
    index: int

    @classmethod
    def make(cls: type[ResourceHandle], resource: Resource, cog_name: str, index: int) -> ResourceHandle:
        """Make a ResourceHandle instance."""
        resource_name = resource.identifier
        endpoint_id = UuidHandler(ENDPOINT_UUID_TYPE, resource.uuid)
        policy_name = to_camel(resource_name) + "Policy"
        return cls(
            resource_name=resource_name,
            endpoint_id=endpoint_id,
            policy_name=policy_name,
            cog_name=cog_name,
            index=index,
        )

    def render_policy_struct(self) -> CppChunk:
        """Render the Policy struct.

        struct ResourceWorldPolicy
        {
            using MsgType = ***;
            static constexpr auto endpoint_id = ***;
            static constexpr ::std::string_view name = ***;
        }
        """
        chunk = CppChunk()
        chunk.context.add_includes(
            [
                Header(JEWELS_REPO, "jewels/uuid/uuid.hh"),
                SystemHeader("string_view"),
            ]
        )
        body = [
            "using MemoryResourceType = ::jewels::memory::MemoryResource;",
            f"static constexpr auto endpoint_id = {self.endpoint_id.render_from_string_func()};",
            gen_const_str([self.cog_name, self.policy_name]),
        ]
        chunk.append(formatted_struct(self.policy_name, body))
        return chunk

    def render_get(self, arg_resources: str) -> str:
        """Generate get expression."""
        return f"::std::get<{self.index}>({arg_resources})"


@dataclass
class Resources:
    """Manage resources."""

    resource_registry: tuple[ResourceHandle, ...]

    @classmethod
    def make(cls: type[Resources], cog_resources: ResourcesStruct, cog_name: str) -> Resources:
        """Make resources."""
        resources = [
            ResourceHandle.make(resource, cog_name, idx)
            for idx, resource in enumerate(cog_resources.resources.values())
        ]
        return cls(resource_registry=tuple(resources))

    def render_resources(self) -> CppChunk:
        """Render Resources section."""
        chunk = CppChunk()

        # Add relevant headers
        chunk.context.add_includes([Header(CLK_REPO, "clockwork/cog/cog_memory_resources.hh")])

        # Render policies and CogMemoryResources
        chunk.append("/// MemoryResources ///")
        for resource in self.resource_registry:
            chunk.append(resource.render_policy_struct())
        policy_template_args = ", ".join([resource.policy_name for resource in self.resource_registry])
        chunk.append(
            f"using MemoryResourcesType = ::{CLOCKWORK_NAMESPACE}::CogMemoryResources<{policy_template_args}>;"
        )
        return chunk

    def __len__(self) -> int:
        """Convenience size getter."""
        return len(self.resource_registry)

    def __iter__(self) -> Iterator[ResourceHandle]:
        """Convenience iterator."""
        return iter(self.resource_registry)


@dataclass
class ConfigHandle:
    """Manage all aspects of a Config."""

    config_name: str
    msg_type: CppType | CppTemplateType
    endpoint_id: UuidHandler
    policy_name: str
    cog_name: str
    index: int

    @classmethod
    def make(cls: type[ConfigHandle], config: Config, cog_name: str, index: int) -> ConfigHandle:
        """Make a ConfigHandle instance."""
        config_name = config.identifier
        endpoint_id = UuidHandler(ENDPOINT_UUID_TYPE, config.uuid)
        policy_name = to_camel(config_name) + "Policy"
        return cls(
            config_name=config_name,
            msg_type=config.cpp_type,
            endpoint_id=endpoint_id,
            policy_name=policy_name,
            cog_name=cog_name,
            index=index,
        )

    def render_policy_struct(self, enclosing_namespace: str) -> CppChunk:
        """Render the Policy struct.

        struct ConfigWorldPolicy
        {
            using MsgType = ***;
            static constexpr auto endpoint_id = ***;
            static constexpr ::std::string_view name = ***;
        }
        """
        chunk = CppChunk()
        chunk.context.add_includes(
            [
                Header(JEWELS_REPO, "jewels/uuid/uuid.hh"),
                SystemHeader("string_view"),
            ]
        )
        body = [
            f"using ConfigType = {self.msg_type.render(enclosing_namespace, with_qualifiers=False)};",
            f"static constexpr auto endpoint_id = {self.endpoint_id.render_from_string_func()};",
            gen_const_str([self.cog_name, self.policy_name]),
        ]
        chunk.append(formatted_struct(self.policy_name, body))
        return chunk

    def render_get(self, arg_configs: str) -> str:
        """Generate get expression."""
        return f"::std::get<{self.index}>({arg_configs})"


@dataclass
class Configs:
    """Manage configs."""

    config_registry: tuple[ConfigHandle, ...]

    @classmethod
    def make(cls: type[Configs], cog_configs: ConfigsStruct, cog_name: str) -> Configs:
        """Make Configs."""
        configs = [ConfigHandle.make(config, cog_name, idx) for idx, config in enumerate(cog_configs.configs.values())]
        return cls(config_registry=tuple(configs))

    def render_configs(self, enclosing_namespace: str) -> CppChunk:
        """Render Configs section."""
        chunk = CppChunk()

        # Add relevant headers
        chunk.context.add_includes([Header(CLK_REPO, "clockwork/cog/cog_configs.hh")])

        # Render policies and CogConfigs
        chunk.append("/// Configs ///")
        for config in self.config_registry:
            chunk.append(config.render_policy_struct(enclosing_namespace))
        policy_template_args = ", ".join([config.policy_name for config in self.config_registry])
        chunk.append(f"using ConfigsType = ::{CLOCKWORK_NAMESPACE}::CogConfigs<{policy_template_args}>;")
        return chunk

    def __len__(self) -> int:
        """Convenience size getter."""
        return len(self.config_registry)

    def __iter__(self) -> Iterator[ConfigHandle]:
        """Convenience iterator."""
        return iter(self.config_registry)


@dataclass
class StateHandle:
    """Manage all aspects of an State."""

    state_name: str
    msg_type: schema_reg.InterfaceInfo | CogParameterRef | typesys.Instantiation | ExternType
    cpp_type: CppType | CppTemplateType
    endpoint_id: UuidHandler
    read_only: bool
    policy_name: str
    cog_name: str
    index: int

    @classmethod
    def make(cls: type[StateHandle], state: State, cog_name: str, index: int) -> StateHandle:
        """Make a StateHandle instance."""
        state_name = state.identifier
        endpoint_id = UuidHandler(ENDPOINT_UUID_TYPE, state.uuid)
        policy_name = to_camel(state_name) + "Policy"
        return cls(
            state_name=state_name,
            msg_type=state.msg_type,
            cpp_type=state.cpp_type,
            endpoint_id=endpoint_id,
            read_only=state.read_only,
            policy_name=policy_name,
            cog_name=cog_name,
            index=index,
        )

    def render_policy_struct(self, enclosing_namespace: str) -> CppChunk:
        """Render the Policy struct.

        struct StateWorldPolicy
        {
            using MsgType = ***;
            struct Factory;
            static constexpr auto endpoint_id = ***;
            static constexpr bool read_only = ***;
            static constexpr ::std::string_view name = ***
        }
        """
        chunk = CppChunk()
        chunk.context.add_includes(
            [
                Header(JEWELS_REPO, "jewels/uuid/uuid.hh"),
                SystemHeader("string_view"),
            ]
        )
        if isinstance(self.msg_type, ExternType):
            if self.msg_type.serialized_form is not None:
                assert isinstance(self.msg_type.serialized_form, typesys.Instantiation)
                schema_type = self.msg_type.serialized_form.arguments["schema"]
                serialized_type = typereg.get_cpp_template(self.msg_type.module.context, TAPPY).instantiate(
                    [typereg.get_cpp_type(self.msg_type.module.context, schema_type)]
                )
                chunk.context.add_includes(
                    [
                        Header(CLK_REPO, "clockwork/serializable.hh"),
                        *serialized_type.includes,
                    ]
                )
                serialized_type_str = serialized_type.render(enclosing_namespace, with_qualifiers=False)
            else:
                serialized_type_str = None
        elif isinstance(self.msg_type, schema_reg.InterfaceInfo | typesys.Instantiation):
            serialized_type_str = "StateType"
        else:
            serialized_type_str = None
        body = [f"using StateType = {self.cpp_type.render(enclosing_namespace, with_qualifiers=False)};"]
        if serialized_type_str:
            body.append(f"using SerializedType = {serialized_type_str};")
        body.extend(
            [
                "struct Factory;",
                f"static constexpr auto endpoint_id = {self.endpoint_id.render_from_string_func()};",
                f"static constexpr bool read_only = {'true' if self.read_only else 'false'};",
                gen_const_str([self.cog_name, self.policy_name]),
            ]
        )
        chunk.append(formatted_struct(self.policy_name, body))
        return chunk

    def render_factory_struct(
        self, compiler_context: CompilerContext, parent_policy_name: str, enclosing_namespace: str
    ) -> CppModuleChunks:
        """Render the factory struct implementation."""
        if isinstance(self.msg_type, ExternType):
            make_params = "::jewels::memory::MemoryResource memres_state"
            make_args = "std::move(memres_state)"
            repr_uuid = uuid_reg.lookup_uuid(compiler_context, self.msg_type)
        elif isinstance(self.msg_type, schema_reg.InterfaceInfo):
            make_params = f"::{CLOCKWORK_NAMESPACE}::pinion::PublisherHandle publisher"
            make_args = "std::move(publisher)"
            assert self.msg_type.interface_ir.representation is not None
            repr_uuid = uuid_reg.lookup_uuid(compiler_context, self.msg_type.interface_ir.representation.typespec)
        else:
            msg = f"Unknown state message type {self.msg_type}"
            raise TypeError(msg)
        snapshot_make = _get_serializable_state_factory(
            compiler_context,
            self.msg_type,
            const_qualify(self.cpp_type, False).render(enclosing_namespace),
            enclosing_namespace,
        )
        cpp_mod = _gen_factory(
            UuidHandler(_get_representation_uuid_type(compiler_context), repr_uuid),
            f"{parent_policy_name}::{self.policy_name}::",
            "CogStateFactory",
            "::jewels::memory::MemoryResource memres_sys, " + make_params,
            f"return ::jewels::memory::make_pmr_shared<::{CLOCKWORK_NAMESPACE}::CogStateDataImpl<{const_qualify(self.cpp_type, False).render(enclosing_namespace)}>>(memres_sys, {make_args});",
            snapshot_make=snapshot_make,
        )
        if snapshot_make:
            _add_serializable_state_factory_includes(cpp_mod)
        return cpp_mod

    def render_get(self, arg_states: str) -> str:
        """Generate get expression."""
        return f"::std::get<{self.index}>({arg_states})"


@dataclass
class States:
    """Manage states."""

    state_registry: tuple[StateHandle, ...]

    @classmethod
    def make(cls: type[States], cog_states: StatesStruct, cog_name: str) -> States:
        """Make States."""
        states = [StateHandle.make(state, cog_name, idx) for idx, state in enumerate(cog_states.states.values())]
        return cls(state_registry=tuple(states))

    def render_states(self, enclosing_namespace: str) -> CppChunk:
        """Render States section."""
        chunk = CppChunk()

        # Add relevant headers
        chunk.context.add_includes([Header(CLK_REPO, "clockwork/cog/cog_states.hh")])

        # Render policies and CogStates
        chunk.append("/// States ///")
        for state in self.state_registry:
            chunk.append(state.render_policy_struct(enclosing_namespace))
        policy_template_args = ", ".join([state.policy_name for state in self.state_registry])
        chunk.append(f"using StatesType = ::{CLOCKWORK_NAMESPACE}::CogStates<{policy_template_args}>;")
        return chunk

    def render_factories(
        self, compiler_context: CompilerContext, parent_policy_name: str, enclosing_namespace: str
    ) -> CppModuleChunks:
        """Render the factory struct implementations for the states."""
        chunks = CppModuleChunks()
        for state in self.state_registry:
            chunks.append(state.render_factory_struct(compiler_context, parent_policy_name, enclosing_namespace))
        return chunks

    def __len__(self) -> int:
        """Convenience size getter."""
        return len(self.state_registry)

    def __iter__(self) -> Iterator[StateHandle]:
        """Convenience iterator."""
        return iter(self.state_registry)


@dataclass
class TimeSinceLastExecHandler:
    """Manage TimeSinceLastExec condition."""

    name: str
    threshold_ns: int
    endpoint_id: UuidHandler
    policy_name: str
    cog_name: str
    index: int

    @classmethod
    def make(
        cls: type[TimeSinceLastExecHandler],
        condition: TimeSinceLastExecCondition,
        cog_name: str,
        index: int,
    ) -> TimeSinceLastExecHandler:
        """Make a TimeSinceLastExecHandler instance."""
        endpoint_id = UuidHandler(ENDPOINT_UUID_TYPE, condition.uuid)
        name = condition.identifier
        policy_name = to_camel(name) + "Policy"
        return cls(
            name=name,
            threshold_ns=int(condition.time_ns),
            endpoint_id=endpoint_id,
            policy_name=policy_name,
            cog_name=cog_name,
            index=index,
        )

    def render_policy_struct(self) -> CppChunk:
        """Render the Policy struct.

        struct PeriodicTimer
        {
            static constexpr int64_t threshold_ns = ***;
            static constexpr auto endpoint_id = ***;
            static constexpr ::std::string_view name = ***;
        }
        """
        chunk = CppChunk()
        chunk.context.add_includes(
            [
                Header(JEWELS_REPO, "jewels/uuid/uuid.hh"),
                SystemHeader("cstdint"),
                SystemHeader("string_view"),
            ]
        )
        body = [
            f"static constexpr int64_t threshold_ns = {self.threshold_ns};",
            f"static constexpr auto endpoint_id = {self.endpoint_id.render_from_string_func()};",
            gen_const_str([self.cog_name, self.policy_name]),
        ]
        chunk.append(formatted_struct(self.policy_name, body))
        return chunk

    def render_get(self, arg_timers: str) -> str:
        """Generate get expression."""
        return f"::std::get<{self.index}>({arg_timers})"


# Type alias for any timer handler used in the codegen.
TimerHandler = TimeSinceLastExecHandler | DynamicTimerCodegenHandler


@dataclass
class Timers:
    """Manage timers."""

    timer_registry: tuple[TimerHandler, ...]
    cond_name_to_timer: dict[str, TimerHandler]
    dynamic_timer_handler: DynamicTimerCodegenHandler | None

    @classmethod
    def make(
        cls: type[Timers],
        cog_exec_conditions: ConditionsStruct,
        cog_name: str,
    ) -> Timers:
        """Make Timers."""
        timer_conditions: list[TimeSinceLastExecCondition | DynamicTimerCondition] = [
            cond
            for cond in cog_exec_conditions.conditions.values()
            if isinstance(cond, TimeSinceLastExecCondition | DynamicTimerCondition)
        ]
        registry: list[TimerHandler] = []
        dynamic_handler: DynamicTimerCodegenHandler | None = None
        for idx, cond in enumerate(timer_conditions):
            if isinstance(cond, TimeSinceLastExecCondition):
                registry.append(TimeSinceLastExecHandler.make(cond, cog_name, idx))
            else:
                assert isinstance(cond, DynamicTimerCondition)
                handler = DynamicTimerCodegenHandler.make(cond, cog_name, idx)
                registry.append(handler)
                dynamic_handler = handler
        cond_name_to_timer = {timer.name: timer for timer in registry}
        return cls(
            timer_registry=tuple(registry), cond_name_to_timer=cond_name_to_timer, dynamic_timer_handler=dynamic_handler
        )

    def render_timers(self) -> CppChunk:
        """Render Timers section.

        Note: The dynamic timer policy struct (if any) is NOT rendered here.
        It is rendered in the dial header by cppdial.py, since the dial needs
        access to the concrete handler type for AlignerTimerControl.
        """
        chunk = CppChunk()

        # Add relevant headers
        chunk.context.add_includes([Header(CLK_REPO, "clockwork/cog/cog_timers.hh")])

        chunk.append("/// Timers ///")
        for timer in self.timer_registry:
            if not isinstance(timer, DynamicTimerCodegenHandler):
                chunk.append(timer.render_policy_struct())
        policy_template_args = ", ".join([timer.policy_name for timer in self.timer_registry])
        chunk.append(f"using TimersType = ::{CLOCKWORK_NAMESPACE}::CogTimers<{policy_template_args}>;")

        if self.dynamic_timer_handler is not None:
            chunk.append("")
            chunk.append("static constexpr bool has_dynamic_timer = true;")
            chunk.append(f"static constexpr ::std::size_t dynamic_timer_index = {self.dynamic_timer_handler.index};")
            chunk.context.add_includes([SystemHeader("cstddef")])

        return chunk

    def __len__(self) -> int:
        """Convenience size getter."""
        return len(self.timer_registry)

    def __iter__(self) -> Iterator[TimerHandler]:
        """Convenience iterator."""
        return iter(self.timer_registry)


@dataclass
class InputHandler:
    """Manage all aspects of an input."""

    input_name: str
    cog_input: Input
    endpoint_id: UuidHandler
    policy_name: str
    index: int
    cog_name: str

    @classmethod
    def make(
        cls: type[InputHandler],
        cog_input: Input,
        cog_name: str,
        index: int,
        policy_prefix: str = "",
    ) -> InputHandler:
        """Make InputHandler."""
        endpoint_id = UuidHandler(ENDPOINT_UUID_TYPE, cog_input.uuid)
        input_name = cog_input.identifier
        policy_name = to_camel((policy_prefix + "_" + input_name) if policy_prefix else input_name) + "Policy"
        return cls(
            input_name=input_name,
            cog_input=cog_input,
            endpoint_id=endpoint_id,
            policy_name=policy_name,
            index=index,
            cog_name=cog_name,
        )

    def render_policy_struct(self) -> CppChunk:
        """Render Policy struct.

        struct HelloWorldPolicy
        {
           using MsgType = ***;
           static constexpr auto endpoint_id = uuid***;
           static constexpr ::std::string_view name = ***;
           static constexpr auto max_view_size = ***;
           static constexpr auto min_msgs = ***;
           static constexpr auto min_new_msgs = ***;
           static constexpr std::optional<::ssize_t> safety_margin = ***;
           static constexpr std::optional<size_t> skip_threshold = ***;
           static constexpr auto copy_inputs = ***;
           static constexpr auto manual_cursor = ***;
        };

        """
        chunk = CppChunk()

        chunk.context.add_includes(
            [
                Header(JEWELS_REPO, "jewels/uuid/uuid.hh"),
                SystemHeader("sys/types.h"),
                SystemHeader("cstdint"),
                SystemHeader("optional"),
                SystemHeader("string_view"),
                *self.cog_input.msg_type.includes,
            ]
        )

        # Parse parameters
        msg_type = self.cog_input.msg_type.render("")
        max_view_size = f"{self.cog_input.max_msgs}U"
        min_msgs = f"{self.cog_input.min_msgs}U"
        min_new_msgs = f"{self.cog_input.min_new_msgs}U"
        safety_margin = "std::nullopt" if self.cog_input.safety_margin is None else f"{self.cog_input.safety_margin}"
        skip_threshold = (
            "std::nullopt" if self.cog_input.skip_threshold is None else f"{self.cog_input.skip_threshold}U"
        )
        manual_cursor = f"{self.cog_input.manual_cursor}"
        expose_seqno = f"{self.cog_input.expose_seqno}"
        use_device_ptr = f"{self.cog_input.use_device_ptr}"

        # fmt: off
        body = [
            f"using MsgType = {msg_type};",
            f"static constexpr auto endpoint_id = {self.endpoint_id.render_from_string_func()};",
            gen_const_str([self.cog_name, self.policy_name]),
            f"static constexpr auto max_view_size = {max_view_size};",
            f"static constexpr auto min_msgs = {min_msgs};",
            f"static constexpr auto min_new_msgs = {min_new_msgs};",
            f"static constexpr std::optional<::ssize_t> safety_margin = {safety_margin};",
            f"static constexpr std::optional<size_t> skip_threshold = {skip_threshold};",
            f"static constexpr auto copy_inputs = {str(self.cog_input.copy_inputs).lower()};",
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            f"static constexpr auto manual_cursor = {str(manual_cursor).lower()};",
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            f"static constexpr auto expose_seqno = {str(expose_seqno).lower()};",
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            f"static constexpr auto use_device_ptr = {str(use_device_ptr).lower()};",
        ]
        # fmt: on
        chunk.append(formatted_struct(self.policy_name, body))
        return chunk

    def render_get(self, arg_name: str) -> str:
        """Generate get expression."""
        return f"::std::get<{self.index}>({arg_name})"


@dataclass
class AlignedInputGroup:
    """Tracks InputHandlers for an aligned input group."""

    group_name: str
    alignment_handler: InputHandler
    upstream_handlers: list[InputHandler]
    resolved_inputs: dict[str, aligner_ir.ResolvedAlignerInput]


@dataclass
class Inputs:
    """Manage all Inputs."""

    inputs_registry: tuple[InputHandler, ...]
    input_uuid_map: dict[str, UuidHandler]
    regular_handlers: list[InputHandler]
    aligned_groups: list[AlignedInputGroup]

    @classmethod
    def make(
        cls: type[Inputs],
        cog_inputs: InputsStruct,
        cog_name: str,
    ) -> Inputs:
        """Make Inputs."""
        registry: list[InputHandler] = []
        regular_handlers: list[InputHandler] = []
        aligned_groups: list[AlignedInputGroup] = []
        idx = 0

        for cog_ipt in cog_inputs.inputs.values():
            if cog_ipt.no_dial:
                continue
            handler = InputHandler.make(cog_ipt, cog_name, idx)
            registry.append(handler)
            regular_handlers.append(handler)
            idx += 1

        for aligned in cog_inputs.aligned_inputs:
            alignment_handler = InputHandler.make(
                aligned.alignment_msg_input,
                cog_name,
                idx,
            )
            registry.append(alignment_handler)
            idx += 1

            upstream_handlers: list[InputHandler] = []
            for upstream in aligned.upstream_inputs:
                handler = InputHandler.make(
                    upstream,
                    cog_name,
                    idx,
                    policy_prefix=aligned.group_name,
                )
                registry.append(handler)
                upstream_handlers.append(handler)
                idx += 1

            aligned_groups.append(
                AlignedInputGroup(
                    group_name=aligned.group_name,
                    alignment_handler=alignment_handler,
                    upstream_handlers=upstream_handlers,
                    resolved_inputs=aligned.resolved_aligner_inputs,
                )
            )

        input_uuid_map: dict[str, UuidHandler] = {}
        for ipt in registry:
            assert ipt.input_name not in input_uuid_map, f"Duplicate input name '{ipt.input_name}' in cog '{cog_name}'"
            input_uuid_map[ipt.input_name] = ipt.endpoint_id
        return cls(
            inputs_registry=tuple(registry),
            input_uuid_map=input_uuid_map,
            regular_handlers=regular_handlers,
            aligned_groups=aligned_groups,
        )

    def render_inputs(self) -> CppChunk:
        """Render Inputs section."""
        chunk = CppChunk()

        # Add relevant headers
        chunk.context.add_includes(
            [
                Header(CLK_REPO, "clockwork/cog/cog_inputs.hh"),
            ]
        )

        # Render policies and CogInputs
        chunk.append("/// Inputs ///")
        for ipt in self.inputs_registry:
            chunk.append(ipt.render_policy_struct())
        policy_template_args = ", ".join([ipt.policy_name for ipt in self.inputs_registry])
        chunk.append(f"using InputsType = ::{CLOCKWORK_NAMESPACE}::CogInputs<{policy_template_args}>;")

        return chunk

    def __len__(self) -> int:
        """Convenience size getter."""
        return len(self.inputs_registry)

    def __iter__(self) -> Iterator[InputHandler]:
        """Convenience iterator."""
        return iter(self.inputs_registry)


@dataclass
class InputConditionHandler:
    """Manage execution condition associated with an Input."""

    name: str
    exec_condition: MessagesPresentCondition
    input_endpoint_id: UuidHandler
    policy_name: str
    index: int
    cog_name: str

    @classmethod
    def make(
        cls: type[InputConditionHandler],
        exec_condition: MessagesPresentCondition,
        index: int,
        cog_name: str,
        input_endpoint_id: UuidHandler,
    ) -> InputConditionHandler:
        """Make InputConditionHandler."""
        name = exec_condition.identifier
        policy_name = to_camel(name) + "Policy"
        return cls(
            name=name,
            exec_condition=exec_condition,
            input_endpoint_id=input_endpoint_id,
            policy_name=policy_name,
            index=index,
            cog_name=cog_name,
        )

    def render_policy_struct(self) -> CppChunk:
        """Render Policy struct.

        struct AnyMsgPolicy
        {
           using MsgType = ***;
           static constexpr auto endpoint_id = uuid***;
           static constexpr ::std::string_view name = ***;
           static constexpr auto bounds_min = ***;
           static constexpr auto bounds_max = ***;
           static constexpr auto condition_type = ***;
        };

        """
        # Translate condition type to string
        input_condition_type: dict[type[ConditionBase], str] = {
            AnyMessageCondition: f"::{CLOCKWORK_NAMESPACE}::InputConditionType::any_message",
            NewMessageCondition: f"::{CLOCKWORK_NAMESPACE}::InputConditionType::new_message",
        }

        chunk = CppChunk()

        chunk.context.add_includes(
            [
                Header(JEWELS_REPO, "jewels/uuid/uuid.hh"),
                Header(CLK_REPO, "clockwork/cog/input_condition.hh"),
                SystemHeader("cstdint"),
                SystemHeader("string_view"),
            ]
        )

        # Parse parameters
        bounds_min = f"{self.exec_condition.lower_bound}U"
        if self.exec_condition.is_upper_bound_max():
            bounds_max = "::std::numeric_limits<uint32_t>::max()"
            chunk.context.add_include(SystemHeader("limits"))
        else:
            bounds_max = f"{self.exec_condition.upper_bound}U"
        condition_type = input_condition_type[type(self.exec_condition)]

        policy_name = to_camel(self.name) + "Policy"
        body = [
            f"static constexpr auto endpoint_id = {self.input_endpoint_id.render_from_string_func()};",
            gen_const_str([self.cog_name, policy_name]),
            f"static constexpr auto bounds_min = {bounds_min};",
            f"static constexpr auto bounds_max = {bounds_max};",
            f"static constexpr auto condition_type = {condition_type};",
        ]
        chunk.append(formatted_struct(policy_name, body))
        return chunk

    def render_get(self, arg_name: str) -> str:
        """Generate get expression."""
        return f"::std::get<{self.index}>({arg_name})"


@dataclass
class InputConditions:
    """Manage all input conditions."""

    input_conditions_registry: dict[str, InputConditionHandler]

    @classmethod
    def make(
        cls: type[InputConditions],
        cog_exec_conditions: ConditionsStruct,
        cog_name: str,
        input_uuid_map: dict[str, UuidHandler],
    ) -> InputConditions:
        """Make InputConditions."""
        conditions = {}
        idx = 0
        for cond in cog_exec_conditions.conditions.values():
            if isinstance(cond, MessagesPresentCondition):
                if cond.input_name not in input_uuid_map:
                    msg = f"Condition input {cond.input_name} does not exist."
                    raise RuntimeError(msg)
                conditions[cond.identifier] = InputConditionHandler.make(
                    cond, idx, cog_name, input_uuid_map[cond.input_name]
                )
                idx += 1
        return cls(conditions)

    def render_input_conditions(self) -> CppChunk:
        """Render InputConditiosn section."""
        chunk = CppChunk()

        # Add relevant headers
        chunk.context.add_includes(
            [
                Header(CLK_REPO, "clockwork/cog/cog_conditions.hh"),
            ]
        )

        # Render policies and ConditionsType
        chunk.append("/// InputConditions ///")
        for input_condition in self.input_conditions_registry.values():
            chunk.append(input_condition.render_policy_struct())
        policy_template_args = ", ".join(
            [input_condition.policy_name for input_condition in self.input_conditions_registry.values()]
        )
        chunk.append(f"using ConditionsType = ::{CLOCKWORK_NAMESPACE}::CogConditions<{policy_template_args}>;")
        return chunk

    def __len__(self) -> int:
        """Convenience size getter."""
        return len(self.input_conditions_registry)

    def __iter__(self) -> Iterator[InputConditionHandler]:
        """Convenience iterator."""
        return iter(self.input_conditions_registry.values())


@dataclass
class PublisherHandler:
    """Manage all aspects of an Output."""

    output_name: str
    msg_type: CppType | CppTemplateType
    endpoint_id: UuidHandler
    policy_name: str
    index: int
    cog_name: str
    rate_limit: cog.ResolvedRateLimitSpec | None
    metrics_log_type: cog.MetricsLogType
    max_msgs_per_exec: int
    cog_metrics_output: bool
    is_report_group: bool

    @classmethod
    def make(cls: type[PublisherHandler], output: Output, cog_name: str, index: int) -> PublisherHandler:
        """Make a TimeSinceLastExecHandler instance."""
        output_name = output.identifier
        endpoint_id = UuidHandler(ENDPOINT_UUID_TYPE, output.uuid)
        policy_name = to_camel(output_name) + "Policy"
        return cls(
            output_name=output_name,
            msg_type=output.msg_type,
            endpoint_id=endpoint_id,
            policy_name=policy_name,
            index=index,
            cog_name=cog_name,
            rate_limit=output.rate_limit,
            metrics_log_type=output.metrics_log_type,
            max_msgs_per_exec=output.max_msgs_per_exec,
            cog_metrics_output=output.cog_metrics_output,
            is_report_group=output.is_report_group,
        )

    def render_policy_struct(self) -> CppChunk:
        """Render the Policy struct.

        struct OutWorldPolicy
        {
            using MsgType = ***;
            static constexpr auto endpoint_id = ***;
            static constexpr ::std::string_view name = ***;
        }
        """
        chunk = CppChunk()
        chunk.context.add_includes(
            [
                Header(JEWELS_REPO, "jewels/uuid/uuid.hh"),
                SystemHeader("cstddef"),
                SystemHeader("cstdint"),
                SystemHeader("string_view"),
                SystemHeader("optional"),
                *self.msg_type.includes,
            ]
        )
        # Report group publishers never participate in infra diagnostics (frequency signals),
        # so has_diagnostics must be false for them regardless of metrics_log_type.
        has_diag = not self.is_report_group and self.metrics_log_type == cog.MetricsLogType.none
        body = [
            f"using MsgType = {self.msg_type.render('')};",
            f"static constexpr auto endpoint_id = {self.endpoint_id.render_from_string_func()};",
            gen_const_str([self.cog_name, self.policy_name]),
            f"static constexpr bool has_diagnostics = {'true' if has_diag else 'false'};",
            f"static constexpr size_t max_msgs_per_exec = {self.max_msgs_per_exec}U;",
        ]
        if self.rate_limit:
            period_ns = int(self.rate_limit.period_s * 1e9)
            body.append(
                f"static constexpr std::optional<::clockwork::RateLimitParameters> rate_limit_params{{{{.limit={self.rate_limit.limit}U, .period=std::chrono::nanoseconds{{{period_ns}U}}}}}};"
            )
        else:
            body.append("static constexpr std::optional<::clockwork::RateLimitParameters> rate_limit_params{};")

        chunk.append(formatted_struct(self.policy_name, body))
        return chunk

    def render_get(self, arg_publishables: str) -> str:
        """Generate get expression."""
        return f"::std::get<{self.index}>({arg_publishables})"


@dataclass
class Publishers:
    """Manage all publishers."""

    publisher_registry: tuple[PublisherHandler, ...]

    @classmethod
    def make(
        cls: type[Publishers],
        cog_outputs: OutputsStruct,
        cog_name: str,
    ) -> Publishers:
        """Make Publishers."""
        registry = [
            PublisherHandler.make(output, cog_name, idx) for idx, output in enumerate(cog_outputs.outputs.values())
        ]
        return cls(publisher_registry=tuple(registry))

    def render_publishers(self) -> CppChunk:
        """Render Publishers section."""
        chunk = CppChunk()
        # Add relevent headers
        chunk.context.add_includes(
            [
                Header(CLK_REPO, "clockwork/cog/cog_publishers.hh"),
            ]
        )

        # Render policies and CogPublishers
        chunk.append("/// Publishers ///")
        for publisher in self.publisher_registry:
            chunk.append(publisher.render_policy_struct())
        policy_template_args = ", ".join([publisher.policy_name for publisher in self.publisher_registry])
        chunk.append(f"using PublishersType = ::{CLOCKWORK_NAMESPACE}::CogPublishers<{policy_template_args}>;")
        # create a constexpr index for each of the metrics logging publishers in the registry
        for publisher in self.publisher_registry:
            if (
                publisher.cog_metrics_output
                and publisher.metrics_log_type == cog.MetricsLogType.non_redundant_telemetry
            ):
                chunk.append(_gen_const_size(publisher.index, "telemetry_metrics_index"))
            elif publisher.cog_metrics_output and publisher.metrics_log_type == cog.MetricsLogType.event:
                chunk.append(_gen_const_size(publisher.index, "event_metrics_index"))
        # create a constexpr index for each report group publisher
        for publisher in self.publisher_registry:
            if publisher.is_report_group:
                chunk.append(_gen_const_size(publisher.index, f"{publisher.output_name}_index"))
        # get the template arguments for the non metrics publishers
        non_metrics_template_args = [
            publisher.policy_name
            for publisher in self.publisher_registry
            if publisher.metrics_log_type not in (cog.MetricsLogType.non_redundant_telemetry, cog.MetricsLogType.event)
            and not publisher.is_report_group
        ]
        chunk.append(
            f"using OutputPublishersType = ::{CLOCKWORK_NAMESPACE}::CogPublishers<{', '.join(non_metrics_template_args)}>;"
        )
        # get the template arguments for the metrics publishers (only cog_metrics_output=True, excludes report groups)
        metrics_template_args = [
            publisher.policy_name
            for publisher in self.publisher_registry
            if publisher.cog_metrics_output
            and publisher.metrics_log_type in (cog.MetricsLogType.non_redundant_telemetry, cog.MetricsLogType.event)
        ]
        chunk.append(
            f"using MetricsPublishersType = ::{CLOCKWORK_NAMESPACE}::CogPublishers<{', '.join(metrics_template_args)}>;"
        )

        if any(
            not publisher.is_report_group
            and (
                (publisher.metrics_log_type == cog.MetricsLogType.non_redundant_telemetry)
                | (publisher.metrics_log_type == cog.MetricsLogType.event)
            )
            for publisher in self.publisher_registry
        ):
            chunk.append("static constexpr auto publish_metrics = true;")
        else:
            chunk.append("static constexpr auto publish_metrics = false;")
        return chunk

    def __len__(self) -> int:
        """Convenience size getter."""
        return len(self.publisher_registry)

    def __iter__(self) -> Iterator[PublisherHandler]:
        """Convenience iterator."""
        return iter(self.publisher_registry)


def to_dial_name(cog_name: str) -> str:
    """Convert a cogs name to its dial name."""
    return f"{cog_name}Dial"


@dataclass
class ExecuteExprHandler:
    """Handle Condition-When."""

    execute_when_condition: cog.ConditionExpr

    def init_condition_present(self) -> bool:
        """Check for presence of any timer condition."""
        return _cond_type_present(self.execute_when_condition, cog.InitConditionExpr)

    def log_condition_present(self) -> bool:
        """Check for presence of init condition."""
        return _cond_type_present(self.execute_when_condition, cog.LogConditionExpr)

    def time_condition_present(self) -> bool:
        """Check for presence of any timer condition (TimeSinceLastExec or DynamicTimer)."""
        return _cond_type_present(self.execute_when_condition, cog.TimeSinceLastExec) or _cond_type_present(
            self.execute_when_condition, DynamicTimer
        )

    def message_condition_present(self) -> bool:
        """Check for presence of any message condition."""
        return _cond_type_present(self.execute_when_condition, cog.MessagesPresent)

    def render(
        self,
        timer_handle_map: dict[str, TimerHandler],
        arg_timers: str,
        input_condition_handle_map: dict[str, InputConditionHandler],
        arg_input_conditions: str,
    ) -> str:
        """Render execution conditional."""
        return _execute_expr_render_helper(
            self.execute_when_condition, timer_handle_map, arg_timers, input_condition_handle_map, arg_input_conditions
        )


@dataclass
class DiagnosticsHandler:
    """Manage diagnostics."""

    name: str
    group_id: str | CogParameterRef | None
    instance_id: str | CogParameterRef | None
    manager_type: CppType | CppTemplateType
    reporter_type: CppType | CppTemplateType
    endpoint_id: UuidHandler
    policy_name: str
    index: int
    cog_name: str

    @classmethod
    def make(
        cls: type[DiagnosticsHandler], cog_diagnostics: Diagnostics, cog_name: str, index: int
    ) -> DiagnosticsHandler:
        """Make Diagnostics."""
        endpoint_id = UuidHandler(ENDPOINT_UUID_TYPE, cog_diagnostics.uuid)
        policy_name = to_camel(cog_diagnostics.identifier) + "Policy"
        return cls(
            name=cog_diagnostics.identifier,
            group_id=cog_diagnostics.group_id,
            instance_id=cog_diagnostics.instance_id,
            manager_type=cog_diagnostics.manager_type,
            reporter_type=cog_diagnostics.reporter_type,
            endpoint_id=endpoint_id,
            policy_name=policy_name,
            index=index,
            cog_name=cog_name,
        )

    def render_policy_struct(self, extra_defs: Iterable[str] = ()) -> CppChunk:
        """Render States section.

        Args:
            extra_defs: Additional lines strings to include in the policy struct.
        """
        chunk = CppChunk()
        chunk.context.add_includes(
            [
                Header(JEWELS_REPO, "jewels/uuid/uuid.hh"),
                SystemHeader("string_view"),
                *self.manager_type.includes,
            ]
        )
        if (
            isinstance(self.manager_type, CppTemplateType)
            and self.manager_type.arguments is not None
            and self.manager_type.template_name == "ClockworkManagerStruct"
        ):
            lazy_group = self.manager_type.arguments[0]
            lazy_manager = self.manager_type
            assert lazy_manager.arguments is not None  # pyright can't follow the assignment
            lazy_manager.arguments[0] = CppType([], "GroupType", None)
            manager_def = [
                f"template<typename GroupType = {lazy_group.render('')}>",
                f"using ManagerType = {lazy_manager.render('')};",
            ]
        else:
            manager_def = [f"using ManagerType = {self.manager_type.render('')};"]
        chunk.append(
            formatted_struct(
                self.policy_name,
                [
                    f"static constexpr auto endpoint_id = {self.endpoint_id.render_from_string_func()};",
                    gen_const_str([self.cog_name, self.name]),
                    gen_const_str(name="member_name", terms=self.name),
                    gen_const_str(name="group_name", terms=(self.group_id or "")),
                    gen_const_str(name="instance_name", terms=(self.instance_id or "")),
                    *manager_def,
                    *extra_defs,
                ],
            )
        )
        return chunk

    def __len__(self) -> int:
        """Returns how many diagnostic managers the cog will have."""
        return 0 if self.manager_type is VOID else 1

    def render_get(self, arg_resources: str) -> str:
        """Generate get expression."""
        return f"::std::get<{self.index}>({arg_resources})"


@dataclass
class Diagnosticses:
    """Manage all diagnostics."""

    diagnostics_registry: tuple[DiagnosticsHandler, ...]

    @classmethod
    def make(
        cls: type[Diagnosticses],
        cog_diagnostics: DiagnosticsStruct,
        cog_name: str,
    ) -> Diagnosticses:
        """Make Diagnostics."""
        registry = [
            DiagnosticsHandler.make(diagnostics, cog_name, idx)
            for idx, diagnostics in enumerate(cog_diagnostics.diagnostics.values())
        ]
        return cls(diagnostics_registry=tuple(registry))

    def render_diagnostics(self) -> CppChunk:
        """Render Diagnostics section."""
        chunk = CppChunk()
        # Add relevant headers
        chunk.context.add_include(Header(CLK_REPO, "clockwork/cog/cog_diagnostics.hh"))

        # Render policies and CogDiagnostics
        chunk.append("/// Diagnostics ///")
        for diagnostics in self.diagnostics_registry:
            chunk.append(diagnostics.render_policy_struct())
        policy_template_args = ", ".join([diagnostics.policy_name for diagnostics in self.diagnostics_registry])
        chunk.append(f"using DiagnosticsType = ::{CLOCKWORK_NAMESPACE}::CogDiagnostics<{policy_template_args}>;")
        return chunk

    def __len__(self) -> int:
        """Convenience size getter."""
        return len(self.diagnostics_registry)

    def __iter__(self) -> Iterator[DiagnosticsHandler]:
        """Convenience iterator."""
        return iter(self.diagnostics_registry)


@dataclass
class InfraDiagnostics:
    """Class for infra diagnostics."""

    policy: DiagnosticsHandler
    cog_class_name: str
    fault_header: Header
    signals: list[DiagnosticsSignalDef]
    is_template: bool = False

    @classmethod
    def make(  # noqa: PLR0913 (these could possibly be made into a dataclass or named tuple for parameter packing)
        cls: type[InfraDiagnostics],
        cog_diagnostics: Diagnostics,
        infra_diagnostics: InfraDiagnosticsDef,
        cog_name: str,
        cog_class_name: str,
        dial_header: Header,
        *,
        is_template: bool = False,
    ) -> InfraDiagnostics:
        """Make infra diagnostics."""
        endpoint_id = UuidHandler(ENDPOINT_UUID_TYPE, cog_diagnostics.uuid)
        policy_name = to_camel(cog_diagnostics.identifier) + "Policy"
        fault_header = infra_defs_header_from_dial_header(dial_header)
        assert infra_diagnostics.signals is not None
        return cls(
            policy=DiagnosticsHandler(
                name=cog_diagnostics.identifier,
                group_id=cog_diagnostics.group_id,
                instance_id=cog_diagnostics.instance_id,
                manager_type=cog_diagnostics.manager_type,
                reporter_type=cog_diagnostics.reporter_type,
                endpoint_id=endpoint_id,
                policy_name=policy_name,
                index=-1,
                cog_name=cog_name,
            ),
            cog_class_name=cog_class_name,
            fault_header=fault_header,
            signals=infra_diagnostics.signals,
            is_template=is_template,
        )

    def render_diagnostics(self, enclosing_namespace: str) -> CppChunk:
        """Render Infrastructure Diagnostics section."""
        chunk = CppChunk()
        diag_ns = DIAGNOSTICS_NAMESPACE
        base_name = to_camel(self.policy.name)
        signal_id_name = base_name + "SignalId"
        threshold_prefix = f"::{enclosing_namespace}::{to_snake(self.cog_class_name)}_"
        detector_ns = f"{diag_ns}::fault::detector"
        # Render Signal Descriptors
        signals_no_fault = [
            f'::{diag_ns}::Descriptor<{signal_id_name}::{signal.name}, {signal.type}>{{"{signal.name}", "N/A"}}'
            for signal in self.signals
        ]
        # Add relevant headers.  Include string and utility since iwyu can't decide if they are required
        chunk.context.add_include(SystemHeader("string", iwyu_pragma="IWYU pragma: keep"))
        chunk.context.add_include(SystemHeader("utility", iwyu_pragma="IWYU pragma: keep"))
        chunk.append("/// Infra Diagnostics ///")
        # Render signal enum
        # Render Signal Group
        chunk.append(f"struct {COG_INFRA_DIAGS_GROUP_DEF_NAME}")
        chunk.append("{")
        chunk.append("};")
        # Render policy
        chunk.append(
            self.policy.render_policy_struct(
                [
                ],
            )
        )
        chunk.append(
            f"using InfraDiagnosticsType = ::{CLOCKWORK_NAMESPACE}::CogInfraDiagnostics<{self.policy.policy_name}>;"
        )
        return chunk


@dataclass(frozen=True)
class EventMetricsSignalPopulation:
    """Additional inputs needed for event metrics signal population."""

    input_sequence_metadata_types: dict[str, str]
    """Input name to metadata type for input sequence-number metadata."""

    outputs_with_first_sequence_number_signal: set[str]
    """Output names whose event metrics group includes a first-sequence-number signal."""

    publishables_arg_name: str
    """Argument name to use when accessing output publishables."""


@dataclass
class Cog:
    """Representation of a C++ Cog."""

    cog_ir: cog.Cog
    class_name: str
    dial_name: str
    header_name: str | None
    cpp_namespace: str
    dial_header: Header

    resources: Resources | None = None
    configs: Configs | None = None
    states: States | None = None
    timers: Timers | None = None
    inputs: Inputs | None = None
    input_conditions: InputConditions | None = None
    publishers: Publishers | None = None
    diagnostics: Diagnosticses | None = None
    infra_diags: InfraDiagnostics | None = None
    signals_struct: SignalsStruct | None = None
    template_params: str = ""
    template_args: str = ""

    cog_policy_name: str = ""

    @classmethod
    def make(  # noqa: PLR0913 (this is the central cog generation function so needs several parameters)
        cls: type[Cog],
        cog_ir: cog.Cog,
        class_name: str,
        dial_name: str | None,
        header_name: str | None,
        cpp_namespace: str,
        dial_header: Header,
    ) -> Cog:
        """Make Cog instance."""
        if not dial_name:
            dial_name = to_dial_name(class_name)
        return cls(
            cog_ir=cog_ir,
            class_name=class_name,
            header_name=header_name,
            dial_name=dial_name,
            cpp_namespace=cpp_namespace,
            dial_header=dial_header,
        )

    def render(self) -> CppModuleChunks:  # noqa: PLR0915 naturally large with no easy breaks
        """Generate header and source skeleton for Cog.

        Special situations:
        - Toolchain adds namespace enclosure, so it will not be explicitly added here.
        """
        cpp_mod = CppModuleChunks()
        cpp_mod.header_chunk.context.add_includes(
            [
                Header(CLK_REPO, "clockwork/cog/include_common.hh"),
                SystemHeader("chrono"),
                SystemHeader("string_view"),
            ]
        )
        if self.cog_ir.metrics_options.metrics_enabled:
            cpp_mod.header_chunk.context.add_include(FwdDecl(CLOCKWORK_NAMESPACE, "template <class> struct Tap"))
        cpp_mod.implementation_chunk.context.add_includes([Header(CLK_REPO, "clockwork/cog/include_common.hh")])

        if self.header_name:
            cpp_mod.implementation_chunk.context.add_includes(
                [Header(self.cog_ir.module.module_id.repo, self.header_name)]
            )

        cpp_mod.header_chunk.context.add_include(self.dial_header)
        cpp_mod.implementation_chunk.context.add_include(self.dial_header)

        if self.cog_ir.is_generic():
            cpp_mod.implementation_chunk.context.add_include(
                Header(
                    self.dial_header.repo,
                    self.dial_header.path.with_name(self.dial_header.path.name.replace("_dial.hh", "_impl.hh")),
                )
            )
            self.template_params = ", ".join(
                param.render(self.cpp_namespace)
                for param in get_template_params(self.cog_ir.module.context, self.cog_ir.parameters)
            )
            self.template_args = (
                f"<{', '.join(arg.render(self.cpp_namespace) for arg in get_template_args(self.cog_ir.parameters))}>"
            )
            dial_type = CppType([], self.dial_name + self.template_args, self.cpp_namespace)

            self.cog_policy_name = self.class_name + "PolicyBase" + self.template_args
            cpp_mod.header_chunk.append(
                [
                    f"template <{self.template_params}>",
                    f"struct {self.class_name}Policy",
                    "{};",
                    f"template <{self.template_params}>",
                    f"struct {self.class_name}PolicyBase",
                    "{",
                ]
            )
        else:
            dial_type = CppType([], self.dial_name, self.cpp_namespace)

            self.cog_policy_name = self.class_name + "Policy"
            cpp_mod.header_chunk.append(
                [
                    f"struct {self.cog_policy_name}",
                    "{",
                ]
            )

        cog_fqn: Final = self.cog_ir.fqn

        if not self.cog_ir.is_generic():
            cpp_mod.header_chunk.append(
                gen_const_str([cog_fqn, self.cog_policy_name]),
                indent=1,
            )

        sim_execution_duration_const = "simulated_execution_duration"
        if sim_options := self.cog_ir.simulation_options:
            cpp_mod.header_chunk.append(
                _gen_const_milliseconds(sim_options.execution_duration, sim_execution_duration_const), indent=1
            )
        else:
            cpp_mod.header_chunk.append(
                _gen_const_milliseconds(
                    primitive.UnitValue.make(Decimal(1), units.MILLISECONDS), sim_execution_duration_const
                ),
                indent=1,
            )

        cpp_mod.header_chunk.append(
            _gen_const_size(self.cog_ir.metrics_options.batch_size, "event_metrics_batch_size"), indent=1
        )

        cpp_mod.header_chunk.append(
            f"using CogDial = {dial_type.render(self.cpp_namespace)};",
            indent=1,
        )

        self.signals_struct = SignalsStruct.from_ir(
            self.cog_ir.module.context, self.cog_ir.report_groups | self.cog_ir.cog_metrics_report_groups
        )
        has_batched_signals = any(signal.is_batched for signal in self.signals_struct.signals.values())
        cpp_mod.header_chunk.append(
            f"static constexpr auto has_signals = {'true' if has_batched_signals else 'false'};",
            indent=1,
        )

        has_cog_metrics_rgs = any(_is_cog_metrics_group(name) for name in self.cog_ir.cog_metrics_report_groups)
        cpp_mod.header_chunk.append(
            f"static constexpr auto has_cog_metrics_report_groups = {'true' if has_cog_metrics_rgs else 'false'};",
            indent=1,
        )

        cpp_mod.header_chunk.append(
            f"using SignalApiType = {self.dial_name}SignalApi;",
            indent=1,
        )

        if not self.cog_ir.is_generic():
            cpp_mod.header_chunk.append(
                f"static constexpr auto cog_id = {UuidHandler(COG_CLASS_UUID_TYPE, uuid_reg.lookup_uuid(self.cog_ir.module.context, self.cog_ir)).render_from_string_func()};",
                indent=1,
            )

        self.resources = Resources.make(
            ResourcesStruct.from_ir(self.cog_ir.module.context, self.cog_ir.resources), cog_fqn
        )
        cpp_mod.header_chunk.append(self.resources.render_resources(), indent=1)
        self.configs = Configs.make(ConfigsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.configs), cog_fqn)
        cpp_mod.header_chunk.append(self.configs.render_configs(self.cpp_namespace), indent=1)
        self.states = States.make(StatesStruct.from_ir(self.cog_ir.module.context, self.cog_ir.states), cog_fqn)
        cpp_mod.header_chunk.append(self.states.render_states(self.cpp_namespace), indent=1)
        self.timers = Timers.make(ConditionsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.conditions), cog_fqn)
        cpp_mod.header_chunk.append(self.timers.render_timers(), indent=1)

        self.inputs = Inputs.make(
            InputsStruct.from_ir(
                self.cog_ir.module.context,
                self.cog_ir.inputs,
                self.cog_ir.execution_spec,
                aligned_input_defs=self.cog_ir.aligned_inputs,
                expanded_aligned_input_defs=self.cog_ir.expanded_aligned_input_defs,
            ),
            cog_fqn,
        )
        cpp_mod.header_chunk.append(self.inputs.render_inputs(), indent=1)

        self.input_conditions = InputConditions.make(
            ConditionsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.conditions),
            cog_fqn,
            self.inputs.input_uuid_map,
        )
        cpp_mod.header_chunk.append(self.input_conditions.render_input_conditions(), indent=1)

        output_dict = (
            self.cog_ir.outputs
            | self.cog_ir.metrics_outputs
            | self.cog_ir.report_groups
            | self.cog_ir.cog_metrics_report_groups
        )
        self.publishers = Publishers.make(
            OutputsStruct.from_ir(self.cog_ir.module.context, output_dict, self.cog_ir.rate_limits),
            cog_fqn,
        )
        cpp_mod.header_chunk.append(self.publishers.render_publishers(), indent=1)

        self.diagnostics = Diagnosticses.make(
            DiagnosticsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.diagnostics), cog_fqn
        )

        assert self.cog_ir.infra_diagnostics is not None
        self.infra_diags = InfraDiagnostics.make(
            Diagnostics.from_cog_infra(
                self.cog_ir.module.context,
                self.cog_ir.infra_diagnostics,
            ),
            self.cog_ir.infra_diagnostics,
            cog_fqn,
            self.class_name,
            self.dial_header,
            is_template=self.cog_ir.is_generic(),
        )

        cpp_mod.header_chunk.append(self.diagnostics.render_diagnostics(), indent=1)
        cpp_mod.header_chunk.append(self.infra_diags.render_diagnostics(self.cpp_namespace), indent=1)

        cpp_mod.append(self._generate_is_ready_method())
        cpp_mod.append(self._generate_make_dial_method())
        if self.inputs and self.inputs.aligned_groups:
            cpp_mod.append(self._generate_resolve_alignment_method())
            cpp_mod.append(self._generate_commit_alignment_method())
        cpp_mod.append(self._generate_execute_method())
        if self.cog_ir.metrics_options.metrics_enabled:
            self._append_metrics_methods(cpp_mod)
        cpp_mod.append(self._generate_publish_report_groups_method())
        cpp_mod.append(self._generate_signal_infra_methods())
        cpp_mod.header_chunk.append("};")

        if self.cog_ir.is_generic():
            cpp_mod.header_chunk.append(
                [
                    f"template <{self.template_params}>",
                    f"using {self.class_name} = ::{CLOCKWORK_NAMESPACE}::SimpleCog<{self.class_name + 'Policy'}{self.template_args}>;",
                    f"template <{self.template_params}>",
                    f"struct {self.class_name}Factory",
                    "{};",
                ]
            )
        else:
            cpp_mod.header_chunk.append(
                f"using {self.class_name} = ::{CLOCKWORK_NAMESPACE}::SimpleCog<{self.cog_policy_name}>;"
            )

            cpp_mod.append(
                _gen_factory(
                    UuidHandler(COG_CLASS_UUID_TYPE, uuid_reg.lookup_uuid(self.cog_ir.module.context, self.cog_ir)),
                    self.class_name,
                    "CogFactory",
                    f"::jewels::memory::MemoryResource resource, const ::jewels::Uuid<::{CLOCKWORK_NAMESPACE}::common::CogInstanceId>& instance_id, ::jewels::memory::ObjectPtr<::{CLOCKWORK_NAMESPACE}::AbstractCogQueue> queue",
                    f"return ::jewels::memory::make_pmr_shared<::{CLOCKWORK_NAMESPACE}::SimpleCog<{self.class_name}Policy>>(resource, resource, instance_id, queue);",
                )
            )
            cpp_mod.append(
                self.states.render_factories(self.cog_ir.module.context, self.cog_policy_name, self.cpp_namespace)
            )

        return cpp_mod

    def _append_metrics_methods(self, cpp_mod: CppModuleChunks) -> None:
        """Append all metrics-related methods to the module."""
        cpp_mod.append(self._generate_populate_input_event_metrics_method())
        cpp_mod.append(self._generate_populate_output_event_metrics_method())
        cpp_mod.append(self._generate_populate_trigger_mask_method())
        cpp_mod.append(self._generate_populate_condition_trigger_vals_method())
        cpp_mod.append(self._generate_populate_input_telemetry_metrics_method())
        cpp_mod.append(self._generate_populate_output_telemetry_metrics_method())
        cpp_mod.append(self._generate_get_conditions_mask_method(self.cog_ir.module.context))
        cpp_mod.append(self._generate_populate_telemetry_triggers_method())
        cpp_mod.append(self._generate_populate_telemetry_metrics_method())
        cpp_mod.append(self._generate_populate_event_metrics_method())

    def _generate_populate_telemetry_triggers_method(self) -> CppModuleChunks:
        """Generate populate_telemetry_triggers method to count condition triggers from telemetry metrics."""
        # Get all condition names
        condition_names = []
        if self.timers:
            condition_names.extend(self.timers.cond_name_to_timer.keys())
        if self.input_conditions:
            condition_names.extend(self.input_conditions.input_conditions_registry.keys())

        arg_telemetry_metrics = CppNamedType(
            CppType([], "::clockwork::TelemetryMetrics", None, const=True, ref=Ref.L),
            "telemetry_metrics" if condition_names else "/*telemetry_metrics*/",
        )

        arg_tachyon = CppNamedType(
            CppType(
                [],
                "::clockwork::Tap<::clockwork::Tachyon<" + self.cog_ir.name + "TelemetryMetrics>>",
                None,
                const=False,
                ref=Ref.L,
            ),
            "telemetry_metrics_tachyon" if condition_names else "/*telemetry_metrics_tachyon*/",
        )

        body = CppChunk()

        if condition_names:
            # Create counters for each condition
            for condition_name in condition_names:
                body.append(f"uint16_t {condition_name}_count = 0;")

            # Count condition triggers from the conditions mask vector
            body.append(
                f"for (const auto& condition_mask : {arg_telemetry_metrics.argument_name}.conditions_mask_vector)"
            )
            body.append("{")
            body.append(
                indent=1, chunk=f"auto trigger_flags = static_cast<{self.cog_ir.name}ConditionsMask>(condition_mask);"
            )

            # For each condition, check if it was triggered in this mask and increment its counter
            for condition_name in condition_names:
                body.append(
                    indent=1,
                    chunk=f"if ((trigger_flags & {self.cog_ir.name}ConditionsMask::{condition_name}) != {self.cog_ir.name}ConditionsMask::no_conditions_active)",
                )
                body.append(indent=1, chunk="{")
                body.append(indent=2, chunk=f"++{condition_name}_count;")
                body.append(indent=1, chunk="}")

            body.append("}")

            # Set the counted values in the telemetry metrics tachyon
            for condition_name in condition_names:
                body.append(f"{arg_tachyon.argument_name}.set_{condition_name}_trigger_vals({condition_name}_count);")

        populate_telemetry_triggers_method = CppMethod(
            name="populate_telemetry_triggers",
            doc=None,
            return_type=CppType([], "void", None),
            arguments=[arg_telemetry_metrics, arg_tachyon],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=False,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace
        cpp_mod = populate_telemetry_triggers_method.render(
            parent_class=parent_type, enclosing_namespace=enclosing_namespace
        )
        return _insert_template_params(self.template_params, cpp_mod)

    def _generate_populate_condition_trigger_vals_method(self) -> CppModuleChunks:
        """Generate populate_condition_trigger_vals method to count condition triggers."""
        # Get all condition names
        condition_names = []
        if self.timers:
            condition_names.extend(self.timers.cond_name_to_timer.keys())
        if self.input_conditions:
            condition_names.extend(self.input_conditions.input_conditions_registry.keys())
        arg_event_metrics = CppNamedType(
            CppType([], "std::pmr::vector<uint64_t>", None, const=True, ref=Ref.L),
            "event_metrics_vec" if condition_names else "/*event_metrics_vec*/",
        )

        arg_tachyon = CppNamedType(
            CppType(
                [],
                "::clockwork::Tap<::clockwork::Tachyon<" + self.cog_ir.name + "TelemetryMetrics>>",
                None,
                const=False,
                ref=Ref.L,
            ),
            "telemetry_metrics_tachyon" if condition_names else "/*telemetry_metrics_tachyon*/",
        )

        body = CppChunk()

        if condition_names:
            # Create counters for each condition
            for condition_name in condition_names:
                body.append(f"uint16_t {condition_name}_count = 0;")

            # Count condition triggers from each condition mask value
            body.append(f"for (const auto& condition_mask : {arg_event_metrics.argument_name})")
            body.append("{")
            body.append(
                indent=1, chunk=f"auto trigger_flags = static_cast<{self.cog_ir.name}ConditionsMask>(condition_mask);"
            )

            # For each condition, check if it was triggered in this mask and increment its counter
            for condition_name in condition_names:
                body.append(
                    indent=1,
                    chunk=f"if ((trigger_flags & {self.cog_ir.name}ConditionsMask::{condition_name}) != {self.cog_ir.name}ConditionsMask::no_conditions_active)",
                )
                body.append(indent=1, chunk="{")
                body.append(indent=2, chunk=f"++{condition_name}_count;")
                body.append(indent=1, chunk="}")

            body.append("}")

            # Set the counted values in the telemetry metrics
            for condition_name in condition_names:
                body.append(f"{arg_tachyon.argument_name}.set_{condition_name}_trigger_vals({condition_name}_count);")

        populate_condition_trigger_vals_method = CppMethod(
            name="populate_condition_trigger_vals",
            doc=None,
            return_type=CppType([], "void", None),
            arguments=[arg_event_metrics, arg_tachyon],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=False,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace
        cpp_mod = populate_condition_trigger_vals_method.render(
            parent_class=parent_type, enclosing_namespace=enclosing_namespace
        )
        return _insert_template_params(self.template_params, cpp_mod)

    def _generate_is_ready_method(self) -> CppModuleChunks:
        execute_when = ExecuteExprHandler(self.cog_ir.execution_spec.condition)

        arg_stats = CppNamedType(
            CppType(
                [Header(CLK_REPO, "clockwork/cog/cog_statistics.hh")],
                "CogStatistics",
                "clockwork",
                False,
                Ref.L,
            ),
            "statistics" if execute_when.init_condition_present() else "/*statistics*/",
            [],
        )
        arg_timers = CppNamedType(
            CppType(
                [Header(CLK_REPO, "clockwork/cog/cog_timers.hh")],
                "TimersType::ConditionsTuple",
                None,
                False,
                Ref.L,
            ),
            "timers" if execute_when.time_condition_present() else "/*timers*/",
            ["typename"],
        )
        arg_input_conditions = CppNamedType(
            CppType(
                [Header(CLK_REPO, "clockwork/cog/input_conditions.hh")],
                "ConditionsType::ConditionsTuple",
                None,
                False,
                Ref.L,
            ),
            "conditions" if execute_when.message_condition_present() else "/*conditions*/",
            ["typename"],
        )

        # Customize condition combinatorial logic here
        body = CppChunk()
        cond_name_to_timer = self.timers.cond_name_to_timer if self.timers else {}
        cond_name_to_input_condition = self.input_conditions.input_conditions_registry if self.input_conditions else {}
        conditional_expr = execute_when.render(
            cond_name_to_timer,
            arg_timers.argument_name,
            cond_name_to_input_condition,
            arg_input_conditions.argument_name,
        )
        body.append(f"return {conditional_expr};")
        body.context.add_include(SystemHeader("tuple"))

        return_type = CppType([], "bool", None)

        is_ready_method = CppMethod(
            name="is_ready",
            doc=None,
            return_type=return_type,
            arguments=[arg_stats, arg_timers, arg_input_conditions],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=False,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        cpp_mod = is_ready_method.render(parent_class=parent_type, enclosing_namespace=self.cpp_namespace)
        return _insert_template_params(self.template_params, cpp_mod)

    def _generate_make_dial_method(self) -> CppModuleChunks:  # noqa: PLR0915, PLR0912, C901 naturally large with no easy breaks
        arg_params = CppNamedType(
            CppType(
                [Header(CLK_REPO, "clockwork/cog/simple_cog.hh")],
                "CogExecuteParams",
                CLOCKWORK_NAMESPACE,
                True,
                Ref.L,
            ),
            "params",
        )

        arg_resources = CppNamedType(
            CppType(
                [Header(CLK_REPO, "clockwork/cog/cog_memory_resources.hh")],
                "MemoryResourcesType::MemoryResourcesTuple&",
                None,
            ),
            "resources" if self.resources and len(self.resources) else "/*resources*/",
            ["typename"],
        )

        arg_configs = CppNamedType(
            CppType(
                [Header(CLK_REPO, "clockwork/cog/cog_configs.hh")],
                "ConfigsType::ConfigsTuple&",
                None,
            ),
            "configs" if self.configs and len(self.configs) else "/*configs*/",
            ["typename"],
        )

        arg_states = CppNamedType(
            CppType(
                [Header(CLK_REPO, "clockwork/cog/cog_states.hh")],
                "StatesType::StatesTuple&",
                None,
            ),
            "states" if self.states and len(self.states) else "/*states*/",
            ["typename"],
        )

        arg_inputs = CppNamedType(
            CppType(
                [Header(CLK_REPO, "clockwork/cog/cog_inputs.hh")],
                "InputsType::InputDialTuple&",
                None,
            ),
            "inputs" if self.inputs and len(self.inputs) else "/*inputs*/",
            ["typename"],
        )

        arg_publishables = CppNamedType(
            CppType(
                [Header(CLK_REPO, "clockwork/cog/cog_publishers.hh")],
                "PublishersType::PublishablesTuple&",
                None,
            ),
            "publishables"
            if self.publishers
            and any(
                publisher.metrics_log_type == cog.MetricsLogType.none and not publisher.is_report_group
                for publisher in self.publishers
            )
            else "/*publishables*/",
            ["typename"],
        )

        arg_timer_conditions = CppNamedType(
            CppType(
                [Header(CLK_REPO, "clockwork/cog/cog_timers.hh")],
                "TimersType::ConditionsTuple",
                None,
                False,
                Ref.L,
            ),
            "timer_conditions" if self.timers and len(self.timers) else "/*timer_conditions*/",
            ["typename"],
        )

        arg_input_conditions = CppNamedType(
            CppType(
                [Header(CLK_REPO, "clockwork/cog/cog_conditions.hh")],
                "ConditionsType::ConditionsTuple",
                None,
                False,
                Ref.L,
            ),
            "message_conditions" if self.input_conditions and len(self.input_conditions) else "/*message_conditions*/",
            ["typename"],
        )

        arg_diagnostics = CppNamedType(
            CppType(
                [Header(CLK_REPO, "clockwork/cog/cog_diagnostics.hh")],
                "DiagnosticsType::ReporterType&",
                None,
            ),
            "diagnostics" if self.diagnostics else "/*diagnostics*/",
            ["typename"],
        )

        arg_signals = CppNamedType(
            CppType(
                [],
                "SignalApiType&",
                None,
            ),
            "signals",
        )

        def make_obj_ptr(statement: str) -> str:
            return f"::jewels::memory::make_non_null_from_ref({statement})"

        has_dynamic_timer = self.timers is not None and self.timers.dynamic_timer_handler is not None
        arg_dynamic_timer: CppNamedType | None = None
        if has_dynamic_timer:
            assert self.timers is not None  # pyright type narrowing
            assert self.timers.dynamic_timer_handler is not None  # pyright type narrowing
            policy_name = self.timers.dynamic_timer_handler.policy_name
            handler_cpp_type = f"DynamicTimerHandler<{policy_name}>"
            arg_dynamic_timer = CppNamedType(
                CppType(
                    [Header(CLK_REPO, "clockwork/cog/dynamic_timer_handler.hh")],
                    handler_cpp_type,
                    CLOCKWORK_NAMESPACE,
                    False,
                    Ref.L,
                ),
                "dynamic_timer_handler",
            )

        return_type = CppType([], self.dial_name + self.template_args, self.cpp_namespace)

        body = CppChunk()

        # For aligned inputs: declare local view variables before the return statement.
        # These views reference the narrowed InputView buffers after resolve_alignment().
        if self.inputs:
            for group in self.inputs.aligned_groups:
                for h in group.upstream_handlers:
                    get_expr = h.render_get(arg_inputs.argument_name)
                    body.append(f"auto& {h.input_name}_resolved = {get_expr};")

        body.append(f"return {return_type.type_name}(")
        # start_time
        start_time_inner_chunk = CppChunk()
        start_time_inner_chunk.append(f"{arg_params.argument_name}.start_time,")
        body.append(start_time_inner_chunk, indent=1)
        # resources
        resources_inner_chunk = CppChunk()
        resources_inner_chunk.append(f"{self.dial_name}Resources{self.template_args}(")
        if self.resources:
            resources_inner_chunk.append(
                [
                    f"{make_obj_ptr(resource.render_get(arg_resources.argument_name))}{',' if (idx < len(self.resources) - 1) else ''}"
                    for idx, resource in enumerate(self.resources)
                ],
                indent=1,
            )
        resources_inner_chunk.append("),")
        body.append(resources_inner_chunk, indent=1)
        # configs
        configs_inner_chunk = CppChunk()
        configs_inner_chunk.append(f"{self.dial_name}Configs{self.template_args}(")
        if self.configs:
            configs_inner_chunk.append(
                [
                    f"{make_obj_ptr(config.render_get(arg_configs.argument_name))}{',' if (idx < len(self.configs) - 1) else ''}"
                    for idx, config in enumerate(self.configs)
                ],
                indent=1,
            )
        configs_inner_chunk.append("),")
        body.append(configs_inner_chunk, indent=1)
        # states
        states_inner_chunk = CppChunk()
        states_inner_chunk.append(f"{self.dial_name}States{self.template_args}(")
        if self.states:
            states_inner_chunk.append(
                [
                    f"{state.render_get(arg_states.argument_name)}{',' if (idx < len(self.states) - 1) else ''}"
                    for idx, state in enumerate(self.states)
                ],
                indent=1,
            )
        states_inner_chunk.append("),")
        body.append(states_inner_chunk, indent=1)
        # conditions
        # Rendering conditions is more involved. As we intantiate the Dial, the condition members need to be specified
        # in the order they're declared due to field designator requirements.  The current solution is to
        # iterate through the conditions IR and search for the corresponding TimerHandle or InputConditionHandle
        # then call the corresponding rendering function.
        conditions_inner_chunk = CppChunk()
        conditions_inner_chunk.append(f"{self.dial_name}Conditions{self.template_args}(")
        cond_struct = ConditionsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.conditions)
        for idx, cond_name in enumerate(cond_struct.conditions):
            if self.timers and cond_name in self.timers.cond_name_to_timer:
                timer = self.timers.cond_name_to_timer[cond_name]
                conditions_inner_chunk.append(
                    f"{make_obj_ptr(timer.render_get(arg_timer_conditions.argument_name))}{',' if (idx < len(cond_struct.conditions) - 1) else ''}",
                    indent=1,
                )
            elif self.input_conditions and cond_name in self.input_conditions.input_conditions_registry:
                input_condition = self.input_conditions.input_conditions_registry[cond_name]
                conditions_inner_chunk.append(
                    f"{make_obj_ptr(input_condition.render_get(arg_input_conditions.argument_name))}{',' if (idx < len(cond_struct.conditions) - 1) else ''}",
                    indent=1,
                )
            else:
                err = f"Condition {cond_name} is not supported"
                raise RuntimeError(err)
        conditions_inner_chunk.append("),")
        body.append(conditions_inner_chunk, indent=1)
        # inputs
        inputs_inner_chunk = CppChunk()
        inputs_inner_chunk.append(f"{self.dial_name}Inputs{self.template_args}(")
        if self.inputs:
            dial_entry_count = len(self.inputs.regular_handlers) + len(self.inputs.aligned_groups)
            dial_entry_idx = 0

            for ipt in self.inputs.regular_handlers:
                comma = "," if dial_entry_idx < dial_entry_count - 1 else ""
                inputs_inner_chunk.append(
                    f"{make_obj_ptr(ipt.render_get(arg_inputs.argument_name))}{comma}",
                    indent=1,
                )
                dial_entry_idx += 1

            for group in self.inputs.aligned_groups:
                comma = "," if dial_entry_idx < dial_entry_count - 1 else ""
                aligned_struct_name = f"{self.dial_name}{to_camel(group.group_name)}Inputs"
                assert group.upstream_handlers, f"Aligned group '{group.group_name}' has no upstream inputs"
                # Construct user-facing dials from the narrowed internal tuple entries.
                # The view variables were declared before the return statement.
                upstream_args = ", ".join(
                    f"::std::remove_cvref_t<decltype(::std::declval<{aligned_struct_name}>().get_{h.input_name}())>"
                    + f"({h.input_name}_resolved.get_view(), {h.input_name}_resolved.get_cursor(), {h.input_name}_resolved.get_first_new())"
                    for h in group.upstream_handlers
                )
                inputs_inner_chunk.append(
                    f"{aligned_struct_name}({upstream_args}){comma}",
                    indent=1,
                )
                dial_entry_idx += 1

        inputs_inner_chunk.append("),")
        body.append(inputs_inner_chunk, indent=1)
        # outputs
        outputs_inner_chunk = CppChunk()
        outputs_inner_chunk.append(f"{self.dial_name}Outputs{self.template_args}(")
        if self.publishers:
            non_metrics_publishers = [
                publisher
                for publisher in self.publishers
                if publisher.metrics_log_type == cog.MetricsLogType.none and not publisher.is_report_group
            ]
            if non_metrics_publishers:
                outputs_inner_chunk.append(
                    [
                        f"{make_obj_ptr(publisher.render_get(arg_publishables.argument_name))}{',' if (idx < len(non_metrics_publishers) - 1) else ''}"
                        for idx, publisher in enumerate(non_metrics_publishers)
                    ],
                    indent=1,
                )
        outputs_inner_chunk.append("),")
        body.append(outputs_inner_chunk, indent=1)
        # diagnostics
        diagnostics_inner_chunk = CppChunk()
        if self.diagnostics and len(self.diagnostics) == 1:
            diagnostics_inner_chunk.append(make_obj_ptr(arg_diagnostics.argument_name))
        else:
            diagnostics_inner_chunk.append(f"{self.dial_name}Diagnostics{self.template_args}(")
            if self.diagnostics:
                diagnostics_inner_chunk.append(
                    [
                        f"{make_obj_ptr(diagnostics.render_get(arg_diagnostics.argument_name))}{',' if (idx < len(self.diagnostics) - 1) else ''}"
                        for idx, diagnostics in enumerate(self.diagnostics)
                    ],
                    indent=1,
                )
            diagnostics_inner_chunk.append(")")
        diagnostics_inner_chunk.append(",")
        body.append(diagnostics_inner_chunk, indent=1)

        # signals
        signals_inner_chunk = CppChunk()
        if has_dynamic_timer:
            signals_inner_chunk.append(f"{arg_signals.argument_name},")
        else:
            signals_inner_chunk.append(f"{arg_signals.argument_name}")
        body.append(signals_inner_chunk, indent=1)

        if has_dynamic_timer:
            assert arg_dynamic_timer is not None  # pyright type narrowing
            timer_control_chunk = CppChunk()
            timer_control_chunk.context.add_includes([Header(CLK_REPO, "clockwork/aligner/timer_control.hh")])
            # Construct AlignerTimerControl from the handler reference
            timer_control_chunk.append(f"::{CLOCKWORK_NAMESPACE}::aligner::AlignerTimerControl(")
            timer_control_chunk.append(
                f"  {arg_dynamic_timer.argument_name})",
                indent=1,
            )
            body.append(timer_control_chunk, indent=1)

        body.append(");")
        body.context.add_includes(
            [
                SystemHeader("utility"),
                SystemHeader("tuple"),
            ]
        )
        if self.inputs and self.inputs.aligned_groups:
            body.context.add_include(SystemHeader("type_traits"))

        make_dial_arguments = [
            arg_params,
            arg_resources,
            arg_configs,
            arg_states,
            arg_inputs,
            arg_publishables,
            arg_timer_conditions,
            arg_input_conditions,
            arg_diagnostics,
            arg_signals,
        ]
        if has_dynamic_timer:
            assert arg_dynamic_timer is not None
            make_dial_arguments.append(arg_dynamic_timer)

        make_dial_method = CppMethod(
            name="make_dial",
            doc=None,
            return_type=return_type,
            arguments=make_dial_arguments,
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=True,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace if self.cpp_namespace is not None else ""  # pyright: ignore[reportUnnecessaryComparison] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

        cpp_mod = make_dial_method.render(parent_class=parent_type, enclosing_namespace=enclosing_namespace)
        return _insert_template_params(self.template_params, cpp_mod)

    def _generate_resolve_alignment_method(self) -> CppModuleChunks:
        """Generate resolve_alignment static method for aligned cogs.

        This method reads the alignment message's per-input seqno fields,
        narrows the upstream InputView buffers to the resolved messages,
        and replaces the InputDialTuple entries. Handles batch, optional,
        and reuse inputs.
        """
        assert self.inputs is not None
        assert self.inputs.aligned_groups

        arg_cog_inputs = CppNamedType(
            CppType([], "InputsType", None, const=False, ref=Ref.L),
            "cog_inputs",
        )
        arg_inputs = CppNamedType(
            CppType([], "typename InputsType::InputDialTuple", None, const=False, ref=Ref.L),
            "inputs",
        )
        stale_alignment_index_type = _OUT_TEMPLATE.instantiate(
            [CppType([SystemHeader("cstddef")], "size_t", None)],
        )
        arg_stale_alignment_index = CppNamedType(
            stale_alignment_index_type,
            _STALE_ALIGNMENT_INDEX_OUT,
        )

        body = CppChunk()
        body.append("bool any_pending = false;")

        for group in self.inputs.aligned_groups:
            alignment_handler = group.alignment_handler
            alignment_candidates_name = f"alignment_{group.group_name}_candidates"
            body.append(
                f"const auto {alignment_candidates_name} = {alignment_handler.render_get('inputs')}.get_cursor_view();",
            )

            body.append(f"if ({alignment_candidates_name}.empty())")
            body.append("{")
            if alignment_handler.cog_input.min_msgs > 0:
                body.append("any_pending = true;", indent=1)
            else:
                for upstream in group.upstream_handlers:
                    body.append(
                        f"{upstream.render_get('inputs')} = cog_inputs.template prepare_empty_aligned_input<{upstream.index}>();",
                        indent=1,
                    )
            body.append("}")
            body.append("else")
            body.append("{")
            body.append(
                f"const auto& alignment_{group.group_name} = {alignment_candidates_name}.back();",
                indent=1,
            )

            resolved_body = CppChunk()

            for upstream in group.upstream_handlers:
                resolved_input = group.resolved_inputs[upstream.input_name]
                is_batch = resolved_input.batch_size is not None
                is_optional = resolved_input.optional

                if is_optional and is_batch:
                    self._gen_optional_batch_resolve(
                        resolved_body,
                        arg_cog_inputs,
                        group.group_name,
                        alignment_handler.index,
                        upstream,
                    )
                elif is_optional:
                    self._gen_optional_resolve(
                        resolved_body,
                        arg_cog_inputs,
                        group.group_name,
                        alignment_handler.index,
                        upstream,
                    )
                elif is_batch:
                    self._gen_batch_resolve(
                        resolved_body,
                        arg_cog_inputs,
                        group.group_name,
                        alignment_handler.index,
                        upstream,
                    )
                else:
                    self._gen_simple_resolve(
                        resolved_body,
                        arg_cog_inputs,
                        group.group_name,
                        alignment_handler.index,
                        upstream,
                    )

            body.append(resolved_body, indent=1)
            body.append("}")

        body.append("if (any_pending)")
        body.append("{")
        body.append(
            f"return ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::pending;",
            indent=1,
        )
        body.append("}")
        body.append(f"return ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::resolved;")

        return_type = CppType([], "AlignedLookupOutcome", CLOCKWORK_NAMESPACE)

        resolve_method = CppMethod(
            name="resolve_alignment",
            doc=(
                "Resolve aligned inputs.\n"
                "@post stale_alignment_index_out is written if and only if the returned result is stale."
            ),
            return_type=return_type,
            arguments=[arg_stale_alignment_index, arg_cog_inputs, arg_inputs],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=True,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        cpp_mod = resolve_method.render(parent_class=parent_type, enclosing_namespace=self.cpp_namespace)
        return _insert_template_params(self.template_params, cpp_mod)

    @staticmethod
    def _gen_simple_resolve(
        body: CppChunk,
        arg_cog_inputs: CppNamedType,
        group_name: str,
        alignment_index: int,
        upstream: InputHandler,
    ) -> None:
        """Generate resolve code for a non-batch, non-optional input."""
        seqno_expr = f"alignment_{group_name}.get_{upstream.input_name}_seq()"
        inner = CppChunk()
        inner.append(
            f"const auto result = {arg_cog_inputs.argument_name}.template prepare_aligned_input<{upstream.index}>("
            + f"::jewels::Out{{{upstream.render_get('inputs')}}}, {seqno_expr});",
        )
        inner.append("switch (result.get())")
        inner.append("{")
        inner.append(f"case ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::resolved:", indent=1)
        inner.append("break;", indent=2)
        inner.append(f"case ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::stale:", indent=1)
        inner.append(
            f"*{_STALE_ALIGNMENT_INDEX_OUT} = {alignment_index};",
            indent=2,
        )
        inner.append(
            f"return ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::stale;",
            indent=2,
        )
        inner.append(f"case ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::pending:", indent=1)
        inner.append("any_pending = true;", indent=2)
        inner.append("break;", indent=2)
        inner.append("}")
        body.append("{")
        body.append(inner, indent=1)
        body.append("}")

    @staticmethod
    def _gen_batch_resolve(
        body: CppChunk,
        arg_cog_inputs: CppNamedType,
        group_name: str,
        alignment_index: int,
        upstream: InputHandler,
    ) -> None:
        """Generate resolve code for a batch, non-optional input."""
        begin_expr = f"alignment_{group_name}.get_{upstream.input_name}_begin_seq()"
        end_expr = f"alignment_{group_name}.get_{upstream.input_name}_end_seq()"
        inner = CppChunk()
        inner.append(
            f"const auto result = {arg_cog_inputs.argument_name}.template prepare_aligned_input_range<{upstream.index}>("
            + f"::jewels::Out{{{upstream.render_get('inputs')}}}, {begin_expr}, {end_expr});",
        )
        inner.append("switch (result.get())")
        inner.append("{")
        inner.append(f"case ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::resolved:", indent=1)
        inner.append("break;", indent=2)
        inner.append(f"case ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::stale:", indent=1)
        inner.append(
            f"*{_STALE_ALIGNMENT_INDEX_OUT} = {alignment_index};",
            indent=2,
        )
        inner.append(
            f"return ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::stale;",
            indent=2,
        )
        inner.append(f"case ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::pending:", indent=1)
        inner.append("any_pending = true;", indent=2)
        inner.append("break;", indent=2)
        inner.append("}")
        body.append("{")
        body.append(inner, indent=1)
        body.append("}")

    @staticmethod
    def _gen_optional_resolve(
        body: CppChunk,
        arg_cog_inputs: CppNamedType,
        group_name: str,
        alignment_index: int,
        upstream: InputHandler,
    ) -> None:
        """Generate resolve code for a non-batch, optional input."""
        has_expr = f"alignment_{group_name}.get_has_{upstream.input_name}()"
        seqno_expr = f"alignment_{group_name}.get_{upstream.input_name}_seq()"
        body.append(f"if ({has_expr})")
        body.append("{")
        inner = CppChunk()
        inner.append(
            f"const auto result = {arg_cog_inputs.argument_name}.template prepare_aligned_input<{upstream.index}>("
            + f"::jewels::Out{{{upstream.render_get('inputs')}}}, {seqno_expr});",
        )
        inner.append("switch (result.get())")
        inner.append("{")
        inner.append(f"case ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::resolved:", indent=1)
        inner.append("break;", indent=2)
        inner.append(f"case ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::stale:", indent=1)
        inner.append(
            f"*{_STALE_ALIGNMENT_INDEX_OUT} = {alignment_index};",
            indent=2,
        )
        inner.append(
            f"return ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::stale;",
            indent=2,
        )
        inner.append(f"case ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::pending:", indent=1)
        inner.append("any_pending = true;", indent=2)
        inner.append("break;", indent=2)
        inner.append("}")
        body.append(inner, indent=1)
        body.append("}")
        body.append("else")
        body.append("{")
        body.append(
            f"{upstream.render_get('inputs')} = {arg_cog_inputs.argument_name}.template prepare_empty_aligned_input<{upstream.index}>();",
            indent=1,
        )
        body.append("}")

    @staticmethod
    def _gen_optional_batch_resolve(
        body: CppChunk,
        arg_cog_inputs: CppNamedType,
        group_name: str,
        alignment_index: int,
        upstream: InputHandler,
    ) -> None:
        """Generate resolve code for a batch + optional input."""
        has_expr = f"alignment_{group_name}.get_has_{upstream.input_name}()"
        begin_expr = f"alignment_{group_name}.get_{upstream.input_name}_begin_seq()"
        end_expr = f"alignment_{group_name}.get_{upstream.input_name}_end_seq()"
        body.append(f"if ({has_expr})")
        body.append("{")
        inner = CppChunk()
        inner.append(
            f"const auto result = {arg_cog_inputs.argument_name}.template prepare_aligned_input_range<{upstream.index}>("
            + f"::jewels::Out{{{upstream.render_get('inputs')}}}, {begin_expr}, {end_expr});",
        )
        inner.append("switch (result.get())")
        inner.append("{")
        inner.append(f"case ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::resolved:", indent=1)
        inner.append("break;", indent=2)
        inner.append(f"case ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::stale:", indent=1)
        inner.append(
            f"*{_STALE_ALIGNMENT_INDEX_OUT} = {alignment_index};",
            indent=2,
        )
        inner.append(
            f"return ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::stale;",
            indent=2,
        )
        inner.append(f"case ::{CLOCKWORK_NAMESPACE}::AlignedLookupResult::pending:", indent=1)
        inner.append("any_pending = true;", indent=2)
        inner.append("break;", indent=2)
        inner.append("}")
        body.append(inner, indent=1)
        body.append("}")
        body.append("else")
        body.append("{")
        body.append(
            f"{upstream.render_get('inputs')} = {arg_cog_inputs.argument_name}.template prepare_empty_aligned_input<{upstream.index}>();",
            indent=1,
        )
        body.append("}")

    def _generate_commit_alignment_method(self) -> CppModuleChunks:
        """Generate commit_alignment static method for aligned cogs.

        Advances aligned_cursor_seqno_ on each upstream InputView after successful execution.
        """
        assert self.inputs is not None
        assert self.inputs.aligned_groups

        arg_cog_inputs = CppNamedType(
            CppType([], "InputsType", None, const=False, ref=Ref.L),
            "cog_inputs",
        )
        arg_inputs = CppNamedType(
            CppType([], "typename InputsType::InputDialTuple", None, const=True, ref=Ref.L),
            "inputs",
        )

        body = CppChunk()

        for group in self.inputs.aligned_groups:
            alignment_handler = group.alignment_handler
            alignment_candidates_name = f"alignment_{group.group_name}_candidates"
            body.append(
                f"const auto {alignment_candidates_name} = {alignment_handler.render_get('inputs')}.get_cursor_view();",
            )
            body.append(f"if (!{alignment_candidates_name}.empty())")
            body.append("{")
            body.append(
                f"const auto& alignment_{group.group_name} = {alignment_candidates_name}.back();",
                indent=1,
            )

            commit_body = CppChunk()

            for upstream in group.upstream_handlers:
                resolved_input = group.resolved_inputs[upstream.input_name]
                is_batch = resolved_input.batch_size is not None
                is_optional = resolved_input.optional

                if is_batch:
                    seqno_expr = f"alignment_{group.group_name}.get_{upstream.input_name}_end_seq()"
                else:
                    seqno_expr = f"alignment_{group.group_name}.get_{upstream.input_name}_seq()"

                advance_stmt = (
                    f"{arg_cog_inputs.argument_name}.template advance_aligned_cursor<{upstream.index}>({seqno_expr});"
                )

                if is_optional:
                    has_expr = f"alignment_{group.group_name}.get_has_{upstream.input_name}()"
                    commit_body.append(f"if ({has_expr})")
                    commit_body.append("{")
                    commit_body.append(advance_stmt, indent=1)
                    commit_body.append("}")
                else:
                    commit_body.append(advance_stmt)

            body.append(commit_body, indent=1)
            body.append("}")

        return_type = VOID

        commit_method = CppMethod(
            name="commit_alignment",
            doc=None,
            return_type=return_type,
            arguments=[arg_cog_inputs, arg_inputs],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=False,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        cpp_mod = commit_method.render(parent_class=parent_type, enclosing_namespace=self.cpp_namespace)
        return _insert_template_params(self.template_params, cpp_mod)

    def _generate_get_conditions_mask_method(self, context: CompilerContext) -> CppModuleChunks:
        """Generate populate_trigger_conditions."""
        arg_timers = CppNamedType(
            CppType([], "typename TimersType::ConditionsTuple", None, const=True, ref=Ref.L),
            "timers" if self.timers else "/*timers*/",
        )

        arg_input_conditions = CppNamedType(
            CppType([], "typename ConditionsType::ConditionsTuple", None, const=True, ref=Ref.L),
            "input_conditions" if self.input_conditions else "/*input_conditions*/",
        )

        condition_mask_type = typereg.get_cpp_type(
            context=context,
            clk_type=get_underlying_enum_type(self.cog_ir.name, self.cog_ir.conditions),
        )

        body = CppChunk()
        body.append(f"{condition_mask_type.render('')} condition_mask{{}};")

        if self.timers:
            for condition_name, timer in self.timers.cond_name_to_timer.items():
                body.append(f"auto& {condition_name}_handle = std::get<{timer.index}>({arg_timers.argument_name});")
                body.append(f"if ({condition_name}_handle.is_active())")
                body.append("{")
                body.append(
                    indent=1,
                    chunk=f"condition_mask = condition_mask | static_cast<{condition_mask_type.render('')}>( {self.cog_ir.name}ConditionsMask::{condition_name});",
                )
                body.append("}")

        if self.input_conditions:
            for condition_name, input_condition in self.input_conditions.input_conditions_registry.items():
                body.append(
                    chunk=f"auto& {condition_name}_handle = std::get<{input_condition.index}>({arg_input_conditions.argument_name});",
                )
                body.append(f"if ({condition_name}_handle.is_active())")
                body.append("{")
                body.append(
                    indent=1,
                    chunk=f"condition_mask = condition_mask | static_cast<{condition_mask_type.render('')}>({self.cog_ir.name}ConditionsMask::{condition_name});",
                )
                body.append("}")

        body.append("return condition_mask;")

        get_conditions_mask_method = CppMethod(
            name="get_conditions_mask",
            doc=None,
            return_type=condition_mask_type,
            arguments=[arg_timers, arg_input_conditions],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=True,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace
        cpp_mod = get_conditions_mask_method.render(parent_class=parent_type, enclosing_namespace=enclosing_namespace)
        return _insert_template_params(self.template_params, cpp_mod)

    def _generate_populate_trigger_mask_method(self) -> CppModuleChunks:
        """Generate populate trigger mask."""
        arg_metrics_vec = CppNamedType(
            CppType([], "std::pmr::vector<::clockwork::EventMetrics>", None, const=True, ref=Ref.L), "event_metrics_vec"
        )

        arg_tachyon = CppNamedType(
            CppType(
                [],
                "::clockwork::Tap<::clockwork::Tachyon<" + self.cog_ir.name + "EventMetricsBatch>>",
                None,
                const=False,
                ref=Ref.L,
            ),
            "event_metrics_tachyon",
        )

        body = CppChunk()

        # resize only if the underlying event metrics is too small
        body.append(
            f"if ({arg_tachyon.argument_name}.get_underlying_event_metrics().size() < {arg_metrics_vec.argument_name}.size())"
        )
        body.append("{")
        body.append(
            indent=1,
            chunk=f"{arg_tachyon.argument_name}.get_underlying_event_metrics().resize({arg_metrics_vec.argument_name}.size());",
        )
        body.append("}")
        body.append(f"for (size_t i = 0; i < {arg_metrics_vec.argument_name}.size(); ++i)")
        body.append("{")

        body.append(
            indent=1,
            chunk=f"{arg_tachyon.argument_name}.get_mutable_event_metrics()[i].set_trigger_flags(static_cast<{self.cog_ir.name}ConditionsMask>({arg_metrics_vec.argument_name}.at(i).conditions_mask));",
        )
        body.append("}")

        populate_trigger_mask_method = CppMethod(
            name="populate_trigger_mask",
            doc=None,
            return_type=CppType([], "void", None),
            arguments=[arg_metrics_vec, arg_tachyon],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=False,
            static=True,
        )
        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace
        cpp_mod = populate_trigger_mask_method.render(parent_class=parent_type, enclosing_namespace=enclosing_namespace)
        return _insert_template_params(self.template_params, cpp_mod)

    def _generate_populate_input_telemetry_metrics_method(self) -> CppModuleChunks:
        """Generate populate_input_telemetry_metrics."""
        arg_inputs = CppNamedType(
            CppType([], "typename InputsType::SubscribersTuple", None, const=True, ref=Ref.L),
            "inputs" if self.inputs else "/*inputs*/",
        )

        arg_tachyon = CppNamedType(
            CppType(
                [],
                "::clockwork::Tap<::clockwork::Tachyon<" + self.cog_ir.name + "TelemetryMetrics>>",
                None,
                const=False,
                ref=Ref.L,
            ),
            "telemetry_metrics_tachyon" if self.inputs else "/*telemetry_metrics_tachyon*/",
        )

        body = CppChunk()

        if self.inputs:
            for index, inpt in enumerate(self.inputs.inputs_registry):
                body.append(
                    f"set_input_channel_telemetry_metrics(std::get<{index}>({arg_inputs.argument_name})->get_aggregated_input_metrics().telemetry_metrics, {arg_tachyon.argument_name}.get_mutable_{inpt.input_name}());"
                )

        populate_input_telemetry_metrics_method = CppMethod(
            name="populate_input_telemetry_metrics",
            doc=None,
            return_type=CppType([], "void", None),
            arguments=[arg_inputs, arg_tachyon],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=False,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace
        cpp_mod = populate_input_telemetry_metrics_method.render(
            parent_class=parent_type, enclosing_namespace=enclosing_namespace
        )
        return _insert_template_params(self.template_params, cpp_mod)

    def _generate_populate_input_event_metrics_method(self) -> CppModuleChunks:
        """Generate populate_input_event_metrics."""
        arg_inputs = CppNamedType(
            CppType([], "typename InputsType::SubscribersTuple", None, const=True, ref=Ref.L),
            "inputs" if self.inputs else "/*inputs*/",
        )

        arg_tachyon = CppNamedType(
            CppType(
                [],
                "::clockwork::Tap<::clockwork::Tachyon<" + self.cog_ir.name + "EventMetricsBatch>>",
                None,
                const=False,
                ref=Ref.L,
            ),
            "event_metrics_tachyon" if self.inputs else "/*event_metrics_tachyon*/",
        )
        body = CppChunk()

        if self.inputs:
            for index, inpt in enumerate(self.inputs.inputs_registry):
                body.append(
                    f"auto& {inpt.input_name}_event_metrics = std::get<{index}>({arg_inputs.argument_name})->get_aggregated_input_metrics().event_metrics;"
                )
                # resize only if the underlying event metrics is too small
                body.append(
                    f"if ({arg_tachyon.argument_name}.get_underlying_event_metrics().size() < {inpt.input_name}_event_metrics.size())"
                )
                body.append("{")
                body.append(
                    indent=1,
                    chunk=f"{arg_tachyon.argument_name}.get_underlying_event_metrics().resize({inpt.input_name}_event_metrics.size());",
                )
                body.append("}")
                body.append(f"for (size_t i = 0; i < {inpt.input_name}_event_metrics.size(); ++i)")
                body.append("{")

                body.append(
                    indent=1,
                    chunk=f"set_input_channel_event_metrics({inpt.input_name}_event_metrics.at(i), {arg_tachyon.argument_name}.get_mutable_event_metrics()[i].get_mutable_{inpt.input_name}());",
                )
                body.append("}")
                # call reset metrics on the input event metrics
                body.append(
                    chunk=f"std::get<{index}>({arg_inputs.argument_name})->reset_metrics();",
                )

        populate_input_event_metrics_method = CppMethod(
            name="populate_input_event_metrics",
            doc=None,
            return_type=CppType([], "void", None),
            arguments=[arg_inputs, arg_tachyon],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=False,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace
        cpp_mod = populate_input_event_metrics_method.render(
            parent_class=parent_type, enclosing_namespace=enclosing_namespace
        )
        return _insert_template_params(self.template_params, cpp_mod)

    def _generate_populate_output_event_metrics_method(self) -> CppModuleChunks:
        """Generate populate_output_event_metrics."""
        # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        output_publishers = []
        if self.publishers:
            # Filter out metrics publishers to get only actual output publishers
            output_publishers = [
                publisher
                for publisher in self.publishers.publisher_registry
                if publisher.metrics_log_type == cog.MetricsLogType.none and not publisher.is_report_group
            ]

        arg_event_metrics = CppNamedType(
            CppType([], "std::pmr::vector<::clockwork::EventMetrics>", None, const=True, ref=Ref.L),
            "event_metrics" if output_publishers else "/*event_metrics*/",
        )

        arg_tachyon = CppNamedType(
            CppType(
                [],
                "::clockwork::Tap<::clockwork::Tachyon<" + self.cog_ir.name + "EventMetricsBatch>>",
                None,
                const=False,
                ref=Ref.L,
            ),
            "event_metrics_tachyon" if output_publishers else "/*event_metrics_tachyon*/",
        )

        body = CppChunk()

        if output_publishers:
            # Resize the output metrics array if needed
            body.append(
                f"if ({arg_tachyon.argument_name}.get_underlying_event_metrics().size() < {arg_event_metrics.argument_name}.size())"
            )
            body.append("{")
            body.append(
                indent=1,
                chunk=f"{arg_tachyon.argument_name}.get_underlying_event_metrics().resize({arg_event_metrics.argument_name}.size());",
            )
            body.append("}")

            # Iterate through each event metrics entry
            body.append(f"for (size_t i = 0; i < {arg_event_metrics.argument_name}.size(); ++i)")
            body.append("{")

            # For each output publisher, set the num_messages for that index
            for index, publisher in enumerate(output_publishers):
                body.append(
                    indent=1,
                    chunk=f"if ({arg_event_metrics.argument_name}.at(i).output_metrics.contains({index}))",
                )
                body.append(indent=1, chunk="{")
                body.append(
                    indent=2,
                    chunk=f"{arg_tachyon.argument_name}.get_mutable_event_metrics()[i].set_{publisher.output_name}_num_messages({arg_event_metrics.argument_name}.at(i).output_metrics.at({index}));",
                )
                body.append(indent=1, chunk="}")
                body.append(indent=1, chunk="else")
                body.append(indent=1, chunk="{")
                body.append(
                    indent=2,
                    chunk=f"{arg_tachyon.argument_name}.get_mutable_event_metrics()[i].set_{publisher.output_name}_num_messages(0);",
                )
                body.append(indent=1, chunk="}")
                if publisher.rate_limit:
                    body.append(
                        indent=1,
                        chunk=f"if ({arg_event_metrics.argument_name}.at(i).publisher_throttle_counts.contains({index}))",
                    )
                    body.append(indent=1, chunk="{")
                    body.append(
                        indent=2,
                        chunk=f"{arg_tachyon.argument_name}.get_mutable_event_metrics()[i].set_{publisher.output_name}_throttle_count({arg_event_metrics.argument_name}.at(i).publisher_throttle_counts.at({index}));",
                    )
                    body.append(indent=1, chunk="}")
                    body.append(indent=1, chunk="else")
                    body.append(indent=1, chunk="{")
                    body.append(
                        indent=2,
                        chunk=f"{arg_tachyon.argument_name}.get_mutable_event_metrics()[i].set_{publisher.output_name}_throttle_count(0);",
                    )
                    body.append(indent=1, chunk="}")
            if any(publisher.rate_limit for publisher in output_publishers):
                body.append(
                    indent=1,
                    chunk=f"{arg_tachyon.argument_name}.get_mutable_event_metrics()[i].set_publisher_throttle_wait_duration({arg_event_metrics.argument_name}.at(i).publisher_throttle_wait_duration);",
                )
                body.append(
                    indent=1,
                    chunk=f"{arg_tachyon.argument_name}.get_mutable_event_metrics()[i].set_post_throttle_exec_latency({arg_event_metrics.argument_name}.at(i).post_throttle_exec_latency);",
                )
                body.append(
                    indent=1,
                    chunk=f"{arg_tachyon.argument_name}.get_mutable_event_metrics()[i].set_last_throttled_until({arg_event_metrics.argument_name}.at(i).last_throttled_until);",
                )
            body.append("}")

        populate_output_event_metrics_method = CppMethod(
            name="populate_output_event_metrics",
            doc=None,
            return_type=CppType([], "void", None),
            arguments=[arg_event_metrics, arg_tachyon],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=False,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace
        cpp_mod = populate_output_event_metrics_method.render(
            parent_class=parent_type, enclosing_namespace=enclosing_namespace
        )
        return _insert_template_params(self.template_params, cpp_mod)

    def _generate_populate_output_telemetry_metrics_method(self) -> CppModuleChunks:
        """Generate populate_output_telemetry_metrics."""
        # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        output_publishers = []
        if self.publishers:
            # Filter out metrics publishers and report groups to get only actual output publishers
            output_publishers = [
                publisher
                for publisher in self.publishers.publisher_registry
                if publisher.metrics_log_type == cog.MetricsLogType.none and not publisher.is_report_group
            ]

        arg_telemetry_metrics = CppNamedType(
            CppType([], "::clockwork::TelemetryMetrics", None, const=True, ref=Ref.L),
            "telemetry_metrics" if output_publishers else "/*telemetry_metrics*/",
        )

        arg_tachyon = CppNamedType(
            CppType(
                [],
                "::clockwork::Tap<::clockwork::Tachyon<" + self.cog_ir.name + "TelemetryMetrics>>",
                None,
                const=False,
                ref=Ref.L,
            ),
            "telemetry_metrics_tachyon" if output_publishers else "/*telemetry_metrics_tachyon*/",
        )

        body = CppChunk()

        if output_publishers:
            # For each output publisher, set the telemetry metrics
            for index, publisher in enumerate(output_publishers):
                body.append(
                    f"if (auto it = {arg_telemetry_metrics.argument_name}.output_metrics.find({index}); it != {arg_telemetry_metrics.argument_name}.output_metrics.end())"
                )
                body.append("{")
                body.append(
                    indent=1,
                    chunk=f"populate_tachyon_min_max_mean(it->second, {arg_tachyon.argument_name}.get_mutable_{publisher.output_name}_num_messages());",
                )
                body.append("}")
                if publisher.rate_limit:
                    body.append(
                        f"if (auto it = {arg_telemetry_metrics.argument_name}.publisher_throttle_counts.find({index}); it != {arg_telemetry_metrics.argument_name}.publisher_throttle_counts.end())"
                    )
                    body.append("{")
                    body.append(
                        indent=1,
                        chunk="if (const auto count = it->second.sum(); count)",
                    )
                    body.append(indent=1, chunk="{")
                    body.append(
                        indent=2,
                        chunk=f"{arg_tachyon.argument_name}.set_{publisher.output_name}_throttle_count(*count);",
                    )
                    body.append(indent=1, chunk="}")
                    body.append("}")
            if any(publisher.rate_limit for publisher in output_publishers):
                body.append(
                    f"{arg_tachyon.argument_name}.set_throttled_execution_count({arg_telemetry_metrics.argument_name}.throttled_execution_count);"
                )
                body.append(
                    f"populate_tachyon_min_max_mean({arg_telemetry_metrics.argument_name}.publisher_throttle_wait_duration, {arg_tachyon.argument_name}.get_mutable_publisher_throttle_wait_duration());"
                )
                body.append(
                    f"populate_tachyon_min_max_mean({arg_telemetry_metrics.argument_name}.post_throttle_exec_latency, {arg_tachyon.argument_name}.get_mutable_post_throttle_exec_latency());"
                )

        populate_output_telemetry_metrics_method = CppMethod(
            name="populate_output_telemetry_metrics",
            doc=None,
            return_type=CppType([], "void", None),
            arguments=[arg_telemetry_metrics, arg_tachyon],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=False,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace
        cpp_mod = populate_output_telemetry_metrics_method.render(
            parent_class=parent_type, enclosing_namespace=enclosing_namespace
        )
        return _insert_template_params(self.template_params, cpp_mod)

    def _generate_populate_event_metrics_method(self) -> CppModuleChunks:
        """Generate populate_event_metrics."""
        arg_event_metrics = CppNamedType(
            CppType([], "std::pmr::vector<::clockwork::EventMetrics>", None, const=True, ref=Ref.L),
            "event_metrics",
        )

        arg_inputs = CppNamedType(
            CppType([], "typename InputsType::SubscribersTuple", None, const=True, ref=Ref.L),
            "inputs",
        )

        arg_publishables = CppNamedType(
            CppType(
                [],
                "typename PublishersType::PublishablesTuple",
                None,
                const=False,
                ref=Ref.L,
            ),
            argument_name="publishables",
        )

        body = CppChunk()

        body.append(f"auto event_publishable = std::get<event_metrics_index>({arg_publishables.argument_name});")
        body.append("auto & event_metrics_msg = event_publishable.message();")
        body.append(f"set_common_event_metrics({arg_event_metrics.argument_name}, event_metrics_msg);")
        body.append(f"populate_input_event_metrics({arg_inputs.argument_name}, event_metrics_msg);")
        body.append(f"populate_output_event_metrics({arg_event_metrics.argument_name}, event_metrics_msg);")
        body.append(f"populate_trigger_mask({arg_event_metrics.argument_name}, event_metrics_msg);")
        body.append("event_publishable.mark_for_publish();")

        populate_event_metrics_method = CppMethod(
            name="populate_event_metrics",
            doc=None,
            return_type=CppType([], "void", None),
            arguments=[arg_event_metrics, arg_inputs, arg_publishables],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=False,
            static=True,
        )
        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace
        cpp_mod = populate_event_metrics_method.render(
            parent_class=parent_type, enclosing_namespace=enclosing_namespace
        )
        return _insert_template_params(self.template_params, cpp_mod)

    def _generate_populate_telemetry_metrics_method(self) -> CppModuleChunks:
        """Generate populate_telemetry_metrics."""
        arg_telemetry_metrics = CppNamedType(
            CppType([], "::clockwork::TelemetryMetrics", None, const=True, ref=Ref.L), "telemetry_metrics"
        )

        arg_inputs = CppNamedType(
            CppType([], "typename InputsType::SubscribersTuple", None, const=True, ref=Ref.L),
            "inputs",
        )

        arg_publishables = CppNamedType(
            CppType(
                [],
                "typename PublishersType::PublishablesTuple",
                None,
                const=False,
                ref=Ref.L,
            ),
            argument_name="publishables",
        )
        body = CppChunk()
        body.append(
            f"auto telemetry_publishable = std::get<telemetry_metrics_index>({arg_publishables.argument_name});"
        )
        body.append("auto & telemetry_metrics_msg = telemetry_publishable.message();")
        body.append(f"set_common_telemetry_metrics(telemetry_metrics_msg, {arg_telemetry_metrics.argument_name});")
        body.append(f"populate_input_telemetry_metrics({arg_inputs.argument_name}, telemetry_metrics_msg);")
        body.append(f"populate_output_telemetry_metrics({arg_telemetry_metrics.argument_name}, telemetry_metrics_msg);")
        body.append(f"populate_telemetry_triggers({arg_telemetry_metrics.argument_name}, telemetry_metrics_msg);")
        body.append("telemetry_publishable.mark_for_publish();")

        populate_telemetry_metrics_method = CppMethod(
            name="populate_telemetry_metrics",
            doc=None,
            return_type=CppType([], "void", None),
            arguments=[arg_telemetry_metrics, arg_inputs, arg_publishables],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=False,
            static=True,
        )
        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace
        cpp_mod = populate_telemetry_metrics_method.render(
            parent_class=parent_type, enclosing_namespace=enclosing_namespace
        )
        return _insert_template_params(self.template_params, cpp_mod)

    def _generate_publish_report_groups_method(self) -> CppModuleChunks:
        """Generate publish_report_groups method to publish report group data from signals API.

        For each report group:
        1. Check if should_publish_{group}() returns true
        2. Get the publishable from the tuple using the constexpr index
        3. Populate the message fields from the signals API
        4. Mark the publishable for publish
        5. Reset the signals data

        Always generates the method, even if no report groups exist (in which case it just returns success).

        Returns:
            CppModuleChunks containing the generated method.
        """
        arg_signals = CppNamedType(
            CppType([], "SignalApiType", None, const=False, ref=Ref.L),
            "signals",
        )

        arg_publishables = CppNamedType(
            CppType(
                [],
                "typename PublishersType::PublishablesTuple",
                None,
                const=False,
                ref=Ref.L,
            ),
            argument_name="publishables",
        )

        body = CppChunk()

        # Check if we have signals and report groups to process
        has_report_groups = False
        if self.signals_struct is not None and self.publishers is not None:
            # Get report group publishers
            report_group_publishers = [
                publisher for publisher in self.publishers.publisher_registry if publisher.is_report_group
            ]
            has_report_groups = len(report_group_publishers) > 0

            if has_report_groups:
                for publisher in report_group_publishers:
                    group_name = publisher.output_name
                    group_index = f"{group_name}_index"

                    # Find the report group and its signals
                    report_group = self.signals_struct.report_groups.get(group_name)
                    if report_group is None:
                        continue

                    # Check if this is a batched or post-aggregated group
                    is_batched = (
                        report_group.report_group_config is not None
                        and report_group.report_group_config.reporting_strategy.value == "batched"
                    )

                    # Generate the should_publish check
                    body.append(f"if (signals.should_publish_{group_name}()) {{")

                    # Get the publishable and message
                    body.append(f"    auto {group_name}_publishable = std::get<{group_index}>(publishables);")
                    body.append(f"    auto& {group_name}_msg = {group_name}_publishable.message();")

                    if is_batched:
                        # For batched: populate from signal API and reset
                        body.append(f"    signals.populate_{group_name}({group_name}_msg);")
                        body.append(f"    signals.reset_batch_{group_name}();")
                    else:
                        # For post-aggregated: populate from signal API and reset
                        body.append(f"    signals.populate_{group_name}({group_name}_msg);")
                        body.append(f"    signals.reset_{group_name}();")

                    body.append(f"    {group_name}_publishable.mark_for_publish();")
                    body.append("}")  # end if should_publish

        # Always add the return statement (will be the only statement if no report groups)
        if not has_report_groups:
            body.append("// No report groups defined")
            body.append("(void)signals;")
            body.append("(void)publishables;")

        body.append("return ::jewels::success;")

        publish_report_groups_method = CppMethod(
            name="publish_report_groups",
            doc="Publish report group data from the signals API.",
            return_type=CppType([], "::jewels::BinaryOutcome", None),
            arguments=[arg_signals, arg_publishables],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=True,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace
        cpp_mod = publish_report_groups_method.render(parent_class=parent_type, enclosing_namespace=enclosing_namespace)
        return _insert_template_params(self.template_params, cpp_mod)

    def _generate_execute_method(self) -> CppModuleChunks:
        """Generate execute."""
        arg_dial = CppNamedType(CppType([], "CogDial&", None), "dial")

        return_type = CppType([], "void", None)

        body = CppChunk()
        body.append(f"execute_cog({arg_dial.argument_name});")
        body.context.add_include(SystemHeader("utility"))

        execute_method = CppMethod(
            name="execute",
            doc=None,
            return_type=return_type,
            arguments=[arg_dial],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=False,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace if self.cpp_namespace is not None else ""  # pyright: ignore[reportUnnecessaryComparison] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        cpp_mod = execute_method.render(parent_class=parent_type, enclosing_namespace=enclosing_namespace)
        return _insert_template_params(self.template_params, cpp_mod)

    def _collect_signal_group_info(
        self,
    ) -> tuple[dict[str, BatchedReportGroupInfo], dict[str, PostAggregatedReportGroupInfo], list[str]]:
        """Collect report group information from the signals struct.

        Returns:
            Tuple of (batched_groups, post_agg_groups, all_group_names).
        """
        batched_groups: dict[str, BatchedReportGroupInfo] = {}
        post_agg_groups: dict[str, PostAggregatedReportGroupInfo] = {}
        all_group_names: list[str] = []

        if self.signals_struct is not None:
            batched_groups = collect_batched_report_groups(self.signals_struct)
            post_agg_groups = collect_post_aggregated_report_groups(self.signals_struct)
            all_group_names = list(batched_groups.keys()) + list(post_agg_groups.keys())

        return batched_groups, post_agg_groups, all_group_names

    def _generate_aggregate_signal_lifecycle_methods(
        self,
        all_group_names: list[str],
        ctx: _SignalMethodGenerationContext,
    ) -> CppModuleChunks:
        """Generate aggregate start_of_execution_signals and end_of_execution_signals methods.

        These methods call the corresponding method on all report groups.

        Args:
            all_group_names: List of all report group names.
            ctx: Context with argument types and rendering parameters.

        Returns:
            CppModuleChunks containing both lifecycle methods.
        """
        cpp_mod = CppModuleChunks()
        has_report_groups = len(all_group_names) > 0

        start_body = CppChunk()
        if has_report_groups:
            for group_name in all_group_names:
                start_body.append(f"signals.start_of_execution_{group_name}(current_time);")
        else:
            start_body.append("(void)signals;")
            start_body.append("(void)current_time;")

        start_method = CppMethod(
            name="start_of_execution_signals",
            doc="Begin the observation window for all report groups.",
            return_type=VOID,
            arguments=[ctx.arg_signals, ctx.arg_current_time],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=start_body,
            no_discard=False,
            static=True,
        )

        start_cpp_mod = start_method.render(parent_class=ctx.parent_type, enclosing_namespace=ctx.enclosing_namespace)
        cpp_mod.append(_insert_template_params(self.template_params, start_cpp_mod))

        end_body = CppChunk()
        # Infra cog metrics groups are excluded from the aggregate end_of_execution_signals call.
        # Their end_of_execution is called explicitly in populate_cog_metrics_signals after
        # signal values have been populated with completed execution data.
        user_group_names = [name for name in all_group_names if not _is_cog_metrics_group(name)]
        if user_group_names:
            for group_name in user_group_names:
                end_body.append(f"signals.end_of_execution_{group_name}(current_time);")
        else:
            end_body.append("(void)signals;")
            end_body.append("(void)current_time;")

        end_method = CppMethod(
            name="end_of_execution_signals",
            doc="End the observation window for all report groups.",
            return_type=VOID,
            arguments=[ctx.arg_signals, ctx.arg_current_time],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=end_body,
            no_discard=False,
            static=True,
        )
        end_cpp_mod = end_method.render(parent_class=ctx.parent_type, enclosing_namespace=ctx.enclosing_namespace)
        cpp_mod.append(_insert_template_params(self.template_params, end_cpp_mod))

        return cpp_mod

    def _generate_per_group_signal_forwarding_methods(
        self,
        all_group_names: list[str],
        batched_groups: dict[str, BatchedReportGroupInfo],
        ctx: _SignalMethodGenerationContext,
    ) -> CppModuleChunks:
        """Generate per-group forwarding methods for signal operations.

        For each report group, generates:
        - start_of_execution_<group>
        - end_of_execution_<group>
        - should_publish_<group>
        - populate_<group>
        - reset_<group> or reset_batch_<group>

        Args:
            all_group_names: List of all report group names.
            batched_groups: Dictionary of batched report group info (to determine if group is batched).
            ctx: Context with argument types and rendering parameters.

        Returns:
            CppModuleChunks containing all per-group forwarding methods.
        """
        cpp_mod = CppModuleChunks()

        for group_name in all_group_names:
            is_batched = group_name in batched_groups

            # start_of_execution_<group>
            fwd_start_body = CppChunk()
            fwd_start_body.append(f"signals.start_of_execution_{group_name}(current_time);")
            fwd_start = CppMethod(
                name=f"start_of_execution_{group_name}",
                doc=None,
                return_type=VOID,
                arguments=[ctx.arg_signals, ctx.arg_current_time],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=fwd_start_body,
                no_discard=False,
                static=True,
            )
            method_chunks = fwd_start.render(parent_class=ctx.parent_type, enclosing_namespace=ctx.enclosing_namespace)
            if self.cog_ir.is_generic():
                method_chunks.implementation_chunk.lines.insert(0, f"template <{self.template_params}>")
            cpp_mod.append(method_chunks)

            # end_of_execution_<group>
            fwd_end_body = CppChunk()
            fwd_end_body.append(f"signals.end_of_execution_{group_name}(current_time);")
            fwd_end = CppMethod(
                name=f"end_of_execution_{group_name}",
                doc=None,
                return_type=VOID,
                arguments=[ctx.arg_signals, ctx.arg_current_time],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=fwd_end_body,
                no_discard=False,
                static=True,
            )
            method_chunks = fwd_end.render(parent_class=ctx.parent_type, enclosing_namespace=ctx.enclosing_namespace)
            if self.cog_ir.is_generic():
                method_chunks.implementation_chunk.lines.insert(0, f"template <{self.template_params}>")
            cpp_mod.append(method_chunks)

            # should_publish_<group>
            fwd_sp_body = CppChunk()
            fwd_sp_body.append(f"return signals.should_publish_{group_name}();")
            fwd_sp = CppMethod(
                name=f"should_publish_{group_name}",
                doc=None,
                return_type=CppType([], "bool", None),
                arguments=[ctx.arg_signals_const],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=fwd_sp_body,
                no_discard=True,
                static=True,
            )
            method_chunks = fwd_sp.render(parent_class=ctx.parent_type, enclosing_namespace=ctx.enclosing_namespace)
            if self.cog_ir.is_generic():
                method_chunks.implementation_chunk.lines.insert(0, f"template <{self.template_params}>")
            cpp_mod.append(method_chunks)

            # populate_<group>
            arg_msg = CppNamedType(CppType([], "auto", None, ref=Ref.L), "msg")
            fwd_pop_body = CppChunk()
            fwd_pop_body.append(f"signals.populate_{group_name}(msg);")
            fwd_pop = CppMethod(
                name=f"populate_{group_name}",
                doc=None,
                return_type=VOID,
                arguments=[ctx.arg_signals_const, arg_msg],
                leading_qualifiers=["inline"],
                trailing_qualifiers=[],
                body=fwd_pop_body,
                no_discard=False,
                static=True,
            )
            method_chunks = fwd_pop.render(parent_class=ctx.parent_type, enclosing_namespace=ctx.enclosing_namespace)
            if self.cog_ir.is_generic():
                method_chunks.inline_chunk.lines.insert(0, f"template <{self.template_params}>")
            cpp_mod.append(method_chunks)

            # reset_<group> / reset_batch_<group>
            if is_batched:
                fwd_reset_body = CppChunk()
                fwd_reset_body.append(f"signals.reset_batch_{group_name}();")
                fwd_reset = CppMethod(
                    name=f"reset_batch_{group_name}",
                    doc=None,
                    return_type=VOID,
                    arguments=[ctx.arg_signals],
                    leading_qualifiers=[],
                    trailing_qualifiers=[],
                    body=fwd_reset_body,
                    no_discard=False,
                    static=True,
                )
            else:
                fwd_reset_body = CppChunk()
                fwd_reset_body.append(f"signals.reset_{group_name}();")
                fwd_reset = CppMethod(
                    name=f"reset_{group_name}",
                    doc=None,
                    return_type=VOID,
                    arguments=[ctx.arg_signals],
                    leading_qualifiers=[],
                    trailing_qualifiers=[],
                    body=fwd_reset_body,
                    no_discard=False,
                    static=True,
                )
            method_chunks = fwd_reset.render(parent_class=ctx.parent_type, enclosing_namespace=ctx.enclosing_namespace)
            if self.cog_ir.is_generic():
                method_chunks.implementation_chunk.lines.insert(0, f"template <{self.template_params}>")
            cpp_mod.append(method_chunks)

        return cpp_mod

    @staticmethod
    def _append_event_metrics_signals(  # noqa: PLR0913 # Arguments needed for full set of metrics signals
        body: CppChunk,
        inputs_list: list[InputHandler],
        output_publishers: list[PublisherHandler],
        condition_names: list[str],
        resource_names: list[str],
        signal_population: EventMetricsSignalPopulation,
    ) -> None:
        """Append event metrics signal assignments and end_of_execution to body."""
        input_sequence_metadata_types = signal_population.input_sequence_metadata_types
        outputs_with_first_sequence_number_signal = signal_population.outputs_with_first_sequence_number_signal
        publishables_arg_name = signal_population.publishables_arg_name

        # Global cog-level signals: unconditionally present in the event metrics group
        body.append(
            "signals.set_cog_dial_start_time("
            + "::jewels::time::SyncTime{std::chrono::nanoseconds{event_metrics.dial_start_time}});"
        )
        body.append(
            "signals.set_cog_exec_start_time("
            + "::jewels::time::SyncTime{std::chrono::nanoseconds{event_metrics.execution_start_time}});"
        )
        body.append("signals.set_cog_exec_duration(event_metrics.execution_duration);")
        body.append("if (event_metrics.execute_cog_wall_duration) {")
        body.append("  signals.set_execute_cog_wall_duration(*event_metrics.execute_cog_wall_duration);")
        body.append("}")
        body.append("if (event_metrics.execute_cog_thread_cpu_duration) {")
        body.append("  signals.set_execute_cog_thread_cpu_duration(*event_metrics.execute_cog_thread_cpu_duration);")
        body.append("}")
        body.append("if (event_metrics.execute_cog_thread_user_duration) {")
        body.append("  signals.set_execute_cog_thread_user_duration(*event_metrics.execute_cog_thread_user_duration);")
        body.append("}")
        body.append("if (event_metrics.execute_cog_thread_system_duration) {")
        body.append(
            "  signals.set_execute_cog_thread_system_duration(*event_metrics.execute_cog_thread_system_duration);"
        )
        body.append("}")
        body.append("signals.set_cog_ready_to_exec_latency(event_metrics.latency_first_ready_to_execution);")
        body.append("signals.set_cog_attempt_to_exec_latency(event_metrics.latency_first_attempt_to_execution);")
        body.append("signals.set_cog_requeue_count(event_metrics.num_requeues_before_execution);")

        # Per-input signals
        for idx, inpt in enumerate(inputs_list):
            input_name = inpt.input_name
            var = f"{input_name}_em"
            metadata_type_str = input_sequence_metadata_types.get(input_name)
            if metadata_type_str is not None:
                body.append("{")
                body.append(f"  {metadata_type_str} {input_name}_seqno_meta{{}};")
                body.append(
                    f"  static_cast<void>({input_name}_seqno_meta.try_set_message_sequence_numbers("
                    + f"std::get<{idx}>(inputs)->get_metrics_sequence_numbers()));"
                )
                body.append(
                    f"  {input_name}_seqno_meta.set_cursor_position("
                    + f"std::get<{idx}>(inputs)->get_metrics_cursor_position());"
                )
                body.append(
                    f"  signals.set_{input_name}_unseen_messages("
                    + f"{var}.empty() ? static_cast<uint16_t>(0) : {var}.back().num_unseen_messages, "
                    + f"{input_name}_seqno_meta);"
                )
                body.append("}")
            else:
                body.append(
                    f"signals.set_{input_name}_unseen_messages("
                    + f"{var}.empty() ? static_cast<uint16_t>(0) : {var}.back().num_unseen_messages);"
                )
            body.append(
                f"signals.set_{input_name}_staleness("
                + f"{var}.empty() ? ::clockwork::TenNanoseconds{{}} : {var}.back().message_staleness);"
            )
            body.append(
                f"signals.set_{input_name}_dropped_messages("
                + f"{var}.empty() ? static_cast<uint16_t>(0) : {var}.back().messages_dropped);"
            )

        # Per-output signals
        for idx, pub in enumerate(output_publishers):
            value_expr = (
                f"event_metrics.output_metrics.contains({idx}) "
                + f"? event_metrics.output_metrics.at({idx}) : static_cast<uint16_t>(0)"
            )
            body.append(f"signals.set_{pub.output_name}_num_messages({value_expr});")
            if pub.output_name in outputs_with_first_sequence_number_signal:
                body.append(
                    f"signals.set_{pub.output_name}_first_sequence_number("
                    + f"std::get<{pub.index}>({publishables_arg_name})"
                    + ".get_metrics_first_sequence_number().value_or(static_cast<uint64_t>(0U)));"
                )

        # Per-condition signals: bool indicating whether this condition was active at execution
        for bit_index, condition_name in enumerate(condition_names):
            body.append(
                f"signals.set_{condition_name}_active("
                + f"(event_metrics.conditions_mask & (1ULL << {bit_index}U)) != 0U);"
            )

        for idx, resource in enumerate(resource_names):
            for metric in ["peak_allocated", "current_allocated", "total_allocated", "total_deallocated"]:
                body.append(f"signals.set_{resource}_{metric}( event_metrics.resource_metrics.at({idx}).{metric});")

        # Close the event metrics observation window now that all signals are set
        body.append(f"signals.end_of_execution_{EVENT_METRICS_GROUP_NAME}(current_time);")

    @staticmethod
    def _append_telemetry_metrics_signals(
        body: CppChunk,
        inputs_list: list[InputHandler],
        output_publishers: list[PublisherHandler],
        condition_names: list[str],
        resource_names: list[str],
    ) -> None:
        """Append telemetry metrics signal assignments and end_of_execution to body."""
        # Global signals shared with the event group carry the telemetry prefix to avoid
        # API identifier collisions between the two groups.
        body.append(f"signals.set_{TELEMETRY_SIGNAL_PREFIX}cog_exec_duration(event_metrics.execution_duration);")
        body.append("if (event_metrics.execute_cog_wall_duration) {")
        body.append(
            f"  signals.set_{TELEMETRY_SIGNAL_PREFIX}execute_cog_wall_duration("
            + "*event_metrics.execute_cog_wall_duration);"
        )
        body.append("}")
        body.append("if (event_metrics.execute_cog_thread_cpu_duration) {")
        body.append(
            f"  signals.set_{TELEMETRY_SIGNAL_PREFIX}execute_cog_thread_cpu_duration("
            + "*event_metrics.execute_cog_thread_cpu_duration);"
        )
        body.append("}")
        body.append("if (event_metrics.execute_cog_thread_user_duration) {")
        body.append(
            f"  signals.set_{TELEMETRY_SIGNAL_PREFIX}execute_cog_thread_user_duration("
            + "*event_metrics.execute_cog_thread_user_duration);"
        )
        body.append("}")
        body.append("if (event_metrics.execute_cog_thread_system_duration) {")
        body.append(
            f"  signals.set_{TELEMETRY_SIGNAL_PREFIX}execute_cog_thread_system_duration("
            + "*event_metrics.execute_cog_thread_system_duration);"
        )
        body.append("}")
        body.append(
            f"signals.set_{TELEMETRY_SIGNAL_PREFIX}cog_ready_to_exec_latency("
            + "event_metrics.latency_first_ready_to_execution);"
        )
        body.append(
            f"signals.set_{TELEMETRY_SIGNAL_PREFIX}cog_attempt_to_exec_latency("
            + "event_metrics.latency_first_attempt_to_execution);"
        )
        body.append(
            f"signals.set_{TELEMETRY_SIGNAL_PREFIX}cog_requeue_count(" + "event_metrics.num_requeues_before_execution);"
        )

        # Telemetry-only signal: execution period is passed as a dedicated parameter because
        # EventMetrics does not carry a per-execution period value.
        body.append("signals.set_cog_exec_period(execution_period);")

        # Per-input agg signals
        for inpt in inputs_list:
            input_name = inpt.input_name
            var = f"{input_name}_em"
            body.append(
                f"signals.set_{TELEMETRY_SIGNAL_PREFIX}{input_name}_unseen_messages("
                + f"{var}.empty() ? static_cast<uint16_t>(0) : {var}.back().num_unseen_messages);"
            )
            body.append(
                f"signals.set_{TELEMETRY_SIGNAL_PREFIX}{input_name}_staleness("
                + f"{var}.empty() ? ::clockwork::TenNanoseconds{{}} : {var}.back().message_staleness);"
            )
            body.append(
                f"signals.set_{TELEMETRY_SIGNAL_PREFIX}{input_name}_dropped_messages("
                + f"{var}.empty() ? static_cast<uint16_t>(0) : {var}.back().messages_dropped);"
            )

        # Per-output agg signals
        for idx, pub in enumerate(output_publishers):
            body.append(
                f"signals.set_{TELEMETRY_SIGNAL_PREFIX}{pub.output_name}_num_messages("
                + f"event_metrics.output_metrics.contains({idx}) "
                + f"? event_metrics.output_metrics.at({idx}) : static_cast<uint16_t>(0));"
            )

        # Per-condition count signals: record whether each condition fired in this execution
        for bit_index, condition_name in enumerate(condition_names):
            body.append(
                f"signals.set_{condition_name}_active_count("
                + f"static_cast<uint16_t>((event_metrics.conditions_mask & (1ULL << {bit_index}U)) != 0U));"
            )

        for idx, resource in enumerate(resource_names):
            body.append(
                f"signals.set_{TELEMETRY_SIGNAL_PREFIX}{resource}_peak_allocated(event_metrics.resource_metrics.at({idx}).peak_allocated);"
            )

        # Close the telemetry metrics observation window now that all signals are set
        body.append(f"signals.end_of_execution_{TELEMETRY_METRICS_GROUP_NAME}(current_time);")

    def _collect_input_seqno_metadata(self, body: CppChunk, inputs_list: list[InputHandler]) -> dict[str, str]:
        """Build input name to metadata type mapping for sequence number metadata."""
        if self.signals_struct is None:
            return {}

        input_seqno_metadata: dict[str, str] = {}
        for inpt in inputs_list:
            signal_key = f"{EVENT_METRICS_GROUP_NAME}_{inpt.input_name}_unseen_messages"
            signal_entry = self.signals_struct.signals.get(signal_key)
            if signal_entry is None or signal_entry.metadata_type is None:
                continue
            input_seqno_metadata[inpt.input_name] = signal_entry.metadata_type.render(
                self.cpp_namespace, with_qualifiers=False
            )
            for inc in signal_entry.metadata_type.includes:
                body.context.add_include(inc)

        if input_seqno_metadata:
            body.context.add_include(SystemHeader("span"))
        return input_seqno_metadata

    def _collect_outputs_with_first_sequence_number_signal(self, output_publishers: list[PublisherHandler]) -> set[str]:
        """Build output names that have first sequence number signals."""
        if self.signals_struct is None:
            return set()

        outputs_with_first_sequence_number_signal: set[str] = set()
        for pub in output_publishers:
            signal_key = f"{EVENT_METRICS_GROUP_NAME}_{pub.output_name}_first_sequence_number"
            signal_entry = self.signals_struct.signals.get(signal_key)
            if signal_entry is None:
                continue
            outputs_with_first_sequence_number_signal.add(pub.output_name)

        return outputs_with_first_sequence_number_signal

    def _generate_populate_cog_metrics_signals(self) -> CppModuleChunks:
        """Generate populate_cog_metrics_signals for the cog metrics infrastructure report groups.

        Generated when either the event or telemetry infrastructure report group is present on the
        cog. The generated method sets signal values from a single EventMetrics observation and
        calls end_of_execution for each present infrastructure report group.

        Returns:
            CppModuleChunks containing the generated method, or an empty CppModuleChunks if
            neither cog metrics group is present on this cog.
        """
        cpp_mod = CppModuleChunks()

        has_event_group = EVENT_METRICS_GROUP_NAME in self.cog_ir.cog_metrics_report_groups
        has_telemetry_group = TELEMETRY_METRICS_GROUP_NAME in self.cog_ir.cog_metrics_report_groups

        if not has_event_group and not has_telemetry_group:
            return cpp_mod

        # Collect real inputs (in tuple index order)
        inputs_list = list(self.inputs.inputs_registry) if self.inputs else []

        # Collect real outputs (not metrics publishers, not report groups), in index order
        output_publishers = (
            [
                pub
                for pub in self.publishers.publisher_registry
                if pub.metrics_log_type == cog.MetricsLogType.none and not pub.is_report_group
            ]
            if self.publishers
            else []
        )

        # Bit i in conditions_mask corresponds to the i-th condition (timers first, then input conditions).
        condition_names: list[str] = []
        if self.timers:
            condition_names.extend(self.timers.cond_name_to_timer.keys())
        if self.input_conditions:
            condition_names.extend(self.input_conditions.input_conditions_registry.keys())

        resource_names = [res.resource_name for res in self.resources.resource_registry] if self.resources else []
        state_with_memory_resource_names = (
            [
                state.state_name
                for state in self.states.state_registry
                if isinstance(state.msg_type, ExternType) and not state.read_only
            ]
            if self.states
            else []
        )

        resource_names.extend(state_with_memory_resource_names)

        body = CppChunk()
        body.context.add_include(SystemHeader("chrono"))
        outputs_with_first_sequence_number_signal: set[str] = set()

        # Pre-define per-input metric variables shared by both the event and telemetry blocks.
        for idx, inpt in enumerate(inputs_list):
            input_name = inpt.input_name
            var = f"{input_name}_em"
            body.append(f"const auto& {var} = std::get<{idx}>(inputs)->get_aggregated_input_metrics().event_metrics;")

        if has_event_group:
            input_seqno_metadata = self._collect_input_seqno_metadata(body, inputs_list)
            outputs_with_first_sequence_number_signal = self._collect_outputs_with_first_sequence_number_signal(
                output_publishers
            )
            self._append_event_metrics_signals(
                body,
                inputs_list,
                output_publishers,
                condition_names,
                resource_names,
                EventMetricsSignalPopulation(
                    input_sequence_metadata_types=input_seqno_metadata,
                    outputs_with_first_sequence_number_signal=outputs_with_first_sequence_number_signal,
                    publishables_arg_name="output_publishables",
                ),
            )

        if has_telemetry_group:
            self._append_telemetry_metrics_signals(
                body, inputs_list, output_publishers, condition_names, resource_names
            )

        # Build argument types
        arg_signals = CppNamedType(CppType([], "SignalApiType", None, const=False, ref=Ref.L), "signals")

        event_metrics_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/cog/cog_statistics.hh")],
            type_name="EventMetrics",
            cpp_namespace="clockwork",
            const=True,
            ref=Ref.L,
        )
        arg_event_metrics = CppNamedType(event_metrics_type, "event_metrics")

        arg_inputs = CppNamedType(
            CppType([], "typename InputsType::SubscribersTuple", None, const=True, ref=Ref.L),
            "inputs" if inputs_list else "/*inputs*/",
        )

        arg_output_publishables = CppNamedType(
            CppType([], "typename PublishersType::PublishablesTuple", None, const=True, ref=Ref.L),
            "output_publishables" if outputs_with_first_sequence_number_signal else "/*output_publishables*/",
        )

        ten_ns_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/dsl/cog/ten_nanosecond_type.hh")],
            type_name="TenNanoseconds",
            cpp_namespace="clockwork",
        )
        arg_execution_period = CppNamedType(
            argument_type=ten_ns_type,
            argument_name="execution_period" if has_telemetry_group else "/*execution_period*/",
        )

        sync_time_type = CppType(
            includes=[Header(CLK_REPO, "jewels/time/sync_time.hh")],
            type_name="SyncTime",
            cpp_namespace="jewels::time",
        )
        arg_current_time = CppNamedType(argument_type=sync_time_type, argument_name="current_time")

        method = CppMethod(
            name="populate_cog_metrics_signals",
            doc=(
                "Populate cog metrics signals from a single execution observation and close"
                " the observation window for each present infrastructure report group."
            ),
            return_type=VOID,
            arguments=[
                arg_signals,
                arg_event_metrics,
                arg_inputs,
                arg_output_publishables,
                arg_execution_period,
                arg_current_time,
            ],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=False,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        cpp_mod.append(method.render(parent_class=parent_type, enclosing_namespace=self.cpp_namespace))
        return _insert_template_params(self.template_params, cpp_mod)

    def _generate_signal_infra_methods(self) -> CppModuleChunks:
        """Generate signal infrastructure methods on the Policy.

        Generates:
        - start_of_execution_signals: Calls start_of_execution for all report groups.
        - end_of_execution_signals: Calls end_of_execution for all report groups.
        - Per-group forwarding methods for infra operations (start, end, should_publish, populate, reset).

        These methods provide the only access path to the private infra methods on the SignalApi.

        Returns:
            CppModuleChunks containing all generated signal infra methods.
        """
        cpp_mod = CppModuleChunks()
        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace

        # Set up common argument types
        arg_signals = CppNamedType(
            CppType([], "SignalApiType", None, const=False, ref=Ref.L),
            "signals",
        )
        arg_signals_const = CppNamedType(
            CppType([], "SignalApiType", None, const=True, ref=Ref.L),
            "signals",
        )
        sync_time_type = CppType(
            includes=[Header(CLK_REPO, "jewels/time/sync_time.hh")],
            type_name="SyncTime",
            cpp_namespace="jewels::time",
        )
        arg_current_time = CppNamedType(argument_type=sync_time_type, argument_name="current_time")

        batched_groups, _, all_group_names = self._collect_signal_group_info()

        ctx = _SignalMethodGenerationContext(
            arg_signals=arg_signals,
            arg_signals_const=arg_signals_const,
            arg_current_time=arg_current_time,
            parent_type=parent_type,
            enclosing_namespace=enclosing_namespace,
        )

        cpp_mod.append(self._generate_aggregate_signal_lifecycle_methods(all_group_names, ctx))
        cpp_mod.append(self._generate_per_group_signal_forwarding_methods(all_group_names, batched_groups, ctx))
        cpp_mod.append(self._generate_populate_cog_metrics_signals())

        return cpp_mod


@dataclass
class _SignalMethodGenerationContext:
    """Context for generating signal infrastructure methods.

    Groups related parameters to reduce function argument counts.
    """

    arg_signals: CppNamedType
    arg_signals_const: CppNamedType
    arg_current_time: CppNamedType
    parent_type: CppType
    enclosing_namespace: str


@dataclass
class InstantiatedCog:
    """Representation of an instantiated C++ Cog."""

    instantiation: cog.InstantiatedCog
    class_name: str
    dial_name: str
    header_name: str | None
    cpp_namespace: str
    dial_header: Header

    template_args: str = ""
    cog_policy_name: str = ""

    @classmethod
    def make(  # noqa: PLR0913 (this is the central cog generation function so needs several parameters)
        cls: type[InstantiatedCog],
        instantiation: cog.InstantiatedCog,
        class_name: str,
        dial_name: str | None,
        header_name: str | None,
        cpp_namespace: str,
        dial_header: Header,
    ) -> InstantiatedCog:
        """Make InstantiatedCog instance."""
        if not dial_name:
            dial_name = to_dial_name(class_name)
        return cls(
            instantiation=instantiation,
            class_name=class_name,
            header_name=header_name,
            dial_name=dial_name,
            cpp_namespace=cpp_namespace,
            dial_header=dial_header,
        )

    def render(self) -> CppModuleChunks:
        """Generate header and source skeleton for InstantiatedCog."""
        cpp_mod = CppModuleChunks()

        for cpp_arg in get_cog_instantiation_args(self.instantiation):
            cpp_mod.header_chunk.context.add_includes(cpp_arg.includes)
        rendered_template_args = get_rendered_cog_instantiation_args(self.instantiation, self.cpp_namespace)

        cog_policy_name = f"{self.class_name}Policy<{rendered_template_args}>"
        cog_policy_base_name = f"{self.class_name}PolicyBase<{rendered_template_args}>"
        cog_fqn: Final = self.instantiation.cog_ir.fqn

        cpp_mod.header_chunk.append(
            [
                "template<>",
                f"struct {cog_policy_name} : public {cog_policy_base_name}",
                "{",
            ]
        )

        cpp_mod.header_chunk.append(
            [
                gen_const_str([cog_fqn, cog_policy_name.replace('"', "")]),
                f"static constexpr auto cog_id = {UuidHandler(COG_CLASS_UUID_TYPE, uuid_reg.lookup_uuid(self.instantiation.module.context, self.instantiation)).render_from_string_func()};",
            ],
            indent=1,
        )

        cpp_mod.header_chunk.append("};")

        cpp_mod.implementation_chunk.append(f"template struct {cog_policy_base_name};")

        cpp_mod.append(
            _gen_factory(
                UuidHandler(
                    COG_CLASS_UUID_TYPE, uuid_reg.lookup_uuid(self.instantiation.module.context, self.instantiation)
                ),
                self.class_name,
                "CogFactory",
                f"::jewels::memory::MemoryResource resource, const ::jewels::Uuid<::{CLOCKWORK_NAMESPACE}::common::CogInstanceId>& instance_id, ::jewels::memory::ObjectPtr<::{CLOCKWORK_NAMESPACE}::AbstractCogQueue> queue",
                f"return ::jewels::memory::make_pmr_shared<::{CLOCKWORK_NAMESPACE}::SimpleCog<{self.class_name}Policy<{rendered_template_args}>>>(resource, resource, instance_id, queue);",
                rendered_template_args,
                True,
            )
        )

        for state_def in self.instantiation.states.values():
            cpp_mod.append(self.render_state_factory(state_def, cog_policy_name, self.cpp_namespace))

        return cpp_mod

    def render_state_factory(
        self, state_def: cog.StateDef, parent_policy_name: str, enclosing_namespace: str
    ) -> CppModuleChunks:
        """Render the state factory struct implementation."""
        if isinstance(state_def.message_type, ExternType):
            make_params = "::jewels::memory::MemoryResource memres_state"
            make_args = "std::move(memres_state)"
            repr_uuid = uuid_reg.lookup_uuid(self.instantiation.module.context, state_def.message_type)
            cpp_type = typereg.get_cpp_type(state_def.module.context, state_def.message_type)
        elif isinstance(state_def.message_type, schema_reg.InterfaceInfo):
            make_params = f"::{CLOCKWORK_NAMESPACE}::pinion::PublisherHandle publisher"
            make_args = "std::move(publisher)"
            assert state_def.message_type.interface_ir.representation is not None
            repr_uuid = uuid_reg.lookup_uuid(
                self.instantiation.module.context, state_def.message_type.interface_ir.representation.typespec
            )
            cpp_type = typereg.get_cpp_type(state_def.module.context, state_def.message_type.interface_ir.typespec)
        else:
            msg = self.instantiation.cog_ir.append_error_line(f"Unknown state message type {state_def.message_type}")
            raise TypeError(msg)
        snapshot_make = _get_serializable_state_factory(
            self.instantiation.module.context,
            state_def.message_type,
            const_qualify(cpp_type, False).render(enclosing_namespace),
            enclosing_namespace,
        )
        cpp_mod = _gen_factory(
            UuidHandler(_get_representation_uuid_type(self.instantiation.module.context), repr_uuid),
            f"{parent_policy_name}::{to_camel(state_def.name) + 'Policy'}::",
            "CogStateFactory",
            "::jewels::memory::MemoryResource memres_sys, " + make_params,
            f"return ::jewels::memory::make_pmr_shared<::{CLOCKWORK_NAMESPACE}::CogStateDataImpl<{const_qualify(cpp_type, False).render(enclosing_namespace)}>>(memres_sys, {make_args});",
            "",
            True,
            snapshot_make,
        )
        if snapshot_make:
            _add_serializable_state_factory_includes(cpp_mod)
        return cpp_mod


def _insert_template_params(params_str: str, cpp_mod: CppModuleChunks) -> CppModuleChunks:
    """Insert a template parameter line `template<params>` before the first line in the module.

    Checks whether the chunk is producing inline or implementation and inserts the
    template parameters into the appropriate chunk.

    Args:
        params_str: Template parameters string like 'class T1, int n'.
        cpp_mod: CppModuleChunks to update.

    Returns:
        Updated module chunks.
    """
    if params_str:
        if cpp_mod.inline_chunk.produce:
            cpp_mod.inline_chunk.lines.insert(0, f"template <{params_str}>")
        else:
            cpp_mod.implementation_chunk.lines.insert(0, f"template <{params_str}>")
    return cpp_mod
