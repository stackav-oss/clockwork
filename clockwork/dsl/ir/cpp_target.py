# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""CppTarget-related IR nodes."""

from __future__ import annotations

from dataclasses import dataclass, field
from itertools import chain
from typing import TYPE_CHECKING, cast

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.bazel.targets import Label
from clockwork.dsl.cog.cpp_test_cog import CppTestCog as CppTestCogGenerator
from clockwork.dsl.cog.cppcog import to_dial_name
from clockwork.dsl.cog.cppdial import Dial as DialGenerator
from clockwork.dsl.cpp import literal, typereg, types
from clockwork.dsl.cpp.context import (
    CppModuleChunks,
    Header,
    SystemHeader,
    as_cc_library,
    comment_doc_string,
    write_to_file,
)
from clockwork.dsl.ir import (
    audio,
    clkbuiltins,
    clkenum,
    cog,
    converter,
    cpp_extern,
    expr,
    extern_type,
    node,
    primitive,
    schema,
    statement,
    strongtypes,
    typesys,
    udp,
)
from clockwork.dsl.ir.cog import Cog
from clockwork.dsl.ir.cpp_executable import CppAudioSource, CppCog, CppPythonCog, CppUdpSocket
from clockwork.dsl.ir.cst_util import get_span
from clockwork.dsl.ir.interface import InterfaceAlias, InterfaceInstantiation
from clockwork.dsl.ir.module_id import JEWELS_REPO
from clockwork.dsl.ir.nanobind_type_casters import NanobindTypeCaster, render_nanobind_casters
from clockwork.dsl.ir.path_resolver import BazelPathResolver
from clockwork.dsl.ir.representation import (
    ReprInstantiation,
    ResolvedReprInstantiation,
)
from clockwork.dsl.ir.statement import ImmutableBinding
from clockwork.dsl.serialization.tap import to_cpp_struct

if TYPE_CHECKING:
    from pathlib import Path

    from clockwork.dsl.bazel.cc_targets import CcLibrary
    from clockwork.dsl.compiler_context import CompilerContext


def _render_cpp_constant(binding: ImmutableBinding) -> types.CppNamedValue:
    assert isinstance(binding.type_info, typesys.InferenceVar)
    resolution = binding.type_info.resolution()
    if not isinstance(resolution, typesys.TypeVal):
        msg = binding.append_error_line("Variable type has not been constrained")
        raise TypeError(msg)

    cpp_type = (
        types.STRING_VIEW
        if resolution is clkbuiltins.STRING
        else typereg.get_cpp_type(binding.module.context, resolution)
    )

    cpp_value: types.CppValueExpr | None = None
    if binding.value in (clkbuiltins.FALSE_VALUE, clkbuiltins.TRUE_VALUE):
        cpp_value = literal.bool_value_to_cpp(binding.value)
    elif isinstance(binding.value, primitive.DecimalValue):
        cpp_value = literal.decimal_value_to_cpp(binding.value)
    elif isinstance(binding.value, primitive.StringLiteral):
        cpp_value = types.CppValue(value_type=None, value=f'"{binding.value.value}"')
    else:
        msg = f"Converting {type(binding.value)} to a cpp value is unsupported."
        raise NotImplementedError(msg)

    return types.CppNamedValue(
        named_type=types.CppNamedType(cpp_type, binding.name),
        value=cpp_value,
        doc=binding.doc.value if binding.doc else "",
        qualifiers=["inline", "constexpr"],
    )


def _render_cpp_constants(bindings: list[ImmutableBinding], namespace: str) -> CppModuleChunks:
    """Render a collection of constants as inline constexprs."""
    cpp_mod = CppModuleChunks()
    for binding in bindings:
        value = _render_cpp_constant(binding)
        cpp_mod.header_chunk.context.add_includes(value.named_type.includes)
        chunk = value.render(namespace)
        cpp_mod.header_chunk.append(chunk)

    return cpp_mod


def _render_instantiate_aliases(
    instantiations: list[schema.InstantiateStmt], namespace: str, compiler_context: CompilerContext
) -> CppModuleChunks:
    """Render aliases for a collection of instantiation statements."""
    cpp_mod = CppModuleChunks()
    for instantiation_ir in instantiations:
        instantiation_alias = types.CppTypeAliasDef(
            name=instantiation_ir.name,
            alias_for=typereg.get_cpp_type(compiler_context, instantiation_ir.typespec),
            doc=None,
        )
        cpp_mod.header_chunk.append(instantiation_alias.render(namespace))

    return cpp_mod


@dataclass(slots=True)
class CppGeneratedEntities:
    """Structure for entities passed to CppTarget.from_generate_cpp."""

    cogs: list[cog.Cog] = field(default_factory=list)
    schemas: list[schema.Schema] = field(default_factory=list)
    enums: list[clkenum.ClkEnum] = field(default_factory=list)
    constants: list[statement.ImmutableBinding] = field(default_factory=list)
    instantiations: list[schema.InstantiateStmt] = field(default_factory=list)
    tags: list[strongtypes.Tag] = field(default_factory=list)
    extern_types: list[extern_type.ExternType] = field(default_factory=list)
    strong_types: list[strongtypes.StrongType] = field(default_factory=list)
    udp_sockets: list[udp.UdpSocket] = field(default_factory=list)
    audio_sources: list[audio.AudioSource] = field(default_factory=list)


@dataclass(slots=True)
class ProtoConvGeneratedEntities:
    """Structure for entities passed to CppTarget.from_generate_proto_conv."""

    schemas: list[schema.Schema] = field(default_factory=list)
    instantiations: list[schema.InstantiateStmt] = field(default_factory=list)


@dataclass
class CppTarget(node.NamedEntity, node.DocableEntity, node.CstNode[cst.CppTarget]):
    """IR for CppTargets."""

    options: CppTargetOptions
    schema_tags: list[SchemaTag]
    tags: list[TagTarget]
    enums: list[EnumTarget]
    representations: list[ReprInstantiation | ResolvedReprInstantiation]
    interfaces: list[InterfaceInstantiation]
    representations_and_interfaces: list[tuple[ResolvedReprInstantiation, InterfaceInstantiation]]
    cogs: list[CppCog]
    dials: list[CppDial]
    cpp_test_cogs: list[CppTestCog]
    python_cogs: list[CppPythonCog]
    externs: list[cpp_extern.CppExtern]
    converters: list[converter.Converter]
    udp_sockets: list[CppUdpSocket]
    audio_sources: list[CppAudioSource]
    nanobind_casters: list[NanobindTypeCaster]
    constants: dict[str, node.Deferrable[ImmutableBinding]]
    instantiations: list[schema.InstantiateStmt]

    # We must disable C901, PLR0912 and PLR0915 here (function complexity, branches, too many statements) because
    # we inherently have many branches, one for each type of module-level entity.
    # However, they're handled in a uniform way that isn't difficult to understand.
    # We could in principle make a data-driven table of handlers instead of explicit
    # branches, but it would be awkward and would not decouple the code in a
    # meaningful way.
    @classmethod
    def from_cst(  # noqa: C901, PLR0912, PLR0915 (see above)
        cls: type[CppTarget], cst_node: cst.CppTarget, module: node.Module
    ) -> CppTarget:
        """Create an IR CppTarget from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), terminals=module.terminals)
        options = CppTargetOptions.from_cst(cst_node.child_cpp_target_options(), module)
        schema_tags = []
        tags = []
        enums = []
        representations = []
        interfaces = []
        cogs = []
        dials = []
        cpp_test_cogs = []
        python_cogs = []
        externs = []
        converters = []
        udp_sockets = []
        audio_sources = []
        nanobind_casters = []
        constants: dict[str, node.Deferrable[ImmutableBinding]] = {}
        for target_stmt in cst_node.children_cpp_target_statement():
            if representation_cst := target_stmt.maybe_cpp_representation():
                representations.append(representation := ReprInstantiation.from_cst(representation_cst, module))
                if representation.name:
                    module.inner_scope.define(representation.name, representation, module.terminals)
            elif schema_tag_cst := target_stmt.maybe_schema_tag():
                schema_tags.append(SchemaTag.from_cst(schema_tag_cst, module))
            elif tag_cst := target_stmt.maybe_tag_target():
                tags.append(TagTarget.from_cst(tag_cst, module))
            elif enum_cst := target_stmt.maybe_cpp_enum():
                enums.append(EnumTarget.from_cst(enum_cst, module))
            elif interface_cst := target_stmt.maybe_cpp_interface():
                interface = InterfaceInstantiation.from_cst(interface_cst, module)
                interfaces.append(interface)
                if interface.name:
                    module.inner_scope.define(
                        interface.name, InterfaceAlias.from_instantiation(interface), module.terminals
                    )
            elif cog_cst := target_stmt.maybe_cpp_cog():
                cpp_cog = CppCog.from_cst(cog_cst, module)
                cogs.append(cpp_cog)
                dials.append(CppDial(cpp_cog))
                if options.generate_cpp_test_cogs:
                    cpp_test_cogs.append(CppTestCog(cpp_cog))
            elif python_cog_cst := target_stmt.maybe_cpp_python_cog():
                cpp_cog = CppCog.from_cst(python_cog_cst, module)
                cogs.append(cpp_cog)
                dials.append(CppDial(cpp_cog))
                python_cogs.append(CppPythonCog(python_cog_cst, module, cpp_cog))
            elif extern_cst := target_stmt.maybe_cpp_extern():
                extern_ir = cpp_extern.CppExtern.from_cst(extern_cst, module)
                externs.append(extern_ir)
            elif converter_cst := target_stmt.maybe_converter():
                converter_ir = converter.Converter.from_cst(converter_cst, module, options.namespace)
                converters.append(converter_ir)
            elif udp_socket_cst := target_stmt.maybe_cpp_udp_socket():
                udp_socket_ir = CppUdpSocket.from_cst(udp_socket_cst, module)
                udp_sockets.append(udp_socket_ir)
            elif nanobind_caster_cst := target_stmt.maybe_nanobind_type_caster():
                nanobind_caster_ir = NanobindTypeCaster.from_cst(nanobind_caster_cst, module, options.namespace)
                nanobind_casters.append(nanobind_caster_ir)
            elif constant_cst := target_stmt.maybe_target_constant():
                constant_lookup = node.DeferredLookup.make(
                    expected_type=ImmutableBinding,
                    cst_identifier=constant_cst.child_constant_name(),
                    terminals=module.terminals,
                )
                if constant_lookup.identifier in constants:
                    msg = node.append_error_line(
                        constant_cst,
                        module,
                        f"Constant {constant_lookup.identifier} is already included in target {name}",
                    )
                    raise ValueError(msg)
                constants[constant_lookup.identifier] = constant_lookup
            elif audio_source_cst := target_stmt.maybe_cpp_audio_source():
                audio_source_ir = CppAudioSource.from_cst(audio_source_cst, module)
                audio_sources.append(audio_source_ir)
            else:
                msg = node.append_error_line(target_stmt, module, "Unrecognized statement within cpp_target")
                raise NotImplementedError(msg)

        return cls(
            module=module,
            cst_node=cst_node,
            doc=doc,
            name=name,
            scope=module.inner_scope,
            options=options,
            schema_tags=schema_tags,
            tags=tags,
            enums=enums,
            representations=representations,
            interfaces=interfaces,
            representations_and_interfaces=[],
            cogs=cogs,
            dials=dials,
            cpp_test_cogs=cpp_test_cogs,
            python_cogs=python_cogs,
            externs=externs,
            converters=converters,
            udp_sockets=udp_sockets,
            audio_sources=audio_sources,
            nanobind_casters=nanobind_casters,
            constants=constants,
            instantiations=[],
        )

    # We disable C901, PLR0912, PLR0915 (function complexity, statements, branches) here because the
    # complexity comes from having to process each entity type passed in from the compiler.
    # However, they're handled in a uniform way that isn't difficult to understand.
    @classmethod
    def from_generate_cpp(cls: type[CppTarget], module: node.Module, entities: CppGeneratedEntities) -> CppTarget:  # noqa: C901, PLR0915 (see above)
        """Generate an IR CppTarget from the entities defined in a module."""
        name = f"{module.module_id.name.split('::')[-1]}_clk_cc"
        schema_tags = []
        representations_and_interfaces = []
        instantiations: list[schema.InstantiateStmt] = []
        enums = []
        tags = []
        externs = []
        constants: dict[str, node.Deferrable[ImmutableBinding]] = {}

        assert module.generates is not None
        assert module.inner_attrs is not None  # Attributes are set whenever the generate attribute set
        namespace = module.inner_attrs.get_cpp_namespace()
        if namespace is None:
            msg = node.append_error_line(module.cst_node, module, "cpp namespace attribute is not set")
            raise ValueError(msg)

        cogs = [CppCog(cog_ir=cog, dial_header=None, cog_header=None) for cog in entities.cogs]
        python_cogs = (
            [CppPythonCog(None, module, cpp_cog) for cpp_cog in cogs]
            if node.GenerateTarget.py_cog in module.generates
            else []
        )
        dials = [CppDial(cpp_cog=cog) for cog in cogs]
        cpp_test_cogs = (
            [CppTestCog(cpp_cog) for cpp_cog in cogs] if node.GenerateTarget.cpp_test_cog in module.generates else []
        )

        options = CppTargetOptions(
            module=module,
            cst_node=None,
            namespace=namespace,
            generate_cog_metrics=module.inner_attrs.get_cpp_generate_cog_metrics(),
            generate_cpp_test_cogs=node.GenerateTarget.cpp_test_cog in module.generates,
        )

        for constant in entities.constants:
            constants[constant.name] = constant

        for schema_ir in entities.schemas:
            if schema_ir.programmatically_generated:
                continue

            schema_tag = SchemaTag(schema_ir=schema_ir)
            schema_tags.append(schema_tag)

            if schema_ir.parameters:
                # Parameterized schemas are handled with instantitate statements
                continue

            representation_ir = ResolvedReprInstantiation.from_schema(schema_ir, module)
            interface_ir = InterfaceInstantiation.from_schema(schema_ir, module)
            representations_and_interfaces.append((representation_ir, interface_ir))

        for instantiation in entities.instantiations:
            if instantiation.name:
                instantiations.append(instantiation)

            assert isinstance(instantiation.typespec, typesys.Instantiation)
            representation_ir = ResolvedReprInstantiation.from_schema(instantiation.typespec, module)
            interface_ir = InterfaceInstantiation.from_schema(instantiation.typespec, module)
            representations_and_interfaces.append((representation_ir, interface_ir))

        for enum_ir in entities.enums:
            enum_target = EnumTarget(enum_ir=enum_ir)
            enums.append(enum_target)

        for tag_ir in entities.tags:
            tag_target = TagTarget(tag_ir=tag_ir)
            tags.append(tag_target)

        for extern_type_ir in entities.extern_types:
            assert extern_type_ir.attributes is not None
            type_header = extern_type_ir.attributes.get_cpp_type_header()
            type_namespace = extern_type_ir.attributes.get_cpp_type_namespace()
            assert type_header is not None  # Invariant
            assert type_namespace is not None  # Invariant
            extern_type = cpp_extern.CppExternType(
                module=module,
                cst_node=None,
                extern_type_expr=None,
                subclass_handler=cpp_extern.CppExternHandler(extern_typ=extern_type_ir),
            )
            externs.append(
                cpp_extern.CppExtern(
                    module=module,
                    cst_node=None,
                    doc=None,
                    header=Header(module.module_id.repo, type_header, iwyu_pragma="IWYU pragma: export"),
                    namespace=type_namespace,
                    extern_types=[extern_type],
                )
            )

        for strong_type_ir in entities.strong_types:
            assert strong_type_ir.attributes is not None
            type_header = strong_type_ir.attributes.get_cpp_type_header()
            type_namespace = strong_type_ir.attributes.get_cpp_type_namespace()
            if type_header is None or type_namespace is None:
                continue
            extern_type = cpp_extern.CppExternType(
                module=module,
                cst_node=None,
                extern_type_expr=None,
                subclass_handler=cpp_extern.StrongTypeHandler(
                    strong_type=strong_type_ir,
                    factory=strong_type_ir.attributes.get_cpp_type_factory(),
                    cst_node=None,
                    module=module,
                ),
            )
            externs.append(
                cpp_extern.CppExtern(
                    module=module,
                    cst_node=None,
                    doc=None,
                    header=Header(module.module_id.repo, type_header, iwyu_pragma="IWYU pragma: export"),
                    namespace=type_namespace,
                    extern_types=[extern_type],
                )
            )

        udp_sockets = [CppUdpSocket.from_generate_cpp(udp_socket) for udp_socket in entities.udp_sockets]
        audio_sources = [CppAudioSource.from_generate_cpp(audio_source) for audio_source in entities.audio_sources]

        result = cls(
            module=module,
            cst_node=None,
            doc=module.doc,
            name=name,
            scope=module.inner_scope,
            options=options,
            schema_tags=schema_tags,
            tags=tags,
            enums=enums,
            representations=[],
            interfaces=[],
            representations_and_interfaces=representations_and_interfaces,
            cogs=cogs,
            dials=dials,
            cpp_test_cogs=cpp_test_cogs,
            python_cogs=python_cogs,
            externs=externs,
            converters=[],
            udp_sockets=udp_sockets,
            audio_sources=audio_sources,
            nanobind_casters=[],
            constants=constants,
            instantiations=instantiations,
        )

        result._handle_generated_cog_metrics()
        result._register_report_group_outputs_on_dial()
        return result

    @classmethod
    def from_generate_proto_conv(
        cls: type[CppTarget], module: node.Module, entities: ProtoConvGeneratedEntities
    ) -> CppTarget:
        """Generate an IR CppTarget to implement protobuf conversion for the entities defined in a module."""
        name = f"{module.module_id.name.split('::')[-1]}_clk_proto_conv"
        converters = []

        assert module.inner_attrs is not None  # Attributes are set whenever the generate attribute set
        namespace = module.inner_attrs.get_proto_conv_namespace()
        if namespace is None:
            msg = node.append_error_line(module.cst_node, module, "proto_conv or cpp namespace attribute is not set")
            raise ValueError(msg)

        options = CppTargetOptions(
            module=module,
            cst_node=None,
            namespace=namespace,
            generate_cog_metrics=False,
            generate_cpp_test_cogs=False,
        )

        for schema_ir in entities.schemas:
            if schema_ir.programmatically_generated or schema_ir.parameters:
                # Parameterized schemas are handled with instantitate statements
                continue
            assert schema_ir.attributes is not None
            cpp_interface_ir = InterfaceInstantiation.from_schema(schema_ir, module)
            assert isinstance(cpp_interface_ir.typespec, typesys.Instantiation)
            proto_representation_ir = ResolvedReprInstantiation.from_proto_schema(schema_ir, module)
            assert isinstance(proto_representation_ir.typespec, typesys.Instantiation)
            if schema_ir.attributes.get_proto_conv_tap_to_protobuf():
                converters.append(
                    converter.Converter.from_generate_proto_conv(
                        module=module,
                        namespace=namespace,
                        converter_type=clkbuiltins.TAP_TO_PROTOBUF,
                        cpp_typespec=cpp_interface_ir.typespec,
                        proto_typespec=proto_representation_ir.typespec,
                    )
                )
            if schema_ir.attributes.get_proto_conv_protobuf_to_tap():
                converters.append(
                    converter.Converter.from_generate_proto_conv(
                        module=module,
                        namespace=namespace,
                        converter_type=clkbuiltins.PROTOBUF_TO_TAP,
                        cpp_typespec=cpp_interface_ir.typespec,
                        proto_typespec=proto_representation_ir.typespec,
                    )
                )

        for instantiation in entities.instantiations:
            assert isinstance(instantiation.typespec, typesys.Instantiation)
            assert isinstance(instantiation.typespec.instantiates, schema.Schema)
            proto_representation_ir = ResolvedReprInstantiation.from_proto_schema(instantiation.typespec, module)
            assert isinstance(proto_representation_ir.typespec, typesys.Instantiation)
            cpp_interface_ir = InterfaceInstantiation.from_schema(instantiation.typespec, module)
            assert isinstance(cpp_interface_ir.typespec, typesys.Instantiation)
            if instantiation.attributes.get_proto_conv_tap_to_protobuf():
                converters.append(
                    converter.Converter.from_generate_proto_conv(
                        module=module,
                        namespace=namespace,
                        converter_type=clkbuiltins.TAP_TO_PROTOBUF,
                        cpp_typespec=cpp_interface_ir.typespec,
                        proto_typespec=proto_representation_ir.typespec,
                    )
                )
            if instantiation.attributes.get_proto_conv_protobuf_to_tap():
                converters.append(
                    converter.Converter.from_generate_proto_conv(
                        module=module,
                        namespace=namespace,
                        converter_type=clkbuiltins.PROTOBUF_TO_TAP,
                        cpp_typespec=cpp_interface_ir.typespec,
                        proto_typespec=proto_representation_ir.typespec,
                    )
                )

        return cls(
            module=module,
            cst_node=None,
            doc=module.doc,
            name=name,
            scope=module.inner_scope,
            options=options,
            schema_tags=[],
            tags=[],
            enums=[],
            representations=[],
            interfaces=[],
            representations_and_interfaces=[],
            cogs=[],
            dials=[],
            cpp_test_cogs=[],
            python_cogs=[],
            externs=[],
            converters=converters,
            udp_sockets=[],
            audio_sources=[],
            nanobind_casters=[],
            constants={},
            instantiations=[],
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        for entity in chain(
            self.schema_tags,
            self.tags,
            self.enums,
            self.representations,
            self.interfaces,
            self.cogs,
            self.externs,
            self.converters,
            self.udp_sockets,
            self.audio_sources,
            self.nanobind_casters,
        ):
            # mypy can't/won't reason through chain
            cast(
                "EnumTarget | ReprInstantiation | InterfaceInstantiation | cpp_extern.CppExtern | converter.Converter | CppUdpSocket | NanobindTypeCaster",
                entity,
            ).resolve()

        self._handle_generated_cog_metrics()
        self._register_report_group_outputs_on_dial()

    def _handle_generated_cog_metrics(self) -> None:
        for member_cog in self.cogs:
            if not isinstance(member_cog.cog_ir, cog.Cog):
                msg = "Attempted to access an unresolved Cog"
                raise TypeError(msg)

            # Add the generated clockwork representations, interfaces, schemas, enums, etc. to the target.
            # This ensures that the necessary corresponding c++ will be generated.
            # Note: Report group generated entities are handled separately in _register_report_group_outputs_on_dial
            self.representations.extend(member_cog.cog_ir.generated_repr())
            self.interfaces.extend(member_cog.cog_ir.generated_interfaces())
            self.schema_tags.extend(
                SchemaTag(schema_ir=generated_schema) for generated_schema in member_cog.cog_ir.generated_schemas()
            )
            self.enums.extend(
                EnumTarget(enum_ir=generated_enum) for generated_enum in member_cog.cog_ir.generated_enums()
            )

    def _register_report_group_outputs_on_dial(self) -> None:
        """Register generated report group representations, interfaces, and schemas on their corresponding dial targets."""
        for member_cog, dial in zip(self.cogs, self.dials, strict=True):
            if not isinstance(member_cog.cog_ir, cog.Cog):
                msg = "Attempted to access an unresolved Cog"
                raise TypeError(msg)

            dial.representations.extend(member_cog.cog_ir.generated_report_group_repr())
            dial.interfaces.extend(member_cog.cog_ir.generated_report_group_interfaces())
            dial.schema_tags.extend(
                SchemaTag(schema_ir=generated_schema)
                for generated_schema in member_cog.cog_ir.generated_report_group_schemas()
            )

    def render_cpp_entities(self) -> CppModuleChunks:
        """Convert primary entities (Cogs, Interfaces) to C++."""
        cpp_mod = CppModuleChunks()

        cpp_mod.append([f"namespace {self.options.namespace}", "{"])
        if self.constants:
            constant_chunks = _render_cpp_constants(
                [cast("ImmutableBinding", lookup) for lookup in self.constants.values()], self.options.namespace
            )
            cpp_mod.header_chunk.append(constant_chunks.header_chunk)

        for tag in chain(
            self.schema_tags, self.tags, self.enums, self.cogs, self.converters, self.udp_sockets, self.audio_sources
        ):
            # mypy can't/won't reason through chain
            cpp_mod.append(
                cast(  # pyright: ignore[reportUnnecessaryCast] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
                    "SchemaTag | TagTarget | EnumTarget | CppCog | converter.Converter | CppUdpSocket | CppAudioSource",
                    tag,
                ).render(self.module.context, self.options.namespace)
            )

        cpp_mod.append(f"}} // namespace {self.options.namespace}")

        clk_interfaces = [interface for _, interface in self.representations_and_interfaces]
        for entity in chain(self.interfaces, self.externs, clk_interfaces):
            cpp_mod.append(cast("InterfaceInstantiation | cpp_extern.CppExtern", entity).render(types.GLOBAL_NAMESPACE))  # pyright: ignore[reportUnnecessaryCast] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

        cpp_mod.append(
            [
                f"namespace {self.options.namespace}",
                "{",
                "// Interface and instantiation aliases",
            ]
        )
        for entity in chain(self.interfaces, clk_interfaces):
            if entity.name:
                cpp_mod.append(entity.render_alias(self.options.namespace))
        cpp_mod.append(_render_instantiate_aliases(self.instantiations, self.options.namespace, self.module.context))

        cpp_mod.append(f"}} // namespace {self.options.namespace}")

        cpp_mod.append(render_nanobind_casters(self.nanobind_casters, cpp_mod.inline_chunk.context.includes))

        return cpp_mod

    def render_cpp_dial(self) -> CppModuleChunks | None:
        """Convert Dials to C++."""
        if not self.dials:
            return None

        dial_cpp_mod = CppModuleChunks()

        dial_cpp_mod.append([f"namespace {self.options.namespace}", "{"])
        for dial in self.dials:
            dial_cpp_mod.append(dial.render(self.options.namespace))
        dial_cpp_mod.append(f"}} // namespace {self.options.namespace}")

        dial_interfaces = list(chain.from_iterable(dial.interfaces for dial in self.dials))
        for interface in dial_interfaces:
            dial_cpp_mod.append(interface.render(types.GLOBAL_NAMESPACE))

        if dial_interfaces:
            dial_cpp_mod.append(
                [
                    f"namespace {self.options.namespace}",
                    "{",
                    "// Interface and instantiation aliases",
                ]
            )
            for interface in dial_interfaces:
                if interface.name:
                    dial_cpp_mod.append(interface.render_alias(self.options.namespace))
            dial_cpp_mod.append(f"}} // namespace {self.options.namespace}")

        return dial_cpp_mod

    def render_cpp_test_cogs(self) -> CppModuleChunks | None:
        """Convert Dials to C++."""
        if not self.cpp_test_cogs:
            return None

        cpp_mod = CppModuleChunks()
        cpp_mod.append([f"namespace {self.options.namespace}", "{"])
        for cpp_test_cog in self.cpp_test_cogs:
            cpp_mod.append(cpp_test_cog.render(self.options.namespace))
        cpp_mod.append(f"}} // namespace {self.options.namespace}")

        return cpp_mod

    def render_cpp_python_cog(self) -> CppModuleChunks | None:
        """Convert python cogs to C++."""
        if not self.python_cogs:
            return None

        python_cog_cpp_mod = CppModuleChunks()

        python_cog_cpp_mod.append([f"namespace {self.options.namespace}", "{"])
        for python_cog in self.python_cogs:
            python_cog_cpp_mod.append(python_cog.render(self.options.namespace))
        python_cog_cpp_mod.append(f"}} // namespace {self.options.namespace}")

        return python_cog_cpp_mod

    def _get_valid_cog_irs(self) -> list[cog.Cog]:
        """Extract valid Cog IR objects from this target.

        Returns:
            List of valid Cog IR objects
        """
        return [cpp_cog.cog_ir for cpp_cog in self.cogs if isinstance(cpp_cog.cog_ir, cog.Cog)]

    def render_and_write(self, root_dir: Path) -> None:
        """Convert to C++ and write output to files."""
        write_dir = root_dir / BazelPathResolver().to_buildtime_path(self.module.module_id).parent
        include_dir = self.module.module_id.get_base_path().parent

        # Generate and write main C++ files
        cpp_mod = self.render_cpp_entities()
        write_to_file(cpp_mod, write_dir, include_dir, self.name, self.module.module_id.repo)

        # Generate and write dial files if present
        dial_cpp_mod = self.render_cpp_dial()
        if dial_cpp_mod:
            write_to_file(dial_cpp_mod, write_dir, include_dir, self.name + "_dial", self.module.module_id.repo)
        test_cogs_cpp_mod = self.render_cpp_test_cogs()
        if test_cogs_cpp_mod:
            write_to_file(test_cogs_cpp_mod, write_dir, include_dir, self.name + "_test", self.module.module_id.repo)
        python_cog_cpp_mod = self.render_cpp_python_cog()
        if python_cog_cpp_mod:
            write_to_file(python_cog_cpp_mod, write_dir, include_dir, self.name + "_impl", self.module.module_id.repo)

    def output_targets(self) -> list[CcLibrary]:
        """Extract language target dependency information."""
        include_dir = self.module.module_id.get_base_path().parent
        cpp_mod = self.render_cpp_entities()
        # Process needs to manage namespace differently, therefore not included as part of `render_cpp_entities`
        main_target = as_cc_library(cpp_mod, self.name, include_dir, self.module.module_id, False)
        targets = [main_target]

        dial_cpp_mod = self.render_cpp_dial()
        if dial_cpp_mod:
            # The user has to provide an implementation of the cog
            # execute functions.  This defines a convention where the
            # name of that target is the same name as the cpp_target
            # with a `_impl` suffix.
            main_target.deps = [*list(main_target.deps), Label(f"//{include_dir}:{self.name}_impl")]

            targets.append(as_cc_library(dial_cpp_mod, self.name + "_dial", include_dir, self.module.module_id, False))

        test_cogs_cpp_mod = self.render_cpp_test_cogs()
        if test_cogs_cpp_mod:
            targets.append(
                as_cc_library(test_cogs_cpp_mod, self.name + "_test", include_dir, self.module.module_id, False)
            )

        python_cog_cpp_mod = self.render_cpp_python_cog()
        if python_cog_cpp_mod:
            targets.append(
                as_cc_library(python_cog_cpp_mod, self.name + "_impl", include_dir, self.module.module_id, False)
            )

        return targets


@dataclass
class CppTargetOptions(node.CstNode[cst.CppTargetOptions]):
    """IR for CppTarget options."""

    namespace: str
    generate_cog_metrics: bool = False
    generate_cpp_test_cogs: bool = False

    @classmethod
    def from_cst(cls: type[CppTargetOptions], cst_node: cst.CppTargetOptions, module: node.Module) -> CppTargetOptions:
        """Create an IR CppTargetOptions from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        namespace = None
        generate_cog_metrics = False
        generate_cpp_test_cogs = False

        # Look for namespace option
        if ns_option := cst_node.maybe_cpp_namespace_option():
            namespace = get_span(ns_option.child_namespace_path().span, terminals=module.terminals)

        # Look for generate_cog_metrics option
        if metrics_option := cst_node.maybe_generate_cog_metrics_option():
            metrics_value = metrics_option.child_boolean()
            generate_cog_metrics = metrics_value.maybe_true() is not None

        # Look for generate_cpp_test_cogs option
        if cpp_test_cogs_option := cst_node.maybe_generate_cpp_test_cogs_option():
            cpp_test_cogs_value = cpp_test_cogs_option.child_boolean()
            generate_cpp_test_cogs = cpp_test_cogs_value.maybe_true() is not None

        if namespace is None:
            msg = node.append_error_line(cst_node, module, "Namespace option must be provided")
            raise ValueError(msg)

        return cls(
            namespace=namespace,
            generate_cog_metrics=generate_cog_metrics,
            module=module,
            cst_node=cst_node,
            generate_cpp_test_cogs=generate_cpp_test_cogs,
        )


@dataclass(eq=True, slots=True)
class SchemaTag:
    """A tag type for a schema inside cpp_target."""

    schema_ir: schema.Schema | expr.Expr

    @classmethod
    def from_cst(
        cls: type[SchemaTag],
        cst_node: cst.SchemaTag,
        module: node.Module,
    ) -> SchemaTag:
        """Create an IR SchemaTag from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        return cls(schema_ir=typespec)

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.schema_ir, expr.Expr):
            msg = f"Attempt to resolve SchemaTag twice: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        typespec = self.schema_ir.evaluate()
        if not isinstance(typespec, schema.Schema):
            msg = self.schema_ir.append_error_line(f"Expected Schema, got {type(typespec)}")
            raise TypeError(msg)
        self.schema_ir = typespec

    def render(self, compiler_context: CompilerContext, enclosing_namespace: str) -> CppModuleChunks:
        """Convert to C++."""
        if not isinstance(self.schema_ir, schema.Schema):
            msg = "Attempt to render before resolving."
            raise TypeError(msg)
        return to_cpp_struct(compiler_context, self.schema_ir).render(enclosing_namespace)


@dataclass(eq=True, slots=True)
class TagTarget:
    """Instantiates a tag type inside cpp_target."""

    tag_ir: strongtypes.Tag | expr.Expr

    @classmethod
    def from_cst(
        cls: type[TagTarget],
        cst_node: cst.TagTarget,
        module: node.Module,
    ) -> TagTarget:
        """Create an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        return cls(tag_ir=typespec)

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.tag_ir, expr.Expr):
            msg = f"Attempt to resolve TagTarget twice: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        typespec = self.tag_ir.evaluate()
        if not isinstance(typespec, strongtypes.Tag):
            msg = self.tag_ir.append_error_line(f"Expected Tag, got {type(typespec)}")
            raise TypeError(msg)
        self.tag_ir = typespec

    def render(self, compiler_context: CompilerContext, enclosing_namespace: str) -> CppModuleChunks:
        """Convert to C++."""
        if not isinstance(self.tag_ir, strongtypes.Tag):
            msg = "Attempt to render before resolving."
            raise TypeError(msg)

        return to_cpp_struct(compiler_context, self.tag_ir).render(enclosing_namespace)


@dataclass(eq=True, slots=True)
class EnumTarget:
    """Instantiates an enum type inside cpp_target."""

    enum_ir: clkenum.ClkEnum | expr.Expr

    @classmethod
    def from_cst(
        cls: type[EnumTarget],
        cst_node: cst.CppEnum,
        module: node.Module,
    ) -> EnumTarget:
        """Create an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        return cls(enum_ir=typespec)

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.enum_ir, expr.Expr):
            msg = f"Attempt to resolve EnumTarget twice: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        typespec = self.enum_ir.evaluate()
        if not isinstance(typespec, clkenum.ClkEnum):
            msg = self.enum_ir.append_error_line(f"Expected ClkEnum, got {type(typespec)}")
            raise TypeError(msg)
        self.enum_ir = typespec

    # ARG002 suppressed because compiler context not used for enums, but it's required for interface
    def render(self, compiler_context: CompilerContext, cpp_namespace: str) -> CppModuleChunks:  # noqa: ARG002 (Unused arguments are required by the parent method signature)
        """Convert to C++."""
        if not isinstance(self.enum_ir, clkenum.ClkEnum):
            msg = "Attempt to render before resolving."
            raise TypeError(msg)
        cpp_mod = CppModuleChunks()
        cpp_mod.header_chunk.context.add_include(SystemHeader("wise_enum.h"))
        ir_type = self.enum_ir.get_underlying_type()
        cpp_type = typereg.get_cpp_type(self.enum_ir.module.context, ir_type)
        cpp_mod.header_chunk.context.add_includes(cpp_type.includes)
        cpp_mod.header_chunk.append(comment_doc_string(self.enum_ir.doc.value))
        if "performance-enum-size" in self.enum_ir.get_linter_overrides():
            cpp_mod.header_chunk.append("// NOLINTNEXTLINE(performance-enum-size)")
        cpp_mod.header_chunk.append("WISE_ENUM_CLASS(")
        cpp_mod.header_chunk.append(f"({self.enum_ir.name}, {cpp_type.render(cpp_namespace)}),", indent=1)
        for index, (_, value) in enumerate(self.enum_ir.values.items(), start=1):
            suffix = "," if index < len(self.enum_ir.values) else ")"
            assert isinstance(value.integer_value, int)
            assert ir_type.signed or value.integer_value >= 0
            cpp_mod.header_chunk.append(f"({value.name}, {value.integer_value}){suffix}", indent=1)
        if self.enum_ir.bit_flags:
            cpp_mod.header_chunk.context.add_include(Header(JEWELS_REPO, "jewels/utility/enum_flags.hh"))
            cpp_mod.header_chunk.append(f"JEWELS_ENABLE_ENUM_FLAGS({self.enum_ir.name})")
        return cpp_mod


@dataclass(eq=True, slots=True)
class CppDial:
    """A wrapper around a CppCog to render the dial."""

    cpp_cog: CppCog
    schema_tags: list[SchemaTag] = field(default_factory=list)
    representations: list[ReprInstantiation | ResolvedReprInstantiation] = field(default_factory=list)
    interfaces: list[InterfaceInstantiation] = field(default_factory=list)

    def render(self, namespace: str) -> CppModuleChunks:
        """Convert the dial to C++."""
        if not isinstance(self.cpp_cog.cog_ir, Cog) or self.cpp_cog.dial_header is None:
            # This is resolved by the CppCog.  No need for an extra resolve here.
            msg = "Attempt to render before resolving."
            raise TypeError(msg)

        dial_class_name = to_dial_name(self.cpp_cog.cog_ir.name)
        dial_gen = DialGenerator(
            cog_ir=self.cpp_cog.cog_ir,
            dial_header=self.cpp_cog.dial_header,
            class_name=dial_class_name,
            cpp_namespace=namespace,
        )

        cpp_mod = dial_gen.render()

        for schema_tag in self.schema_tags:
            cpp_mod.append(schema_tag.render(self.cpp_cog.cog_ir.module.context, namespace))

        return cpp_mod


@dataclass(eq=True, slots=True)
class CppTestCog:
    """A wrapper around a CppCog to render the dial."""

    cpp_cog: CppCog

    def render(self, namespace: str) -> CppModuleChunks:
        """Convert the test cog to C++."""
        if not isinstance(self.cpp_cog.cog_ir, Cog) or self.cpp_cog.cog_header is None:
            # This is resolved by the CppCog.  No need for an extra resolve here.
            msg = "Attempt to render before resolving."
            raise TypeError(msg)

        cpp_test_cog_gen = CppTestCogGenerator(
            cog_ir=self.cpp_cog.cog_ir, cpp_namespace=namespace, cog_header=self.cpp_cog.cog_header
        )
        return cpp_test_cog_gen.render()
