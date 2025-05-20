# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Facilities for generating C++ Dial structs."""

from copy import deepcopy
from dataclasses import dataclass

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
    State,
    StatesStruct,
)
from clockwork.dsl.cpp.context import CppChunk, CppModuleChunks, Header
from clockwork.dsl.cpp.types import (
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
from clockwork.dsl.ir.module_id import CLK_REPO, JEWELS_REPO


@dataclass
class DialMember:
    """Helper class to format the dial common members types (ctor arg, private field, accessor, etc)."""

    name: str
    cpp_type: CppType | CppTemplateType

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
        if not self.is_object_ptr():
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
            trailing_qualifiers=["const"] if self.cpp_type.const else [],
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
                    for value in InputsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.inputs).inputs.values()
                    if not value.no_dial
                ],
            ),
            "outputs": make_struct(
                name=f"{self.class_name}Outputs",
                ir_fields=list(OutputsStruct.from_ir(self.cog_ir.module.context, self.cog_ir.outputs).outputs.values()),
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

        enclosing_namespace = self.cpp_namespace if self.cpp_namespace else ""

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
        namespace_resolved = self.cpp_namespace if self.cpp_namespace else ""
        cpp_mod.header_chunk.append(
            [
                "/// Forward declare ///",
                f"void execute_cog({dial_type.render(namespace_resolved)} /*dial*/);",
            ]
        )
        return cpp_mod
