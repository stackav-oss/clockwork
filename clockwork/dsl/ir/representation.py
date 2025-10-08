# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Representation-related IR nodes."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Generic, TypeVar

from clockwork.dsl import cst
from clockwork.dsl.ir import clkbuiltins, expr, node, primitive, schema, typesys, units
from clockwork.dsl.ir.cst_util import get_span, int_from_cst

if TYPE_CHECKING:
    from collections.abc import Container, Iterable


@dataclass
class Representation(typesys.TypeDef, node.DocableEntity, node.CstNode[cst.Representation]):
    """IR Node for a customized Representation.

    These nodes are used to customize representations of schemas.  This is
    distinct from the C++ instantiation of a representation; for that see
    SchemaRepresentation in cpp_target.
    """

    typespec: typesys.Instantiation | expr.Expr
    options: Options
    field_options: dict[int, FieldOptions]

    def get_repr_type(self) -> typesys.TypeVal:
        """Get the representation type.

        Precondition:
            The Representation must be fully resolved.

        Returns:
            The representation type (e.g., clkbuiltins.TACHYON)
        """
        typespec = self._require_resolved()
        return typespec.instantiates

    def get_schema(self) -> schema.Schema:
        """Get the schema being represented.

        Precondition:
            The Representation must be fully resolved.

        Returns:
            The schema being represented.
        """
        typespec = self._require_resolved()
        schema_ir = typespec.arguments["schema"]
        assert isinstance(schema_ir, schema.Schema)
        return schema_ir

    def _require_resolved(self) -> typesys.Instantiation:
        if not isinstance(self.typespec, typesys.Instantiation):
            msg = "Unresolved Representation"
            raise RuntimeError(msg)  # noqa: TRY004 (Accessing unresolved instance is a runtime error)
        return self.typespec

    @classmethod
    def from_cst(cls: type[Representation], cst_node: cst.Representation, module: node.Module) -> Representation:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name_cst = cst_node.maybe_name()
        name = get_span(name_cst.child_value(), module.terminals) if name_cst else ""
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        options = (
            Options.from_cst(options_cst, module)
            if (options_cst := cst_node.maybe_repr_options())
            else Options.make_default(module)
        )
        field_options = {}
        if fields_cst := cst_node.maybe_repr_fields():
            for field_cst in fields_cst.children_repr_field():
                field_ir = FieldOptions.from_cst(field_cst, module)
                field_options[field_ir.field_num] = field_ir
        return cls(
            doc=doc,
            name=name,
            module=module,
            scope=module.inner_scope,
            cst_node=cst_node,
            type_info=clkbuiltins.TYPE_TYPE,
            typespec=typespec,
            options=options,
            field_options=field_options,
        )

    def resolve(self) -> None:
        """Perform IR finalization."""
        if not isinstance(self.typespec, expr.Expr):
            msg = f"Attempt to resolve twice: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (Resolving twice is a runtime error)
        typespec = self.typespec.evaluate()
        if not isinstance(typespec, typesys.Instantiation):
            msg = self.typespec.append_error_line(f"Expected an instantiation but got {type(typespec)}")
            raise TypeError(msg)
        if typespec.instantiates not in (clkbuiltins.TACHYON, clkbuiltins.PROTOBUF):
            msg = self.typespec.append_error_line("Only Tachyon and Protobuf representation options implemented")
            raise TypeError(msg)
        schema_ir = typespec.arguments["schema"]
        if not isinstance(schema_ir, schema.Schema):
            msg = self.typespec.append_error_line("Tachyon or Protobuf must be instantiated for a schema type.")
            raise TypeError(msg)
        self.options.resolve()
        for a_field in self.field_options.values():
            if (schema_name := schema_ir.fields[a_field.field_num].cur_name) != a_field.field_name:
                msg = a_field.append_error_line(
                    f'Field {a_field.field_num} in schema "{schema_ir.name}"'  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
                    f' has name "{schema_name}", not "{a_field.field_name}"',
                )
                raise ValueError(msg)
            a_field.resolve()
        self.typespec = typespec


@dataclass
class CommonOptions:
    """Options common to schemas and fields."""

    align_bytes: DocableOption[int] | DocableExpr[cst.ReprOptionAlign] | None

    @classmethod
    def base_from_cst(
        cls: type[CommonOptions],
        cst_nodes: Iterable[cst.ReprOption | cst.ReprFieldOption],
        module: node.Module,
    ) -> CommonOptions:
        """Construct an IR node of a derived class from a CST node."""
        align_bytes = None
        for cst_node in cst_nodes:
            align_cst = cst_node.child_repr_option_align()
            if align_bytes is not None:
                msg = node.append_error_line(cst_node, module, '"align" option specified twice')
                raise ValueError(msg)
            doc = node.Doc.maybe_from_cst(align_cst.maybe_doc(), module)
            value = align_cst.child_value()
            align_bytes = DocableExpr(doc, expr.Expr.from_cst(value, module), align_cst)
        return cls(align_bytes=align_bytes)

    def resolve(self) -> None:
        """Finalize the IR."""
        if self.align_bytes is not None:
            self._resolve_align()

    def _resolve_align(self) -> None:
        if not isinstance(self.align_bytes, DocableExpr):
            msg = "Attempt to resolve options twice: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (Resolving twice is a runtime error)
        value = self.align_bytes.expr.evaluate()
        if not isinstance(value, primitive.UnitValue) or value.type_info not in (clkbuiltins.BYTES, clkbuiltins.BITS):
            msg = self.align_bytes.expr.append_error_line(f"Expected a bit or byte unit value, got {value}")
            raise TypeError(msg)
        if value.type_info is clkbuiltins.BITS:
            value = primitive.bits_to_bytes(value)
        assert value.type_info is clkbuiltins.BYTES
        value = value.as_unit(units.BYTE)
        value_int = int(value.value)
        # Checking for power of 2 by looking for bit_count == 1
        if value.value != value_int or value_int < 1 or value_int.bit_count() != 1:
            msg = f"Alignment {value.value} bytes is not an integral power of two"
            raise ValueError(msg)
        self.align_bytes = DocableOption(self.align_bytes.doc, value=value_int)


T = TypeVar("T")


@dataclass
class DocableExpr(node.DocableEntity, Generic[T]):
    """Temporary holder for an unevaluated option."""

    expr: expr.Expr
    cst_node: T


@dataclass
class DocableOption(node.DocableEntity, Generic[T]):
    """An option with optional doc comment."""

    value: T


@dataclass
class Options(CommonOptions, node.CstNode[cst.ReprOptions]):
    """Whole-schema representation options."""

    @classmethod
    def from_cst(cls: type[Options], cst_node: cst.ReprOptions, module: node.Module) -> Options:
        """Construct an IR node from a CST node."""
        common = CommonOptions.base_from_cst(cst_node.children_repr_option(), module)
        return cls(align_bytes=common.align_bytes, module=module, cst_node=cst_node)

    @classmethod
    def make_default(cls: type[Options], module: node.Module) -> Options:
        """Make a default set of Options."""
        return cls(module=module, cst_node=None, align_bytes=None)


@dataclass
class FieldOptions(CommonOptions, node.CstNode[cst.ReprField]):
    """Field-level representation options."""

    field_num: int
    field_name: str

    @classmethod
    def from_cst(cls: type[FieldOptions], cst_node: cst.ReprField, module: node.Module) -> FieldOptions:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        field_num = int_from_cst(cst_node.child_field_num(), module.terminals)
        field_name = get_span(cst_node.child_name().child_value(), module.terminals)
        common = CommonOptions.base_from_cst(
            cst_node.children_repr_field_option(),
            module,
        )
        return cls(module, cst_node, align_bytes=common.align_bytes, field_num=field_num, field_name=field_name)


@dataclass(frozen=True, eq=True, slots=True)
class RepresentationReference:
    """A reference to a Representation.

    Attributes:
        schema_ir: Schema that this represents
        typespec: Instantiation of representation
    """

    schema_ir: schema.InstantiatedSchema
    typespec: typesys.Instantiation = field(repr=False)

    def name_resolution_fields(self) -> Container[str]:
        """Override recursion for node.resolve_names.

        This prevents us from recursively resolving into the referenced representation,
        which will already be resolved.
        """
        return []

    @classmethod
    def from_typespec(cls: type[RepresentationReference], typespec: typesys.Value) -> RepresentationReference | str:
        """Create a RepresentationReference from a type specification.

        Args:
            typespec: A Value expected to resolve to a representation.

        Returns:
            A reference if the typespec has the form of a representation, otherwise an error message.
        """
        if not isinstance(typespec, typesys.Instantiation) or len(typespec.arguments) != 1:
            return f"Invalid Representation type: {typespec}"
        if typespec.instantiates not in (clkbuiltins.POD, clkbuiltins.TACHYON, clkbuiltins.PROTOBUF):
            return f"Only Pod representation supported so far: {typespec}"

        schema_ir = schema.retrieve_schema(typespec.arguments.get("schema"))
        if isinstance(schema_ir, str):
            return schema_ir

        return cls(schema_ir=schema_ir, typespec=typespec)


@dataclass
class ResolvedReprInstantiation(node.NamedEntity, node.CstNode[cst.CppRepresentation | cst.ProtoRepresentation]):
    """An instantiation of a representation of a schema."""

    schema_ir: schema.InstantiatedSchema
    is_generic: bool
    typespec: typesys.Instantiation

    def get_schema(self) -> schema.InstantiatedSchema:
        """Get the instantiated schema that this is a representation of."""
        return self.schema_ir


@dataclass
class ReprInstantiation(node.NamedEntity, node.CstNode[cst.CppRepresentation | cst.ProtoRepresentation]):
    """An instantiation of a representation of a schema."""

    schema_ir: schema.InstantiatedSchema | None
    is_generic: bool
    typespec: typesys.Instantiation | expr.Expr
    resolved: ResolvedReprInstantiation | None

    @classmethod
    def from_cst(
        cls: type[ReprInstantiation],
        cst_node: cst.CppRepresentation | cst.ProtoRepresentation,
        module: node.Module,
    ) -> ReprInstantiation:
        """Create an IR SchemaRepresentation from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        is_generic = cst_node.maybe_generic() is not None
        name_cst = cst_node.maybe_name()
        name = get_span(name_cst.child_value(), module.terminals) if name_cst else ""
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        return cls(
            module=module,
            scope=module.inner_scope,
            cst_node=cst_node,
            schema_ir=None,
            is_generic=is_generic,
            name=name,
            typespec=typespec,
            resolved=None,
        )

    def resolve(self) -> ResolvedReprInstantiation:
        """Perform finalization of the IR."""
        if self.resolved or not isinstance(self.typespec, expr.Expr):
            msg = self.append_error_line(f"Attempt to resolve Representation twice: {self}")
            raise RuntimeError(msg)
        typespec = self.typespec.evaluate()
        ref = RepresentationReference.from_typespec(typespec)
        if isinstance(ref, str):
            msg = self.typespec.append_error_line(ref)
            raise TypeError(msg)
        parameters = ref.schema_ir.schema.generic_parameters()
        ref_schema = ref.typespec.arguments["schema"]
        if not isinstance(ref_schema, schema.Schema | typesys.Instantiation):
            msg = self.typespec.append_error_line("Representation must be instantiated for a schema type.")
            raise TypeError(msg)
        if self.is_generic:
            if parameters is None:
                msg = self.append_error_line("Generic representation requested for non-generic schema")
                raise ValueError(msg)
            if isinstance(ref.typespec, typesys.Instantiation):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
                msg = self.typespec.append_error_line("Generic representation requested for instantiated schema")
                raise ValueError(msg)
        elif parameters is not None and not isinstance(ref_schema, typesys.Instantiation):
            msg = self.append_error_line(
                'Instantiating representation for generic schema requires parameters or "generic" keyword',
            )
            raise ValueError(msg)
        self.typespec = ref.typespec
        self.schema_ir = ref.schema_ir
        self.resolved = ResolvedReprInstantiation(
            module=self.module,
            cst_node=self.cst_node,
            name=self.name,
            scope=self.scope,
            schema_ir=schema.InstantiatedSchema.from_typespec(ref_schema),
            is_generic=self.is_generic,
            typespec=self.typespec,
        )
        return self.resolved

    def get_resolved(self) -> ResolvedReprInstantiation:
        """Get a resolved version of this node."""
        if not self.resolved:
            msg = self.append_error_line("Attempt to access unresolved node")
            raise RuntimeError(msg)
        return self.resolved
