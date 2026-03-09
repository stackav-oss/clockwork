# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Facilities for generating C++ Cog structs."""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal
from typing import TYPE_CHECKING, Final

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
from clockwork.dsl.ir import cog, primitive, schema_reg, units, uuid_reg
from clockwork.dsl.ir.clkbuiltins import REPRESENTATION_TAG_TYPE
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
    from uuid import UUID

    from clockwork.dsl.compiler_context import CompilerContext


_UUID_TEMPLATE = CppTemplate([Header(JEWELS_REPO, "jewels/uuid/uuid/hh")], "Uuid", "jewels")
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


def _get_representation_uuid_type(context: CompilerContext) -> CppTemplateType:
    """Get the UUID type for representations."""
    return _UUID_TEMPLATE.instantiate([get_cpp_type(context, REPRESENTATION_TAG_TYPE)])


def to_camel(snake: str) -> str:
    """Convert from snake to camel case."""
    toks = snake.split("_")
    return "".join(tok.title() for tok in toks)


def _formatted_struct(name: str, declarations: str | list[str]) -> CppChunk:
    """Helper function to format a struct."""
    chunk = CppChunk()
    chunk.append(
        [
            f"struct {name}",
            "{",
        ],
    )
    # Body
    chunk.append(
        declarations,
        indent=1,
    )
    # Closing
    chunk.append("};")
    return chunk


def _cond_type_present(
    root: cog.ConditionExpr,
    looking_for: type[cog.LogConditionExpr | cog.InitConditionExpr | cog.TimeSinceLastExec | cog.MessagesPresent],
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
    timer_handle_map: dict[str, TimeSinceLastExecHandler],
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


def _gen_const_str(terms: str | Iterable[str], name: str = "name") -> str:
    full_str = terms if isinstance(terms, str) else ".".join(terms)
    init = f' = "{full_str}"' if full_str else "{}"
    return f"static constexpr ::std::string_view {name}{init};"


def _gen_const_size(value: int, name: str) -> str:
    return f"static constexpr size_t {name} = {value};"


def _gen_const_milliseconds(value: primitive.UnitValue, name: str) -> str:
    return f"static constexpr auto {name} = ::std::chrono::milliseconds({value.value});"


def _gen_factory(
    uuid: UuidHandler, class_name: str, base_name: str, make_params: str, make_body: str
) -> CppModuleChunks:
    fq_base_name = f"::{CLOCKWORK_NAMESPACE}::{base_name}"
    cpp_mod = CppModuleChunks()
    cpp_mod.header_chunk.append(f"struct {class_name}Factory : {fq_base_name}\n{{")
    cpp_mod.header_chunk.append(
        [
            f"static constexpr auto type_id = {uuid.render_from_string_func()};",
            f"[[nodiscard]] const {fq_base_name}::IdType &id() const override;",
            f"[[nodiscard]] {fq_base_name}::Ptr make({make_params}) const override;",
        ],
        indent=1,
    )
    cpp_mod.header_chunk.append("};")

    cpp_mod.implementation_chunk.append(
        f"const {fq_base_name}::IdType &{class_name}Factory::id() const",
    )
    cpp_mod.implementation_chunk.append("{")
    cpp_mod.implementation_chunk.append("return type_id;", indent=1)
    cpp_mod.implementation_chunk.append("}")
    cpp_mod.implementation_chunk.append(f"{fq_base_name}::Ptr {class_name}Factory::make({make_params}) const")
    cpp_mod.implementation_chunk.append("{")
    cpp_mod.implementation_chunk.append(
        f"{make_body}",
        indent=1,
    )
    cpp_mod.implementation_chunk.append("}")
    cpp_mod.implementation_chunk.append(
        f"static {class_name}Factory {to_snake(class_name.replace('::', '__'))}_factory_inst;"
    )
    return cpp_mod


def _comma_append(lines: list[str], end: str = "") -> list[str]:
    """Append a comma to all but the last line, where an optional end is appended instead. Used to help code gen lists."""
    return [i + j for i, j in zip(lines, (",",) * (len(lines) - 1) + (end,), strict=False)]


@dataclass(frozen=True)
class UuidHandler:
    """Manage UUID."""

    uuid_cpp_type: CppTemplateType
    uuid: UUID

    def render_from_string_func(self) -> str:
        """Render Uuid::from_string(...).value() ."""
        return f'{self.uuid_cpp_type.render("")}::from_string("{self.uuid}").value()'


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
        endpoint_id = UuidHandler(_ENDPOINT_UUID_TYPE, resource.uuid)
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
            _gen_const_str([self.cog_name, self.policy_name]),
        ]
        chunk.append(_formatted_struct(self.policy_name, body))
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
        endpoint_id = UuidHandler(_ENDPOINT_UUID_TYPE, config.uuid)
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
            _gen_const_str([self.cog_name, self.policy_name]),
        ]
        chunk.append(_formatted_struct(self.policy_name, body))
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
    msg_type: schema_reg.InterfaceInfo | ExternType
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
        endpoint_id = UuidHandler(_ENDPOINT_UUID_TYPE, state.uuid)
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
        body = [
            f"using StateType = {self.cpp_type.render(enclosing_namespace, with_qualifiers=False)};",
            "struct Factory;",
            f"static constexpr auto endpoint_id = {self.endpoint_id.render_from_string_func()};",
            f"static constexpr bool read_only = {'true' if self.read_only else 'false'};",
            _gen_const_str([self.cog_name, self.policy_name]),
        ]
        chunk.append(_formatted_struct(self.policy_name, body))
        return chunk

    def render_factory_struct(
        self, compiler_context: CompilerContext, parent_policy_name: str, enclosing_namespace: str
    ) -> CppModuleChunks:
        """Render the factory struct implementation."""
        if isinstance(self.msg_type, ExternType):
            make_params = "::jewels::memory::MemoryResource memres_state"
            make_args = "std::move(memres_state)"
            repr_uuid = uuid_reg.lookup_uuid(compiler_context, self.msg_type)
        elif isinstance(self.msg_type, schema_reg.InterfaceInfo):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            make_params = f"::{CLOCKWORK_NAMESPACE}::pinion::PublisherHandle publisher"
            make_args = "std::move(publisher)"
            assert self.msg_type.interface_ir.representation is not None
            repr_uuid = uuid_reg.lookup_uuid(compiler_context, self.msg_type.interface_ir.representation.typespec)
        else:
            msg = f"Unknown state message type {self.msg_type}"
            raise TypeError(msg)
        return _gen_factory(
            UuidHandler(_get_representation_uuid_type(compiler_context), repr_uuid),
            f"{parent_policy_name}::{self.policy_name}::",
            "CogStateFactory",
            "::jewels::memory::MemoryResource memres_sys, " + make_params,
            f"return ::jewels::memory::make_pmr_shared<::{CLOCKWORK_NAMESPACE}::CogStateDataImpl<{const_qualify(self.cpp_type, False).render(enclosing_namespace)}>>(memres_sys, {make_args});",
        )

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
        endpoint_id = UuidHandler(_ENDPOINT_UUID_TYPE, condition.uuid)
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
            _gen_const_str([self.cog_name, self.policy_name]),
        ]
        chunk.append(_formatted_struct(self.policy_name, body))
        return chunk

    def render_get(self, arg_timers: str) -> str:
        """Generate get expression."""
        return f"::std::get<{self.index}>({arg_timers})"


@dataclass
class Timers:
    """Manage timers."""

    timer_registry: tuple[TimeSinceLastExecHandler, ...]
    cond_name_to_timer: dict[str, TimeSinceLastExecHandler]

    @classmethod
    def make(
        cls: type[Timers],
        cog_exec_conditions: ConditionsStruct,
        cog_name: str,
    ) -> Timers:
        """Make Timers."""
        timer_conditions = [
            cond for cond in cog_exec_conditions.conditions.values() if isinstance(cond, TimeSinceLastExecCondition)
        ]
        registry = [TimeSinceLastExecHandler.make(cond, cog_name, idx) for idx, cond in enumerate(timer_conditions)]
        cond_name_to_timer = {timer.name: timer for timer in registry}
        return cls(timer_registry=tuple(registry), cond_name_to_timer=cond_name_to_timer)

    def render_timers(self) -> CppChunk:
        """Render Timers section."""
        chunk = CppChunk()

        # Add relevant headers
        chunk.context.add_includes([Header(CLK_REPO, "clockwork/cog/cog_timers.hh")])

        # Render policies and CogTimers
        chunk.append("/// Timers ///")
        for timer in self.timer_registry:
            chunk.append(timer.render_policy_struct())
        policy_template_args = ", ".join([timer.policy_name for timer in self.timer_registry])
        chunk.append(f"using TimersType = ::{CLOCKWORK_NAMESPACE}::CogTimers<{policy_template_args}>;")
        return chunk

    def __len__(self) -> int:
        """Convenience size getter."""
        return len(self.timer_registry)

    def __iter__(self) -> Iterator[TimeSinceLastExecHandler]:
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
    ) -> InputHandler:
        """Make InputHandler."""
        endpoint_id = UuidHandler(_ENDPOINT_UUID_TYPE, cog_input.uuid)
        input_name = cog_input.identifier
        policy_name = to_camel(input_name) + "Policy"
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

        policy_name = to_camel(self.input_name) + "Policy"
        body = [
            f"using MsgType = {msg_type};",
            f"static constexpr auto endpoint_id = {self.endpoint_id.render_from_string_func()};",
            _gen_const_str([self.cog_name, policy_name]),
            f"static constexpr auto max_view_size = {max_view_size};",
            f"static constexpr auto min_msgs = {min_msgs};",
            f"static constexpr auto min_new_msgs = {min_new_msgs};",
            f"static constexpr std::optional<::ssize_t> safety_margin = {safety_margin};",
            f"static constexpr std::optional<size_t> skip_threshold = {skip_threshold};",
            f"static constexpr auto copy_inputs = {str(self.cog_input.copy_inputs).lower()};",
            f"static constexpr auto manual_cursor = {str(manual_cursor).lower()};",
        ]
        chunk.append(_formatted_struct(policy_name, body))
        return chunk

    def render_get(self, arg_name: str) -> str:
        """Generate get expression."""
        return f"::std::get<{self.index}>({arg_name})"


@dataclass
class Inputs:
    """Manage all Inputs."""

    inputs_registry: tuple[InputHandler, ...]
    input_uuid_map: dict[str, UuidHandler]

    @classmethod
    def make(
        cls: type[Inputs],
        cog_inputs: InputsStruct,
        cog_name: str,
    ) -> Inputs:
        """Make Inputs."""
        registry = [
            InputHandler.make(ipt, cog_name, idx)
            for idx, ipt in enumerate(cog_ipt for cog_ipt in cog_inputs.inputs.values() if not cog_ipt.no_dial)
        ]
        input_uuid_map = {ipt.input_name: ipt.endpoint_id for ipt in registry}
        return cls(
            inputs_registry=tuple(registry),
            input_uuid_map=input_uuid_map,
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
            _gen_const_str([self.cog_name, policy_name]),
            f"static constexpr auto bounds_min = {bounds_min};",
            f"static constexpr auto bounds_max = {bounds_max};",
            f"static constexpr auto condition_type = {condition_type};",
        ]
        chunk.append(_formatted_struct(policy_name, body))
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
    cog_metrics_output: bool
    is_report_group: bool

    @classmethod
    def make(cls: type[PublisherHandler], output: Output, cog_name: str, index: int) -> PublisherHandler:
        """Make a TimeSinceLastExecHandler instance."""
        output_name = output.identifier
        endpoint_id = UuidHandler(_ENDPOINT_UUID_TYPE, output.uuid)
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
            _gen_const_str([self.cog_name, self.policy_name]),
            f"static constexpr bool has_diagnostics = {'true' if has_diag else 'false'};",
        ]
        if self.rate_limit:
            period_ns = int(self.rate_limit.period_s * 1e9)
            body.append(
                f"static constexpr std::optional<::clockwork::RateLimitParameters> rate_limit_params{{{{.limit={self.rate_limit.limit}U, .period=std::chrono::nanoseconds{{{period_ns}U}}}}}};"
            )
        else:
            body.append("static constexpr std::optional<::clockwork::RateLimitParameters> rate_limit_params{};")

        chunk.append(_formatted_struct(self.policy_name, body))
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
            if publisher.cog_metrics_output and publisher.metrics_log_type == cog.MetricsLogType.telemetry:
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
            if publisher.metrics_log_type not in (cog.MetricsLogType.telemetry, cog.MetricsLogType.event)
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
            and publisher.metrics_log_type in (cog.MetricsLogType.telemetry, cog.MetricsLogType.event)
        ]
        chunk.append(
            f"using MetricsPublishersType = ::{CLOCKWORK_NAMESPACE}::CogPublishers<{', '.join(metrics_template_args)}>;"
        )

        if any(
            (publisher.metrics_log_type == cog.MetricsLogType.telemetry)
            | (publisher.metrics_log_type == cog.MetricsLogType.event)
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
        """Check for presence of any timer condition."""
        return _cond_type_present(self.execute_when_condition, cog.TimeSinceLastExec)

    def message_condition_present(self) -> bool:
        """Check for presence of any message condition."""
        return _cond_type_present(self.execute_when_condition, cog.MessagesPresent)

    def render(
        self,
        timer_handle_map: dict[str, TimeSinceLastExecHandler],
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
    group_id: str | None
    instance_id: str | None
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
        endpoint_id = UuidHandler(_ENDPOINT_UUID_TYPE, cog_diagnostics.uuid)
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
            _formatted_struct(
                self.policy_name,
                [
                    f"static constexpr auto endpoint_id = {self.endpoint_id.render_from_string_func()};",
                    _gen_const_str([self.cog_name, self.name]),
                    _gen_const_str(name="member_name", terms=self.name),
                    _gen_const_str(name="group_name", terms=(self.group_id or "")),
                    _gen_const_str(name="instance_name", terms=(self.instance_id or "")),
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

    @classmethod
    def make(
        cls: type[InfraDiagnostics],
        cog_diagnostics: Diagnostics,
        infra_diagnostics: InfraDiagnosticsDef,
        cog_name: str,
        cog_class_name: str,
        dial_header: Header,
    ) -> InfraDiagnostics:
        """Make infra diagnostics."""
        endpoint_id = UuidHandler(_ENDPOINT_UUID_TYPE, cog_diagnostics.uuid)
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

        dial_type = CppType([], self.dial_name, self.cpp_namespace)

        self.cog_policy_name = self.class_name + "Policy"
        cpp_mod.header_chunk.append(
            [
                f"struct {self.cog_policy_name}",
                "{",
            ]
        )

        cog_fqn: Final = self.cog_ir.fqn

        cpp_mod.header_chunk.append(
            _gen_const_str([cog_fqn, self.cog_policy_name]),
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

        self.signals_struct = SignalsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.report_groups)
        has_batched_signals = any(signal.is_batched for signal in self.signals_struct.signals.values())
        cpp_mod.header_chunk.append(
            f"static constexpr auto has_signals = {'true' if has_batched_signals else 'false'};",
            indent=1,
        )

        cpp_mod.header_chunk.append(
            f"using SignalApiType = {self.dial_name}SignalApi;",
            indent=1,
        )

        cpp_mod.header_chunk.append(
            f"static constexpr auto cog_id = {UuidHandler(_COG_CLASS_UUID_TYPE, uuid_reg.lookup_uuid(self.cog_ir.module.context, self.cog_ir)).render_from_string_func()};",
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
            InputsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.inputs, self.cog_ir.execution_spec),
            cog_fqn,
        )
        cpp_mod.header_chunk.append(self.inputs.render_inputs(), indent=1)

        self.input_conditions = InputConditions.make(
            ConditionsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.conditions),
            cog_fqn,
            self.inputs.input_uuid_map,
        )
        cpp_mod.header_chunk.append(self.input_conditions.render_input_conditions(), indent=1)

        output_dict = self.cog_ir.outputs | self.cog_ir.metrics_outputs | self.cog_ir.report_groups
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
        )

        cpp_mod.header_chunk.append(self.diagnostics.render_diagnostics(), indent=1)
        cpp_mod.header_chunk.append(self.infra_diags.render_diagnostics(self.cpp_namespace), indent=1)

        cpp_mod.append(self._generate_is_ready_method())
        cpp_mod.append(self._generate_make_dial_method())
        cpp_mod.append(self._generate_execute_method())
        if self.cog_ir.metrics_options.metrics_enabled:
            self._append_metrics_methods(cpp_mod)
        cpp_mod.append(self._generate_publish_report_groups_method())
        cpp_mod.append(self._generate_signal_infra_methods())
        cpp_mod.header_chunk.append("};")

        cpp_mod.header_chunk.append(
            f"using {self.class_name} = ::{CLOCKWORK_NAMESPACE}::SimpleCog<{self.cog_policy_name}>;"
        )

        cpp_mod.append(
            _gen_factory(
                UuidHandler(_COG_CLASS_UUID_TYPE, uuid_reg.lookup_uuid(self.cog_ir.module.context, self.cog_ir)),
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
        return populate_telemetry_triggers_method.render(
            parent_class=parent_type, enclosing_namespace=enclosing_namespace
        )

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
        return populate_condition_trigger_vals_method.render(
            parent_class=parent_type, enclosing_namespace=enclosing_namespace
        )

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
        return is_ready_method.render(parent_class=parent_type, enclosing_namespace=self.cpp_namespace)

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
                "PublishersType::PublishablesTuple",
                None,
            ),
            "publishables"
            if self.publishers
            and any(publisher.metrics_log_type == cog.MetricsLogType.none for publisher in self.publishers)
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

        return_type = CppType([], self.dial_name, None)

        body = CppChunk()
        body.append(f"return {return_type.type_name}(")
        # start_time
        start_time_inner_chunk = CppChunk()
        start_time_inner_chunk.append(f"{arg_params.argument_name}.start_time,")
        body.append(start_time_inner_chunk, indent=1)
        # resources
        resources_inner_chunk = CppChunk()
        resources_inner_chunk.append(f"{self.dial_name}Resources(")
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
        configs_inner_chunk.append(f"{self.dial_name}Configs(")
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
        states_inner_chunk.append(f"{self.dial_name}States(")
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
        conditions_inner_chunk.append(f"{self.dial_name}Conditions(")
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
        inputs_inner_chunk.append(f"{self.dial_name}Inputs(")
        if self.inputs:
            inputs_inner_chunk.append(
                [
                    f"{make_obj_ptr(ipt.render_get(arg_inputs.argument_name))}{',' if (idx < len(self.inputs) - 1) else ''}"
                    for idx, ipt in enumerate(self.inputs)
                ],
                indent=1,
            )
        inputs_inner_chunk.append("),")
        body.append(inputs_inner_chunk, indent=1)
        # outputs
        outputs_inner_chunk = CppChunk()
        outputs_inner_chunk.append(f"{self.dial_name}Outputs(")
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
            diagnostics_inner_chunk.append(f"{self.dial_name}Diagnostics(")
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

        signals_inner_chunk = CppChunk()
        signals_inner_chunk.append(f"{arg_signals.argument_name}")
        body.append(signals_inner_chunk, indent=1)

        body.append(");")
        body.context.add_includes(
            [
                SystemHeader("utility"),
                SystemHeader("tuple"),
            ]
        )

        arguments = [
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

        make_dial_method = CppMethod(
            name="make_dial",
            doc=None,
            return_type=return_type,
            arguments=arguments,
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=True,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace if self.cpp_namespace is not None else ""  # pyright: ignore[reportUnnecessaryComparison] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        return make_dial_method.render(parent_class=parent_type, enclosing_namespace=enclosing_namespace)

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
        return get_conditions_mask_method.render(parent_class=parent_type, enclosing_namespace=enclosing_namespace)

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
        return populate_trigger_mask_method.render(parent_class=parent_type, enclosing_namespace=enclosing_namespace)

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
        return populate_input_telemetry_metrics_method.render(
            parent_class=parent_type, enclosing_namespace=enclosing_namespace
        )

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
        return populate_input_event_metrics_method.render(
            parent_class=parent_type, enclosing_namespace=enclosing_namespace
        )

    def _generate_populate_output_event_metrics_method(self) -> CppModuleChunks:
        """Generate populate_output_event_metrics."""
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
        return populate_output_event_metrics_method.render(
            parent_class=parent_type, enclosing_namespace=enclosing_namespace
        )

    def _generate_populate_output_telemetry_metrics_method(self) -> CppModuleChunks:
        """Generate populate_output_telemetry_metrics."""
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
        return populate_output_telemetry_metrics_method.render(
            parent_class=parent_type, enclosing_namespace=enclosing_namespace
        )

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
        return populate_event_metrics_method.render(parent_class=parent_type, enclosing_namespace=enclosing_namespace)

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
        return populate_telemetry_metrics_method.render(
            parent_class=parent_type, enclosing_namespace=enclosing_namespace
        )

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
        return publish_report_groups_method.render(parent_class=parent_type, enclosing_namespace=enclosing_namespace)

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
        return execute_method.render(parent_class=parent_type, enclosing_namespace=enclosing_namespace)

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
        cpp_mod.append(start_method.render(parent_class=ctx.parent_type, enclosing_namespace=ctx.enclosing_namespace))

        end_body = CppChunk()
        if has_report_groups:
            for group_name in all_group_names:
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
        cpp_mod.append(end_method.render(parent_class=ctx.parent_type, enclosing_namespace=ctx.enclosing_namespace))

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
            cpp_mod.append(fwd_start.render(parent_class=ctx.parent_type, enclosing_namespace=ctx.enclosing_namespace))

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
            cpp_mod.append(fwd_end.render(parent_class=ctx.parent_type, enclosing_namespace=ctx.enclosing_namespace))

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
            cpp_mod.append(fwd_sp.render(parent_class=ctx.parent_type, enclosing_namespace=ctx.enclosing_namespace))

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
            cpp_mod.append(fwd_pop.render(parent_class=ctx.parent_type, enclosing_namespace=ctx.enclosing_namespace))

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
            cpp_mod.append(fwd_reset.render(parent_class=ctx.parent_type, enclosing_namespace=ctx.enclosing_namespace))

        return cpp_mod

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
