# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Nanobind type caster specializations."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Final

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.cpp import context, typereg, types
from clockwork.dsl.cpp.context import CppChunk, Header, SystemHeader
from clockwork.dsl.ir import clkbuiltins, clkenum, expr, node, schema, typesys
from clockwork.dsl.ir.interface import InterfaceAlias, InterfaceInstantiation, InterfaceReference
from clockwork.dsl.ir.module_id import CLK_REPO, JEWELS_REPO
from clockwork.dsl.ir.py_target import PyTarget

NB_NAMESPACE: Final[str] = "nanobind"
NB_DETAIL_NAMESPACE: Final[str] = "nanobind::detail"


@dataclass
class ResolvedNanobindTypeCaster(node.CstNode[cst.NanobindTypeCaster]):
    """A fully resolved nanboind type caster."""

    original_type: InterfaceReference | InterfaceAlias | clkenum.ResolvedEnum
    python_target: PyTarget
    namespace: str


@dataclass
class NanobindTypeCaster(node.CstNode[cst.NanobindTypeCaster]):
    """IR node for nanobind type caster specialiaziations for Tappy types."""

    typespec: expr.Expr | node.Deferrable[InterfaceAlias | clkenum.ClkEnum]
    python_target: node.Deferrable[PyTarget]
    namespace: str
    resolved: ResolvedNanobindTypeCaster | None

    @classmethod
    def from_cst(
        cls: type[NanobindTypeCaster],
        cst_node: cst.NanobindTypeCaster,
        module: node.Module,
        namespace: str,
    ) -> NanobindTypeCaster:
        """Construct a NanobindTypeCaster IR node from a CST node."""
        child_typespec = cst_node.child_typespec()
        typespec: expr.Expr | node.Deferrable[InterfaceAlias | clkenum.ClkEnum] | None = None
        if (identifier := child_typespec.maybe_identifier()) is not None:
            typespec = node.DeferredLookup.make(
                expected_type=InterfaceAlias | clkenum.ClkEnum,  # pyright: ignore[reportArgumentType] Type not known ahead of time
                cst_identifier=identifier,
                terminals=module.terminals,
            )
        else:
            typespec = expr.Expr.from_cst(child_typespec, module)

        assert typespec is not None

        return cls(
            module=module,
            cst_node=cst_node,
            typespec=typespec,
            python_target=node.DeferredLookup.make(
                expected_type=PyTarget,
                cst_identifier=cst_node.child_python_target(),
                terminals=module.terminals,
            ),
            namespace=namespace,
            resolved=None,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if self.resolved:
            msg = self.append_error_line(f"Attempt to resolve nanobind type caster twice: {self}")
            raise RuntimeError(msg)

        original_type: InterfaceReference | InterfaceAlias | clkenum.ResolvedEnum | str | None = None
        if isinstance(self.typespec, expr.Expr):
            typespec = self.typespec.evaluate()
            if isinstance(typespec, typesys.Instantiation):
                original_type = InterfaceReference.from_typespec(typespec)
                if not isinstance(original_type, InterfaceReference) or (
                    original_type.typespec.instantiates is not clkbuiltins.TAP
                    or original_type.representation.typespec.instantiates is not clkbuiltins.TACHYON
                ):
                    msg = self.append_error_line("Nanobind casters over can only be generated for Tap interfraces.")
                    raise TypeError(msg)

                repr_schema = original_type.representation.schema_ir
                if repr_schema.generic_parameters() is not None:
                    msg = self.append_error_line("Nanobind casters with generic schemas must use an alias.")
                    raise ValueError(msg)

        elif isinstance(self.typespec, clkenum.ClkEnum):
            original_type = self.typespec.get_resolved()

        elif isinstance(self.typespec, InterfaceAlias):
            original_type = self.typespec

        else:
            msg = self.append_error_line(f"Cannot generate nanobind casters for {type(self.typespec)}.")
            raise TypeError(msg)

        assert isinstance(original_type, InterfaceReference | InterfaceAlias | clkenum.ResolvedEnum)

        if not isinstance(self.python_target, PyTarget):
            msg = self.append_error_line(f"Invalid type for python target: {type(self.python_target)}")
            raise TypeError(msg)

        self.resolved = ResolvedNanobindTypeCaster(
            module=self.module,
            cst_node=self.cst_node,
            original_type=original_type,
            python_target=self.python_target,
            namespace=self.namespace,
        )

    def get_resolved(self) -> ResolvedNanobindTypeCaster:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)

        return self.resolved


def _render_schema_caster(nanobind_caster: ResolvedNanobindTypeCaster) -> context.CppModuleChunks:  # noqa: PLR0915 TODO(OI-2846)
    """Render the caster into a cpp module."""
    cpp_mod = context.CppModuleChunks()

    interface: InterfaceInstantiation | InterfaceReference | None = None
    assert isinstance(nanobind_caster.original_type, InterfaceAlias | InterfaceReference)
    if isinstance(nanobind_caster.original_type, InterfaceAlias):
        interface = nanobind_caster.original_type.interface
    else:
        interface = nanobind_caster.original_type

    assert interface.representation is not None
    repr_typespec = interface.representation.typespec
    repr_schema = repr_typespec.arguments["schema"]
    assert isinstance(repr_schema, schema.Schema | schema.ResolvedSchema | typesys.Instantiation)
    schema_ir = schema.InstantiatedSchema.from_typespec(repr_schema)
    schema_type = typereg.get_cpp_type(nanobind_caster.module.context, schema_ir)
    assert isinstance(schema_type, types.CppType | types.CppTemplateType)

    cpp_mod.header_chunk.context.add_includes(schema_type.includes)
    schema_cpp_type = schema_type.render("")

    deserialize_fn_call = types.CppFn(
        headers=[Header(JEWELS_REPO, "jewels/nanobind/nanobind_tappy_convert.hh")],
        namespace="jewels",
        name=f"deserialize_tappy_from_py<{schema_cpp_type}>",
    ).invoke(
        [
            types.CppValue(None, "src"),
        ]
    )
    from_python_body = CppChunk()
    from_python_body.append(f"value = {deserialize_fn_call.render(NB_DETAIL_NAMESPACE)};")
    from_python_body.append("return true;")
    from_python_method = types.CppMethod(
        name="from_python",
        doc="Cast the python representation to the Tappy type.",
        return_type=types.BOOLEAN,
        arguments=[
            types.CppNamedType(argument_type=types.CppType([], "handle", NB_NAMESPACE), argument_name="src"),
            types.CppNamedType(argument_type=types.CppType([], "uint8_t", None), argument_name="/*policy*/"),
            types.CppNamedType(
                argument_type=types.CppType([], "cleanup_list", NB_DETAIL_NAMESPACE, ref=types.Ref.POINTER),
                argument_name="/*cleanup*/",
            ),
        ],
        leading_qualifiers=["inline"],
        trailing_qualifiers=[],
        body=from_python_body,
        no_discard=True,
        static=False,
    )

    message_arg_type = typereg.get_cpp_type(nanobind_caster.module.context, interface.typespec)
    cpp_mod.header_chunk.context.add_includes(message_arg_type.includes)
    assert isinstance(message_arg_type, types.CppTemplateType)
    message_arg_type.ref = types.Ref.L
    serialize_fn_call = types.CppFn(
        headers=[Header(JEWELS_REPO, "jewels/nanobind/nanobind_tappy_convert.hh")],
        namespace="jewels",
        name=f"serialize_tappy_to_py<{schema_cpp_type}>",
    ).invoke(
        [
            types.CppValue(None, "message"),
            types.CppValue(None, "module_name"),
            types.CppValue(None, "class_name"),
        ]
    )
    py_module = nanobind_caster.python_target.module.module_id.get_base_path()
    module_parts = (*py_module.parts[:-1], nanobind_caster.python_target.name)
    module_path = ".".join(module_parts)
    from_cpp_body = CppChunk()
    if isinstance(nanobind_caster.original_type, InterfaceAlias):
        class_name = nanobind_caster.original_type.name
    else:
        class_name = schema_ir.schema_name

    from_cpp_body.append(f'const std::string class_name{{"{class_name}"}};')
    from_cpp_body.append(f'const std::string module_name{{"{module_path}"}};')
    from_cpp_body.append(f"return {serialize_fn_call.render(NB_DETAIL_NAMESPACE)};")
    from_cpp_method = types.CppMethod(
        name="from_cpp",
        doc="Cast the Tappy type to the python representation.",
        return_type=types.CppType([], "handle", NB_NAMESPACE),
        arguments=[
            types.CppNamedType(argument_type=message_arg_type, argument_name="message", qualifiers=["const"]),
            types.CppNamedType(
                argument_type=types.CppType([], "rv_policy", NB_NAMESPACE),
                argument_name="/*policy*/",
            ),
            types.CppNamedType(
                argument_type=types.CppType([], "cleanup_list", NB_DETAIL_NAMESPACE, ref=types.Ref.POINTER),
                argument_name="/*cleanup*/",
            ),
        ],
        leading_qualifiers=["inline"],
        trailing_qualifiers=[],
        body=from_cpp_body,
        no_discard=True,
        static=True,
    )

    tappy_type = typereg.get_cpp_type(nanobind_caster.module.context, interface.typespec)
    assert isinstance(tappy_type, types.CppTemplateType)
    cpp_mod.header_chunk.context.add_includes(tappy_type.includes)
    tappy_cpp_type = tappy_type.render(NB_DETAIL_NAMESPACE)
    friendly_tappy_type = tappy_type.render(tappy_type.cpp_namespace)

    nb_preamble = CppChunk()
    caster_type = types.CppStruct(
        name=types.CppTemplateType(
            include=[],
            template_name="type_caster",
            cpp_namespace=NB_DETAIL_NAMESPACE,
            arguments=[tappy_type],
        ),
        doc=f"nanobind caster for {friendly_tappy_type}",
        members={types.MemberAccess.public: [from_python_method, from_cpp_method]},
        leading_header_chunk=nb_preamble,
    )

    # The NB_TYPE_CASTER macro emits method definitions. To keep things
    # hygienic, just put an opaque foward decl in the header and place
    # everything else in the inline chunk.
    nb_preamble.append(
        f'NB_TYPE_CASTER({tappy_cpp_type}, ::{NB_DETAIL_NAMESPACE}::const_name("{module_path}.{class_name}"))'
    )
    cpp_mod.header_chunk.append(f"template <> struct type_caster<{tappy_cpp_type}>; // IWYU pragma: keep")
    caster_chunks = caster_type.render(NB_DETAIL_NAMESPACE)
    cpp_mod.inline_chunk.append(caster_chunks.header_chunk)
    cpp_mod.inline_chunk.append(caster_chunks.inline_chunk)
    return cpp_mod


def _render_enum_caster(nanobind_caster: ResolvedNanobindTypeCaster) -> context.CppModuleChunks:
    cpp_mod = context.CppModuleChunks()

    assert isinstance(nanobind_caster.original_type, clkenum.ResolvedEnum)
    enum_type = typereg.get_cpp_type(nanobind_caster.module.context, nanobind_caster.original_type)
    assert isinstance(enum_type, types.CppType)
    cpp_mod.header_chunk.context.add_includes(enum_type.includes)
    enum_cpp_type = enum_type.render("")

    deserialize_fn_call = types.CppFn(
        headers=[Header(JEWELS_REPO, "jewels/nanobind/nanobind_tappy_convert.hh")],
        namespace="jewels",
        name=f"deserialize_enum_from_py<{enum_cpp_type}>",
    ).invoke(
        [
            types.CppValue(None, "src"),
            types.CppValue(None, "value"),
        ]
    )
    from_python_body = CppChunk()
    from_python_body.append(f"return {deserialize_fn_call.render(NB_DETAIL_NAMESPACE)};")
    from_python_method = types.CppMethod(
        name="from_python",
        doc="Cast the python representation to the enum type.",
        return_type=types.BOOLEAN,
        arguments=[
            types.CppNamedType(argument_type=types.CppType([], "handle", NB_NAMESPACE), argument_name="src"),
            types.CppNamedType(argument_type=types.CppType([], "uint8_t", None), argument_name="/*policy*/"),
            types.CppNamedType(
                argument_type=types.CppType([], "cleanup_list", NB_DETAIL_NAMESPACE, ref=types.Ref.POINTER),
                argument_name="/*cleanup*/",
            ),
        ],
        leading_qualifiers=["inline"],
        trailing_qualifiers=[],
        body=from_python_body,
        no_discard=True,
        static=False,
    )

    serialize_fn_call = types.CppFn(
        headers=[Header(JEWELS_REPO, "jewels/nanobind/nanobind_tappy_convert.hh")],
        namespace="jewels",
        name=f"serialize_enum_to_py<{enum_cpp_type}>",
    ).invoke(
        [
            types.CppValue(None, "cpp_value"),
            types.CppValue(None, "module_name"),
            types.CppValue(None, "enum_name"),
        ]
    )
    py_module = nanobind_caster.python_target.module.module_id.get_base_path()
    module_parts = (*py_module.parts[:-1], nanobind_caster.python_target.name)
    module_path = ".".join(module_parts)
    from_cpp_body = CppChunk()
    from_cpp_body.append(f'const std::string enum_name{{"{nanobind_caster.original_type.name}"}};')
    from_cpp_body.append(f'const std::string module_name{{"{module_path}"}};')
    from_cpp_body.append(f"return {serialize_fn_call.render(NB_DETAIL_NAMESPACE)};")
    from_cpp_method = types.CppMethod(
        name="from_cpp",
        doc="Cast the enum type to the python representation.",
        return_type=types.CppType([], "handle", NB_NAMESPACE),
        arguments=[
            types.CppNamedType(argument_type=enum_type, argument_name="cpp_value"),
            types.CppNamedType(
                argument_type=types.CppType([], "rv_policy", NB_NAMESPACE),
                argument_name="/*policy*/",
            ),
            types.CppNamedType(
                argument_type=types.CppType([], "cleanup_list", NB_DETAIL_NAMESPACE, ref=types.Ref.POINTER),
                argument_name="/*cleanup*/",
            ),
        ],
        leading_qualifiers=["inline"],
        trailing_qualifiers=[],
        body=from_cpp_body,
        no_discard=True,
        static=True,
    )

    nb_preamble = CppChunk()
    caster_type = types.CppStruct(
        name=types.CppTemplateType(
            include=[],
            template_name="type_caster",
            cpp_namespace=NB_DETAIL_NAMESPACE,
            arguments=[enum_type],
        ),
        doc=f"nanobind caster for {enum_cpp_type}",
        members={types.MemberAccess.public: [from_python_method, from_cpp_method]},
        leading_header_chunk=nb_preamble,
    )

    # The NB_TYPE_CASTER macro emits method definitions. To keep things
    # hygienic, just put an opaque foward decl in the header and place
    # everything else in the inline chunk.
    nb_preamble.append(
        f'NB_TYPE_CASTER({enum_cpp_type}, ::{NB_DETAIL_NAMESPACE}::const_name("{module_path}.{enum_type.type_name}"))'
    )
    cpp_mod.header_chunk.append(f"template <> struct type_caster<{enum_cpp_type}>; // IWYU pragma: keep")
    caster_chunks = caster_type.render(NB_DETAIL_NAMESPACE)
    cpp_mod.inline_chunk.append(caster_chunks.header_chunk)
    cpp_mod.inline_chunk.append(caster_chunks.inline_chunk)
    return cpp_mod


def _render_nanobind_caster(nanobind_caster: ResolvedNanobindTypeCaster) -> context.CppModuleChunks:
    """Render the caster into a cpp module."""
    if isinstance(nanobind_caster.original_type, clkenum.ResolvedEnum):
        return _render_enum_caster(nanobind_caster)
    return _render_schema_caster(nanobind_caster)


def render_nanobind_casters(
    nanobind_casters: list[NanobindTypeCaster], enclosing_includes: set[context.Include]
) -> context.CppModuleChunks:
    """Generate C++ module chunks for a set of nanobind casters."""
    cpp_mod = context.CppModuleChunks()
    if len(nanobind_casters) == 0:
        return cpp_mod

    cpp_mod.append("#ifdef CLK_ENABLE_NANOBIND_TYPE_CASTER")

    common_includes = {
        Header(CLK_REPO, "clockwork/repr_iface.hh"),
    }
    cpp_mod.header_chunk.context.add_includes(common_includes)
    cpp_mod.inline_chunk.context.add_includes(common_includes)

    # Append these headers directly to the inline chunk so they are
    # guraded by the ifdef.
    inline_includes: set[context.Include] = {
        Header(JEWELS_REPO, "jewels/nanobind/nanobind_tappy_convert.hh"),
        SystemHeader("string"),
    }
    inline_includes -= enclosing_includes
    for header in sorted(inline_includes):
        cpp_mod.inline_chunk.append(header.render())

    cpp_mod.append(SystemHeader("nanobind/nanobind.h").render())
    cpp_mod.append("NAMESPACE_BEGIN(NB_NAMESPACE)")
    cpp_mod.append("NAMESPACE_BEGIN(detail)")

    for caster in nanobind_casters:
        cpp_mod.append(_render_nanobind_caster(caster.get_resolved()))

    cpp_mod.append("NAMESPACE_END(detail)")
    cpp_mod.append("NAMESPACE_END(NB_NAMESPACE)")
    cpp_mod.append("#endif // CLK_ENABLE_NANOBIND_TYPE_CASTER")

    return cpp_mod
