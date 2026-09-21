# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Facilities for generating C++ Dial structs."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass, replace
from typing import TYPE_CHECKING

from clockwork.dsl.cog.codegen_helpers import (
    ENDPOINT_UUID_TYPE,
    UuidHandler,
    formatted_struct,
    gen_const_str,
    get_cog_instantiation_args,
    get_rendered_cog_instantiation_args,
    get_template_args,
    get_template_params,
    to_camel,
)
from clockwork.dsl.cog.cppdial_signals import make_signal_api_struct
from clockwork.dsl.cog.pycog import (
    ConditionBase,
    ConditionsStruct,
    Config,
    ConfigsStruct,
    Diagnostics,
    DiagnosticsStruct,
    DynamicTimerCondition,
    Input,
    InputsStruct,
    Output,
    OutputsStruct,
    Resource,
    ResourcesStruct,
    SignalsStruct,
    State,
    StatesStruct,
)
from clockwork.dsl.cpp.context import CppChunk, CppModuleChunks, Header, SystemHeader
from clockwork.dsl.cpp.typereg import CLOCKWORK_NAMESPACE
from clockwork.dsl.cpp.types import (
    BOOLEAN,
    CppConstructor,
    CppMethod,
    CppNamedType,
    CppNamedValue,
    CppStruct,
    CppTemplate,
    CppTemplateParam,
    CppTemplateType,
    CppType,
    CppTypeExpr,
    CppValue,
    CppValueExpr,
    Ref,
)
from clockwork.dsl.ir.diagnostics import infra_defs_header_from_dial_header
from clockwork.dsl.ir.module_id import CLK_REPO, JEWELS_REPO

if TYPE_CHECKING:
    from clockwork.dsl.ir import aligner as aligner_ir
    from clockwork.dsl.ir import cog


@dataclass
class DynamicTimerCodegenHandler:
    """Manage DynamicTimer condition codegen (for aligner optional input timeouts)."""

    name: str
    endpoint_id: UuidHandler
    policy_name: str
    cog_name: str
    index: int

    @classmethod
    def make(
        cls: type[DynamicTimerCodegenHandler],
        condition: DynamicTimerCondition,
        cog_name: str,
        index: int,
    ) -> DynamicTimerCodegenHandler:
        """Make a DynamicTimerCodegenHandler instance."""
        endpoint_id = UuidHandler(ENDPOINT_UUID_TYPE, condition.uuid)
        name = condition.identifier
        policy_name = to_camel(name) + "Policy"
        return cls(
            name=name,
            endpoint_id=endpoint_id,
            policy_name=policy_name,
            cog_name=cog_name,
            index=index,
        )

    def render_policy_struct(self) -> CppChunk:
        """Render the DynamicTimer Policy struct.

        Note: This method does not use self.index. Callers may pass index=0 when
        creating a DynamicTimerCodegenHandler solely for policy struct rendering.

        struct OptionalTimerPolicy
        {
            using HandlerType = ::clockwork::DynamicTimerHandler<OptionalTimerPolicy>;
            static constexpr auto endpoint_id = ***;
            static constexpr ::std::string_view name = ***;
        }
        """
        chunk = CppChunk()
        chunk.context.add_includes(
            [
                Header(CLK_REPO, "clockwork/cog/dynamic_timer_handler.hh"),
                Header(CLK_REPO, "clockwork/common/process_description_clk_cc.hh"),
                Header(JEWELS_REPO, "jewels/uuid/uuid.hh"),
                SystemHeader("string_view"),
            ]
        )
        body = [
            f"using HandlerType = ::{CLOCKWORK_NAMESPACE}::DynamicTimerHandler<{self.policy_name}>;",
            f"static constexpr auto endpoint_id = {self.endpoint_id.render_from_string_func()};",
            gen_const_str([self.cog_name, self.policy_name]),
        ]
        chunk.append(formatted_struct(self.policy_name, body))
        return chunk

    def render_get(self, arg_timers: str) -> str:
        """Generate get expression."""
        return f"::std::get<{self.index}>({arg_timers})"


@dataclass
class DialMember:
    """Helper class to format the dial common members types (ctor arg, private field, accessor, etc)."""

    name: str
    template_args: list[CppTypeExpr | CppValueExpr] | None
    cpp_type: CppType | CppTemplateType
    store_by_reference: bool = False
    generate_const_accessor: bool = True
    no_accessor: bool = False
    multi_connect_elements: list[str] | None = None

    def arg_name(self) -> str:
        """Get the argument name."""
        return self.name

    def field_name(self) -> str:
        """Get the private field name."""
        return f"{self.arg_name()}_"

    def is_object_ptr(self) -> bool:
        """Return true if this type is an object ptr."""
        return isinstance(self.cpp_type, CppTemplateType) and self.cpp_type.template_name == "ObjectPtr"

    def ctor_arg(self) -> CppNamedType:
        """Return the ctor named type."""
        if self.template_args and isinstance(self.cpp_type, CppType):
            return CppNamedType(
                argument_type=CppTemplate([], self.cpp_type.type_name, None).instantiate(self.template_args),
                argument_name=self.name,
            )
        return CppNamedType(
            argument_type=self.cpp_type,
            argument_name=self.name,
        )

    def ctor_init(self) -> tuple[str, str]:
        """Return the ctor initializer for this field."""
        return (self.field_name(), self.arg_name())

    def public_accessors(self) -> list[CppMethod]:
        """Return the C++ methods for the public accessors."""
        if self.no_accessor:
            return []
        if self.store_by_reference or not self.is_object_ptr():
            return_type = deepcopy(self.cpp_type)
        elif (
            isinstance(self.cpp_type, CppTemplateType)
            and isinstance(self.cpp_type.arguments, list)
            and len(self.cpp_type.arguments) == 1
            and isinstance(self.cpp_type.arguments[0], CppType | CppTemplateType)
        ):
            return_type = deepcopy(self.cpp_type.arguments[0])
        else:
            msg = "Unexpected type during dial construction: expected ObjectPtr<T> or T."
            raise RuntimeError(msg)

        if self.template_args and isinstance(return_type, CppType):
            return_type = CppTemplate([], return_type.type_name, None).instantiate(self.template_args)

        body = CppChunk()

        if self.multi_connect_elements is not None:
            return_type_is_const = return_type.const
            return_type = CppTemplateType(
                include=[SystemHeader("functional")],
                template_name="reference_wrapper",
                cpp_namespace="std",
                arguments=[return_type],
            )
            return_type = CppTemplateType(
                include=[SystemHeader("array")],
                template_name="array",
                cpp_namespace="std",
                arguments=[return_type, CppValue(None, f"{len(self.multi_connect_elements)}")],
                const=return_type_is_const,
            )
            return_values = ", ".join(
                [f"{'*' if self.is_object_ptr() else ''}{element}_" for element in self.multi_connect_elements]
            )
            body.append(f"return {{{return_values}}};")
        else:
            return_type.ref = Ref.L
            body.append(f"return {'*' if self.is_object_ptr() else ''}{self.field_name()};")

        accessor = CppMethod(
            name=f"get_{self.arg_name()}",
            doc=f"Get {self.arg_name()}.",
            return_type=return_type,
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=["const"] if return_type.const else [],
            body=body,
            no_discard=True,
        )

        if "const" in accessor.trailing_qualifiers or not self.generate_const_accessor:
            return [accessor]

        const_return_type = deepcopy(return_type)
        const_return_type.const = True
        return [
            accessor,
            replace(
                accessor,
                return_type=const_return_type,
                trailing_qualifiers=["const"],
            ),
        ]

    def private_field(self) -> CppNamedValue:
        """Return the named value for the private field."""
        if self.template_args and isinstance(self.cpp_type, CppType):
            return CppNamedValue(
                named_type=CppNamedType(
                    argument_type=CppTemplate([], self.cpp_type.type_name, None).instantiate(self.template_args),
                    argument_name=self.field_name(),
                ),
                value=None,
                doc=f"{self.arg_name()}.",
                render_initializer=False,
            )

        return CppNamedValue(
            named_type=CppNamedType(argument_type=self.cpp_type, argument_name=self.field_name()),
            value=None,
            doc=f"{self.arg_name()}.",
            render_initializer=False,
        )


def to_object_ptr(cpp_type: CppType | CppTemplateType) -> CppTemplateType:
    """Convert the CppType to an ObjectPtr wrapper (if not already one)."""
    template_name = "ObjectPtr"
    if isinstance(cpp_type, CppTemplateType) and cpp_type.template_name == template_name:
        return cpp_type
    return CppTemplateType(
        include=[Header(JEWELS_REPO, "jewels/memory/pointers.hh")],
        template_name=template_name,
        cpp_namespace="jewels::memory",
        arguments=[cpp_type],
        const=False,
    )


def to_user_facing_dial_type(
    input_ir: Input,
    resolved_input: aligner_ir.ResolvedAlignerInput | None = None,
) -> CppTemplateType:
    """Create the user-facing MessageInputDial type for aligned upstream inputs.

    The resolved view is a narrowed slice of the internal buffer, so max_view_size
    must match the internal type's buffer capacity. min_msgs and min_new_msgs vary
    based on batch/optional/reuse properties.
    """
    if resolved_input is not None:
        batch_size = resolved_input.batch_size
        is_optional = resolved_input.optional
        is_reuse = resolved_input.reuse
        batch_lo = batch_size[0] if batch_size is not None else 1

        if is_optional:
            min_msgs = 0
            min_new_msgs = 0
        elif is_reuse:
            min_msgs = batch_lo
            min_new_msgs = 0
        else:
            min_msgs = batch_lo
            min_new_msgs = batch_lo
    else:
        min_msgs = 1
        min_new_msgs = 1

    return CppTemplateType(
        include=[Header(CLK_REPO, "clockwork/dial/msg_input.hh")],
        template_name="MessageInputDial",
        cpp_namespace=CLOCKWORK_NAMESPACE,
        arguments=[
            input_ir.msg_type,
            CppValue(None, f"{input_ir.max_msgs}U"),  # max_view_size
            CppValue(None, f"{min_msgs}U"),  # min_msgs
            CppValue(None, f"{min_new_msgs}U"),  # min_new_msgs
            CppValue(None, "false"),  # manual_cursor
            CppValue(None, "true"),  # expose_seqno
            CppValue(None, "false"),  # use_device_ptr
        ],
        const=True,
    )


@dataclass
class Dial:
    """Representation of a C++ Dial."""

    cog_ir: cog.Cog
    dial_header: Header
    class_name: str
    cpp_namespace: str | None
    _template_params: list[CppTemplateParam] | None = None
    _template_args: list[CppTypeExpr | CppValueExpr] | None = None

    def _get_template_params(self) -> list[CppTemplateParam]:
        if self._template_params is None:
            self._template_params = get_template_params(self.cog_ir.module.context, self.cog_ir.parameters)
        return self._template_params

    def _get_template_args(self) -> list[CppTypeExpr | CppValueExpr]:
        if self._template_args is None:
            self._template_args = get_template_args(self.cog_ir.parameters)
        return self._template_args

    def _make_struct(
        self,
        name: str,
        ir_fields: list[Resource]
        | list[Config]
        | list[State]
        | list[ConditionBase]
        | list[Input]
        | list[Output]
        | list[Diagnostics],
        *,
        generate_const_accessors: bool = True,
    ) -> CppStruct:
        """Make the CppStruct type to represent the dial substruct (resource, config, state, conditions, input, outputs)."""
        members = [
            DialMember(
                field.identifier,
                None,
                to_object_ptr(field.cpp_type),
                generate_const_accessor=generate_const_accessors,
            )
            for field in ir_fields
        ]
        return self._build_struct_from_members(name, members)

    def _build_struct_from_members(
        self, name: str, members: list[DialMember], multi_connect_members: list[DialMember] | None = None
    ) -> CppStruct:
        """Build a CppStruct from pre-built DialMember objects."""
        ctor_args = [member.ctor_arg() for member in members]
        ctor_init = [member.ctor_init() for member in members]
        public_accessors = [accessor for member in members for accessor in member.public_accessors()]
        if multi_connect_members:
            for multi_connect_member in multi_connect_members:
                public_accessors.extend(multi_connect_member.public_accessors())
        private_fields = [member.private_field() for member in members]

        ctor = CppConstructor(
            doc="Constructor.",
            arguments=ctor_args,
            leading_qualifiers=[],
            trailing_qualifiers=[],
            member_init_list=ctor_init,
            body=CppChunk(),
        )

        struct = CppStruct(name=CppType(includes=[], type_name=name, cpp_namespace=None), doc=name)
        struct.template_param = self._get_template_params()
        struct.template_args = self._get_template_args()
        struct.public.append(ctor)
        struct.public.extend(public_accessors)
        struct.private.extend(private_fields)
        return struct

    def render(self) -> CppModuleChunks:
        """Render the cpp dial."""
        sync_time_type = CppType(
            includes=[Header(JEWELS_REPO, "jewels/time/sync_time.hh")],
            type_name="SyncTime",
            cpp_namespace="jewels::time",
        )

        cond_struct = ConditionsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.conditions)

        inputs_struct_ir = InputsStruct.from_ir(
            self.cog_ir.module.context,
            self.cog_ir.inputs,
            self.cog_ir.execution_spec,
            aligned_input_defs=self.cog_ir.aligned_inputs,
            expanded_aligned_input_defs=self.cog_ir.expanded_aligned_input_defs,
        )

        aligned_substructs, inputs_substruct = self._build_inputs_substructs(inputs_struct_ir)

        substructs: dict[str, CppStruct] = {
            "resources": self._make_struct(
                name=f"{self.class_name}Resources",
                ir_fields=list(
                    ResourcesStruct.from_ir(self.cog_ir.module.context, self.cog_ir.resources).resources.values()
                ),
                # there is no point having a const accessor for a memory resource
                generate_const_accessors=False,
            ),
            "configs": self._make_struct(
                name=f"{self.class_name}Configs",
                ir_fields=list(ConfigsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.configs).configs.values()),
            ),
            "states": self._make_struct(
                name=f"{self.class_name}States",
                ir_fields=list(StatesStruct.from_ir(self.cog_ir.module.context, self.cog_ir.states).states.values()),
            ),
            "conditions": self._make_struct(
                name=f"{self.class_name}Conditions",
                ir_fields=list(cond_struct.conditions.values()),
            ),
        }
        # Aligned nested structs (e.g., ConsumerCogDialAlignedInputs) are rendered before the
        # main Inputs struct in the header because the Inputs struct references them as member types.
        extra_substructs: list[CppStruct] = list(aligned_substructs.values())
        substructs["inputs"] = inputs_substruct
        substructs["outputs"] = self._make_struct(
            name=f"{self.class_name}Outputs",
            ir_fields=list(
                OutputsStruct.from_ir(
                    self.cog_ir.module.context, self.cog_ir.outputs, self.cog_ir.rate_limits
                ).outputs.values()
            ),
        )

        dial = CppStruct(
            name=CppType(includes=[], type_name=self.class_name, cpp_namespace=None),
            doc=f"{self.class_name}",
        )
        dial.template_param = self._get_template_params()
        dial.template_args = self._get_template_args()

        members = [DialMember(name="start_time", template_args=None, cpp_type=sync_time_type)] + [
            DialMember(name=name, template_args=self._get_template_args(), cpp_type=struct.name)
            for name, struct in substructs.items()
        ]

        diagnostics = DiagnosticsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.diagnostics).diagnostics
        if len(diagnostics) == 1:
            members.append(
                DialMember(
                    name="diagnostics",
                    template_args=None,
                    cpp_type=to_object_ptr(next(iter(diagnostics.values())).reporter_type),
                )
            )
        else:
            substructs["diagnostics"] = self._make_struct(
                name=f"{self.class_name}Diagnostics",
                ir_fields=list(diagnostics.values()),
            )
            members.append(
                DialMember(
                    name="diagnostics", template_args=self._get_template_args(), cpp_type=substructs["diagnostics"].name
                )
            )

        policy_template_params = ", ".join(
            param.render(self.cpp_namespace or "") for param in self._get_template_params()
        )
        signals_substruct, signals_member = _make_signals_substruct_and_member(
            class_name=self.class_name,
            cog_ir=self.cog_ir,
            policy_template_params=policy_template_params,
        )
        substructs["signals"] = signals_substruct
        members.append(signals_member)

        self._add_infra_faults_method(dial)

        timer_policy_chunk = self._build_dynamic_timer_member(cond_struct, members)

        ctor_args = [member.ctor_arg() for member in members]
        ctor_init = [member.ctor_init() for member in members]
        public_accessors = [accessor for member in members for accessor in member.public_accessors()]
        private_fields = [member.private_field() for member in members]

        ctor = CppConstructor(
            doc="Constructor.",
            arguments=ctor_args,
            leading_qualifiers=[],
            trailing_qualifiers=[],
            member_init_list=ctor_init,
            body=CppChunk(),
        )

        dial.public.append(ctor)
        dial.public.extend(public_accessors)
        dial.private.extend(private_fields)

        enclosing_namespace = self.cpp_namespace or ""

        chunks = CppModuleChunks()
        chunks.header_chunk.context.add_include(Header(CLK_REPO, "clockwork/dial/include_common.hh"))
        chunks.inline_chunk.context.add_include(Header(CLK_REPO, "clockwork/dial/include_common.hh"))
        chunks.implementation_chunk.context.add_include(Header(CLK_REPO, "clockwork/dial/include_common.hh"))

        # Render aligned nested structs first so they're defined before the Inputs struct uses them
        for extra in extra_substructs:
            chunks.append(extra.render(enclosing_namespace))
        for substruct in substructs.values():
            chunks.append(substruct.render(enclosing_namespace))
        if timer_policy_chunk is not None:
            timer_policy_chunks = CppModuleChunks()
            timer_policy_chunks.header_chunk.append(timer_policy_chunk)
            chunks.append(timer_policy_chunks)
        chunks.append(dial.render(enclosing_namespace))
        chunks.append(self._render_forward_decl_exec_func())

        return chunks

    def _add_infra_faults_method(self, dial: CppStruct) -> None:
        """Add the has_infra_faults() constexpr method to the dial struct."""
        infra_diag_header = infra_defs_header_from_dial_header(self.dial_header)
        infra_diag_header = replace(infra_diag_header, iwyu_pragma="IWYU pragma: keep")
        infra_diag_check = CppChunk()
        infra_diag_check.append("return false;")
        dial.public.append(
            CppMethod(
                name="has_infra_faults",
                doc="Indicates if the infra fault thresholds header was found and thus if the cog is sending infra faults.",
                return_type=BOOLEAN,
                arguments=[],
                leading_qualifiers=["constexpr"],
                trailing_qualifiers=[],
                body=infra_diag_check,
                no_discard=True,
                static=True,
            )
        )

    def _build_inputs_substructs(
        self,
        inputs_struct_ir: InputsStruct,
    ) -> tuple[dict[str, CppStruct], CppStruct]:
        """Build aligned input nested structs and the main Inputs substruct.

        Returns (aligned_substructs, inputs_substruct).
        """
        aligned_substructs: dict[str, CppStruct] = {}
        for aligned in inputs_struct_ir.aligned_inputs:
            members = [
                DialMember(
                    field.identifier,
                    None,
                    to_user_facing_dial_type(field, aligned.resolved_aligner_inputs.get(field.identifier)),
                )
                for field in aligned.upstream_inputs
            ]
            aligned_substructs[aligned.group_name] = self._build_struct_from_members(
                name=f"{self.class_name}{to_camel(aligned.group_name)}Inputs",
                members=members,
            )

        inputs_members = []
        for field in inputs_struct_ir.inputs.values():
            if field.no_dial:
                continue
            inputs_members.append(
                DialMember(
                    field.identifier,
                    None,
                    to_object_ptr(field.cpp_type),
                    no_accessor=field.no_accessor,
                )
            )
        inputs_multi_connect_members = [
            DialMember(
                field.identifier, None, to_object_ptr(field.cpp_type), multi_connect_elements=field.input_elements
            )
            for field in inputs_struct_ir.multi_connect_inputs.values()
        ]
        for group_name, aligned_struct in aligned_substructs.items():
            inputs_members.append(DialMember(group_name, None, aligned_struct.name))
        inputs_substruct = self._build_struct_from_members(
            f"{self.class_name}Inputs", inputs_members, inputs_multi_connect_members
        )

        return aligned_substructs, inputs_substruct

    def _build_dynamic_timer_member(
        self,
        cond_struct: ConditionsStruct,
        members: list[DialMember],
    ) -> CppChunk | None:
        """Build the dynamic timer policy struct and dial member, if the cog has a DynamicTimer condition.

        The timer policy struct is rendered in the dial header (not the cog header) so that
        AlignerTimerControl can directly reference the concrete DynamicTimerHandler<Policy>
        type without type-erasure.

        Returns the CppChunk for the timer policy struct, or None if no dynamic timer is present.
        """
        dynamic_timer_conds = [c for c in cond_struct.conditions.values() if isinstance(c, DynamicTimerCondition)]
        if not dynamic_timer_conds:
            return None

        assert len(dynamic_timer_conds) == 1, f"Multiple DynamicTimerConditions found: {len(dynamic_timer_conds)}"
        dtc = dynamic_timer_conds[0]
        timer_codegen = DynamicTimerCodegenHandler.make(
            condition=dtc,
            cog_name=self.cog_ir.name,
            index=0,  # index is irrelevant for policy struct rendering
        )
        timer_policy_chunk = timer_codegen.render_policy_struct()
        handler_type = f"::clockwork::DynamicTimerHandler<{timer_codegen.policy_name}>"
        timer_control_type = CppType(
            includes=[
                Header(CLK_REPO, "clockwork/aligner/timer_control.hh"),
                Header(CLK_REPO, "clockwork/cog/dynamic_timer_handler.hh"),
            ],
            type_name=f"AlignerTimerControl<{handler_type}>",
            cpp_namespace="clockwork::aligner",
            const=False,
            ref=None,
        )
        members.append(DialMember(name="optional_timer", template_args=None, cpp_type=timer_control_type))
        return timer_policy_chunk

    def _render_forward_decl_exec_func(self) -> CppModuleChunks:
        cpp_mod = CppModuleChunks()
        namespace_resolved = self.cpp_namespace or ""
        if self.cog_ir.parameters:
            param_str = ", ".join(param.render(namespace_resolved) for param in self._get_template_params())
            args_str = ", ".join(param_name for param_name in self.cog_ir.parameters)
            cpp_mod.header_chunk.append(
                [
                    "/// Forward declare ///",
                    "template <typename T>",
                    f"struct Is{self.class_name}Type : std::false_type {{}};",
                    f"template <{param_str}>",
                    f"struct Is{self.class_name}Type<{self.class_name}<{args_str}>> : std::true_type {{}};",
                    "template <typename T>",
                    f"concept {self.class_name}Type = Is{self.class_name}Type<T>::value;",
                    f"template <{self.class_name}Type T>",
                    "void execute_cog(T& /*dial*/);",
                ]
            )
        else:
            dial_type = CppType([], f"{self.class_name}", self.cpp_namespace)
            dial_type.ref = Ref.L
            cpp_mod.header_chunk.append(
                [
                    "/// Forward declare ///",
                    f"void execute_cog({dial_type.render(namespace_resolved)} /*dial*/);",
                ]
            )
        return cpp_mod


def _make_signals_substruct_and_member(
    class_name: str,
    cog_ir: cog.Cog,
    policy_template_params: str,
) -> tuple[CppStruct, DialMember]:
    """Create the signals substruct and dial member for the signal API.

    Args:
        class_name: The name of the cog class (used for naming the SignalApi struct).
        cog_ir: The cog IR containing report groups.
        policy_template_params: Policy template parameters.

    Returns:
        A tuple of (substruct, dial_member) for the signals API.
    """
    signals_struct = SignalsStruct.from_ir(
        cog_ir.module.context, cog_ir.report_groups | cog_ir.cog_metrics_report_groups
    )
    has_batched_signals = any(signal.is_batched for signal in signals_struct.signals.values())
    has_post_agg_signals = any(not signal.is_batched for signal in signals_struct.signals.values())

    policy_class_name = f"{cog_ir.name}Policy"

    if has_batched_signals or has_post_agg_signals:
        signal_api_struct = make_signal_api_struct(
            name=f"{class_name}SignalApi",
            signals_struct=signals_struct,
            policy_class_name=policy_class_name,
            policy_template_params=policy_template_params,
        )
    else:
        signal_api_struct = CppStruct(
            name=CppType(includes=[], type_name=f"{class_name}SignalApi", cpp_namespace=None),
            doc="Empty SignalApi (no signals).",
        )

    signal_api_ref_type = deepcopy(signal_api_struct.name)
    signal_api_ref_type.ref = Ref.L
    member = DialMember(name="signals", template_args=None, cpp_type=signal_api_ref_type, store_by_reference=True)

    return signal_api_struct, member


@dataclass
class InstantiatedDial:
    """Representation of a C++ Dial."""

    instantiation: cog.InstantiatedCog
    dial_header: Header
    class_name: str
    cpp_namespace: str | None

    def render(self) -> CppModuleChunks:
        """Render explicit instantiations for the dial classes."""
        cpp_mod = CppModuleChunks()
        for cpp_arg in get_cog_instantiation_args(self.instantiation):
            cpp_mod.implementation_chunk.context.add_includes(cpp_arg.includes)
        namespace_resolved = self.cpp_namespace or ""
        if self.cpp_namespace:
            cpp_mod.implementation_chunk.append(
                [
                    f"namespace {self.cpp_namespace}",
                    "{",
                ]
            )
        rendered_template_args = get_rendered_cog_instantiation_args(self.instantiation, namespace_resolved)
        if len(self.instantiation.cog_ir.diagnostics) != 1:
            cpp_mod.implementation_chunk.append(
                f"template struct {self.class_name}Diagnostics<{rendered_template_args}>;"
            )

        cpp_mod.implementation_chunk.append(
            [
                f"template struct {self.class_name}Resources<{rendered_template_args}>;",
                f"template struct {self.class_name}Configs<{rendered_template_args}>;",
                f"template struct {self.class_name}States<{rendered_template_args}>;",
                f"template struct {self.class_name}Conditions<{rendered_template_args}>;",
                f"template struct {self.class_name}Inputs<{rendered_template_args}>;",
                f"template struct {self.class_name}Outputs<{rendered_template_args}>;",
                f"template struct {self.class_name}<{rendered_template_args}>;",
            ],
        )
        if self.cpp_namespace:
            cpp_mod.implementation_chunk.append(f"}} // namespace {self.cpp_namespace}")
        return cpp_mod
