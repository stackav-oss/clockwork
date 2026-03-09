# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Facilities for generating C++ Dial structs."""

from copy import deepcopy
from dataclasses import dataclass, replace

from clockwork.dsl.cog.cppdial_signals import make_signal_api_struct
from clockwork.dsl.cog.pycog import (
    ConditionBase,
    ConditionsStruct,
    Config,
    ConfigsStruct,
    Diagnostics,
    DiagnosticsStruct,
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
from clockwork.dsl.cpp.context import CppChunk, CppModuleChunks, Header
from clockwork.dsl.cpp.types import (
    BOOLEAN,
    CppConstructor,
    CppMethod,
    CppNamedType,
    CppNamedValue,
    CppStruct,
    CppTemplateType,
    CppType,
    Ref,
)
from clockwork.dsl.ir import cog
from clockwork.dsl.ir.diagnostics import infra_defs_header_from_dial_header
from clockwork.dsl.ir.module_id import CLK_REPO, JEWELS_REPO


@dataclass
class DialMember:
    """Helper class to format the dial common members types (ctor arg, private field, accessor, etc)."""

    name: str
    cpp_type: CppType | CppTemplateType
    store_by_reference: bool = False

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
        return CppNamedType(
            argument_type=self.cpp_type,
            argument_name=self.name,
        )

    def ctor_init(self) -> tuple[str, str]:
        """Return the ctor initializer for this field."""
        return (self.field_name(), self.arg_name())

    def public_accessor(self) -> CppMethod:
        """Return the cpp method for the public accessor."""
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

        return_type.ref = Ref.L
        body = CppChunk()
        body.append(f"return {'*' if self.is_object_ptr() else ''}{self.field_name()};")

        return CppMethod(
            name=f"get_{self.arg_name()}",
            doc=f"Get {self.arg_name()}.",
            return_type=return_type,
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=["const"] if return_type.const else [],
            body=body,
            no_discard=True,
        )

    def private_field(self) -> CppNamedValue:
        """Return the named value for the private field."""
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


def make_struct(
    name: str,
    ir_fields: list[Resource]
    | list[Config]
    | list[State]
    | list[ConditionBase]
    | list[Input]
    | list[Output]
    | list[Diagnostics],
) -> CppStruct:
    """Make the CppStruct type to represent the dial substruct (resource, config, state, conditions, input, outputs)."""
    members = [DialMember(field.identifier, to_object_ptr(field.cpp_type)) for field in ir_fields]
    ctor_args = [member.ctor_arg() for member in members]
    ctor_init = [member.ctor_init() for member in members]
    public_accessors = [member.public_accessor() for member in members]
    private_fields = [member.private_field() for member in members]

    ctor = CppConstructor(
        doc="Constructor.",
        arguments=ctor_args,
        leading_qualifiers=[],
        trailing_qualifiers=[],
        member_init_list=ctor_init,
        body=CppChunk(),
    )

    struct = CppStruct(name=CppType(includes=[], type_name=f"{name}", cpp_namespace=None), doc=f"{name}")
    struct.public.append(ctor)
    struct.public.extend(public_accessors)
    struct.private.extend(private_fields)
    return struct


@dataclass
class Dial:
    """Representation of a C++ Dial."""

    cog_ir: cog.Cog
    dial_header: Header
    class_name: str
    cpp_namespace: str | None

    def render(self) -> CppModuleChunks:
        """Render the cpp dial."""
        sync_time_type = CppType(
            includes=[Header(JEWELS_REPO, "jewels/time/sync_time.hh")],
            type_name="SyncTime",
            cpp_namespace="jewels::time",
        )

        substructs = {
            "resources": make_struct(
                name=f"{self.class_name}Resources",
                ir_fields=list(
                    ResourcesStruct.from_ir(self.cog_ir.module.context, self.cog_ir.resources).resources.values()
                ),
            ),
            "configs": make_struct(
                name=f"{self.class_name}Configs",
                ir_fields=list(ConfigsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.configs).configs.values()),
            ),
            "states": make_struct(
                name=f"{self.class_name}States",
                ir_fields=list(StatesStruct.from_ir(self.cog_ir.module.context, self.cog_ir.states).states.values()),
            ),
            "conditions": make_struct(
                name=f"{self.class_name}Conditions",
                ir_fields=list(
                    ConditionsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.conditions).conditions.values()
                ),
            ),
            "inputs": make_struct(
                name=f"{self.class_name}Inputs",
                ir_fields=[
                    value
                    for value in InputsStruct.from_ir(
                        self.cog_ir.module.context,
                        self.cog_ir.inputs,
                        self.cog_ir.execution_spec,
                    ).inputs.values()
                    if not value.no_dial
                ],
            ),
            "outputs": make_struct(
                name=f"{self.class_name}Outputs",
                ir_fields=list(
                    OutputsStruct.from_ir(
                        self.cog_ir.module.context, self.cog_ir.outputs, self.cog_ir.rate_limits
                    ).outputs.values()
                ),
            ),
        }

        dial = CppStruct(
            name=CppType(includes=[], type_name=self.class_name, cpp_namespace=None),
            doc=f"{self.class_name}",
        )

        members = [DialMember(name="start_time", cpp_type=sync_time_type)] + [
            DialMember(name=name, cpp_type=struct.name) for name, struct in substructs.items()
        ]

        diagnostics = DiagnosticsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.diagnostics).diagnostics
        if len(diagnostics) == 1:
            members.append(
                DialMember(name="diagnostics", cpp_type=to_object_ptr(next(iter(diagnostics.values())).reporter_type))
            )
        else:
            substructs["diagnostics"] = make_struct(
                name=f"{self.class_name}Diagnostics",
                ir_fields=list(diagnostics.values()),
            )
            members.append(DialMember(name="diagnostics", cpp_type=substructs["diagnostics"].name))

        signals_substruct, signals_member = _make_signals_substruct_and_member(
            class_name=self.class_name,
            cog_ir=self.cog_ir,
        )
        substructs["signals"] = signals_substruct
        members.append(signals_member)

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

        ctor_args = [member.ctor_arg() for member in members]
        ctor_init = [member.ctor_init() for member in members]
        public_accessors = [member.public_accessor() for member in members]
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
        for substruct in substructs.values():
            chunks.append(substruct.render(enclosing_namespace))
        chunks.append(dial.render(enclosing_namespace))
        chunks.append(self._render_forward_decl_exec_func())

        return chunks

    def _render_forward_decl_exec_func(self) -> CppModuleChunks:
        dial_type = CppType([], f"{self.class_name}&", self.cpp_namespace)
        cpp_mod = CppModuleChunks()
        namespace_resolved = self.cpp_namespace or ""
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
) -> tuple[CppStruct, DialMember]:
    """Create the signals substruct and dial member for the signal API.

    Args:
        class_name: The name of the cog class (used for naming the SignalApi struct).
        cog_ir: The cog IR containing report groups.

    Returns:
        A tuple of (substruct, dial_member) for the signals API.
    """
    signals_struct = SignalsStruct.from_ir(cog_ir.module.context, cog_ir.report_groups)
    has_batched_signals = any(signal.is_batched for signal in signals_struct.signals.values())
    has_post_agg_signals = any(not signal.is_batched for signal in signals_struct.signals.values())

    policy_class_name = f"{cog_ir.name}Policy"

    if has_batched_signals or has_post_agg_signals:
        signal_api_struct = make_signal_api_struct(
            name=f"{class_name}SignalApi",
            signals_struct=signals_struct,
            policy_class_name=policy_class_name,
        )
    else:
        signal_api_struct = CppStruct(
            name=CppType(includes=[], type_name=f"{class_name}SignalApi", cpp_namespace=None),
            doc="Empty SignalApi (no signals).",
        )

    signal_api_ref_type = deepcopy(signal_api_struct.name)
    signal_api_ref_type.ref = Ref.L
    member = DialMember(name="signals", cpp_type=signal_api_ref_type, store_by_reference=True)

    return signal_api_struct, member
