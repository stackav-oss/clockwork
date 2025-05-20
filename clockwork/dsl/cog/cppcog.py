# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Facilities for generating C++ Cog structs."""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal
from typing import TYPE_CHECKING, Final

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
    State,
    StatesStruct,
    TimeSinceLastExecCondition,
)
from clockwork.dsl.cpp.context import CppChunk, CppModuleChunks, Header, SystemHeader
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
from clockwork.dsl.ir.extern_type import ExternType
from clockwork.dsl.ir.module_id import CLK_REPO, JEWELS_REPO
from pydantic.alias_generators import to_snake

if TYPE_CHECKING:
    from collections.abc import Iterable, Iterator
    from uuid import UUID

    from clockwork.dsl.compiler_context import CompilerContext


_UUID_TEMPLATE = CppTemplate([Header(JEWELS_REPO, "jewels/uuid/uuid/hh")], "Uuid", "jewels")
_COG_CLASS_TYPE = CppType(
    [Header(CLK_REPO, "clockwork/common/process_description.hh")],
    "CogClassId",
    CLOCKWORK_NAMESPACE + "::common",
)
_ENDPOINT_TYPE = CppType(
    [Header(CLK_REPO, "clockwork/common/process_description.hh")],
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
            assert self.msg_type.interface_ir.representation is not None  # noqa: S101 (invariant)
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
           static constexpr auto copy_inputs = ***;
           static constexpr auto manual_cursor = ***;
        };

        """
        chunk = CppChunk()

        chunk.context.add_includes(
            [
                Header(JEWELS_REPO, "jewels/uuid/uuid.hh"),
                SystemHeader("cstdint"),
            ]
        )

        # Parse parameters
        msg_type = self.cog_input.msg_type.render("")
        max_view_size = f"{self.cog_input.max_msgs}U"
        copy_inputs = False
        manual_cursor = f"{self.cog_input.manual_cursor}"

        policy_name = to_camel(self.input_name) + "Policy"
        body = [
            f"using MsgType = {msg_type};",
            f"static constexpr auto endpoint_id = {self.endpoint_id.render_from_string_func()};",
            _gen_const_str([self.cog_name, policy_name]),
            f"static constexpr auto max_view_size = {max_view_size};",
            f"static constexpr auto copy_inputs = {str(copy_inputs).lower()};",
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
            ]
        )
        body = [
            f"using MsgType = {self.msg_type.render('')};",
            f"static constexpr auto endpoint_id = {self.endpoint_id.render_from_string_func()};",
            _gen_const_str([self.cog_name, self.policy_name]),
        ]
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

    def render_policy_struct(self) -> CppChunk:
        """Render States section."""
        chunk = CppChunk()
        chunk.context.add_includes(self.manager_type.includes)
        chunk.append(
            _formatted_struct(
                self.policy_name,
                [
                    f"static constexpr auto endpoint_id = {self.endpoint_id.render_from_string_func()};",
                    _gen_const_str([self.cog_name, self.name]),
                    _gen_const_str(name="member_name", terms=self.name),
                    _gen_const_str(name="group_name", terms=(self.group_id or "")),
                    _gen_const_str(name="instance_name", terms=(self.instance_id or "")),
                    f"using ManagerType = {self.manager_type.render('')};",
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
        """Render Publishers section."""
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

    def render(self) -> CppModuleChunks:
        """Generate header and source skeleton for Cog.

        Special situations:
        - Toolchain adds namespace enclosure, so it will not be explicitly added here.
        """
        cpp_mod = CppModuleChunks()
        cpp_mod.header_chunk.context.add_includes([Header(CLK_REPO, "clockwork/cog/include_common.hh")])
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
            f"using CogDial = {dial_type.render(self.cpp_namespace)};",
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
            InputsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.inputs),
            cog_fqn,
        )
        cpp_mod.header_chunk.append(self.inputs.render_inputs(), indent=1)

        self.input_conditions = InputConditions.make(
            ConditionsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.conditions),
            cog_fqn,
            self.inputs.input_uuid_map,
        )
        cpp_mod.header_chunk.append(self.input_conditions.render_input_conditions(), indent=1)

        self.publishers = Publishers.make(
            OutputsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.outputs),
            cog_fqn,
        )
        cpp_mod.header_chunk.append(self.publishers.render_publishers(), indent=1)

        self.diagnostics = Diagnosticses.make(
            DiagnosticsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.diagnostics), cog_fqn
        )
        cpp_mod.header_chunk.append(self.diagnostics.render_diagnostics(), indent=1)

        cpp_mod.append(self._generate_is_ready_method())
        cpp_mod.append(self._generate_make_dial_method())
        cpp_mod.append(self._generate_execute_method())

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
        enclosing_namespace = self.cpp_namespace if self.cpp_namespace is not None else ""  # pyright: ignore[reportUnnecessaryComparison] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        return is_ready_method.render(parent_class=parent_type, enclosing_namespace=enclosing_namespace)

    def _generate_make_dial_method(self) -> CppModuleChunks:  # noqa: PLR0915, C901 naturally large with no easy breaks
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
            "publishables" if self.publishers and len(self.publishers) else "/*publishables*/",
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
            outputs_inner_chunk.append(
                [
                    f"{make_obj_ptr(publisher.render_get(arg_publishables.argument_name))}{',' if (idx < len(self.publishers) - 1) else ''}"
                    for idx, publisher in enumerate(self.publishers)
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
        body.append(diagnostics_inner_chunk, indent=1)

        body.append(");")
        body.context.add_includes(
            [
                SystemHeader("utility"),
                SystemHeader("tuple"),
            ]
        )

        make_dial_method = CppMethod(
            name="make_dial",
            doc=None,
            return_type=return_type,
            arguments=[
                arg_params,
                arg_resources,
                arg_configs,
                arg_states,
                arg_inputs,
                arg_publishables,
                arg_timer_conditions,
                arg_input_conditions,
                arg_diagnostics,
            ],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=body,
            no_discard=True,
            static=True,
        )

        parent_type = CppType([], self.cog_policy_name, self.cpp_namespace)
        enclosing_namespace = self.cpp_namespace if self.cpp_namespace is not None else ""  # pyright: ignore[reportUnnecessaryComparison] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        return make_dial_method.render(parent_class=parent_type, enclosing_namespace=enclosing_namespace)

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
