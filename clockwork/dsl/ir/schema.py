# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Schema-related IR nodes."""

from __future__ import annotations

import itertools
import uuid
from dataclasses import dataclass
from dataclasses import field as dc_field
from typing import TYPE_CHECKING, Any

from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl.ir import (
    clkbuiltins,
    clkenum,
    expr,
    extern_type,
    node,
    primitive,
    statement,
    strongtypes,
    tensor_builtins,
    typesys,
)
from clockwork.dsl.ir.cst_util import get_span, int_from_cst
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Iterable, Mapping


# Types that cannot be used for fields
_INVALID_FIELD_TYPES = (extern_type.ExternType,)


@dataclass
class ResolvedSchema(typesys.SchemaType, node.DocRequiredEntity, node.CstNode[cst.Schema]):
    """Fully resolved version of a Schema node."""

    inner_scope: node.Scope = dc_field(repr=False)
    uuid: uuid.UUID | None
    parameters: dict[int, ResolvedFieldDef] | None
    fields: dict[int, ResolvedFieldDef] = dc_field(repr=False)
    field_src_order: dict[int, int] = dc_field(repr=False)
    options: SchemaOptions | None = dc_field(repr=False)
    source: Schema | None = dc_field(repr=False)
    history: SchemaHistory = dc_field(repr=False)

    def get_resolved(self) -> ResolvedSchema:
        """Get a resolved version of this object."""
        return self

    @override
    def generic_parameters(self) -> list[typesys.Parameter] | None:
        """Get the generic parameters for the type.

        Returns:
            The generic parameters, or None if the type is not generic.
        """
        if not self.parameters:
            return None

        params = []
        for _, param in sorted(self.parameters.items()):
            assert isinstance(param.type_info, typesys.TypeVal)
            optional = (
                isinstance(param.type_info, typesys.Instantiation)
                and param.type_info.instantiates is clkbuiltins.OPTIONAL
            )
            params.append(
                typesys.Parameter(
                    name=param.cur_name,
                    type_bound=param.type_info,
                    default=param.init_value,
                    is_optional=optional,
                )
            )
        return params


@dataclass
class SchemaHistory:
    """Track historical information about a schema."""

    version: int
    legacy_became: dict[int, int]
    removed: set[int]

    @classmethod
    def from_cst(
        cls: type[SchemaHistory],
        cst_node: cst.SchemaHistoryBlock,
        module: node.Module,
    ) -> SchemaHistory | None:
        """Create an IR SchemaHistory from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        # Parse version
        version_cst = cst_node.child_version_spec()
        version = int_from_cst(version_cst.child_version(), module.terminals)

        # Parse legacy_became if present
        legacy_became = {}
        new_numbers = set()
        if legacy_became_spec_cst := cst_node.maybe_legacy_became_spec():
            for became_spec_cst in legacy_became_spec_cst.children_became_spec():
                old_number_cst = became_spec_cst.child_old_number()
                old_number = int_from_cst(old_number_cst, module.terminals)
                if old_number in legacy_became:
                    msg = node.append_error_line(
                        old_number_cst, module, f"Duplicate old field number {old_number} in legacy became block"
                    )
                    raise ValueError(msg)
                new_number_cst = became_spec_cst.child_new_number()
                new_number = int_from_cst(new_number_cst, module.terminals)
                if new_number in new_numbers:
                    msg = node.append_error_line(
                        new_number_cst, module, f"Duplicate new field number {new_number} in legacy became block"
                    )
                    raise ValueError(msg)
                legacy_became[old_number] = new_number
                new_numbers.add(new_number)

        # Parse removed if present
        removed = set()
        if removed_spec_cst := cst_node.maybe_removed_spec():
            for field_num_cst in removed_spec_cst.children_num():
                field_num = int_from_cst(field_num_cst, module.terminals)
                if field_num in legacy_became:
                    msg = node.append_error_line(
                        field_num_cst, module, f"Field number {field_num} is in both legacy_became and removed"
                    )
                    raise ValueError(msg)
                removed.add(field_num)

        return cls(version=version, legacy_became=legacy_became, removed=removed)


@dataclass
class Schema(typesys.SchemaType, node.DocRequiredEntity, node.CstNode[cst.Schema]):
    """IR Node representing a schema.

    Attributes:
        fields: Mapping of {field_num: field} of all fields in this schema.
    """

    inner_scope: node.Scope
    uuid: uuid.UUID | None
    parameters: dict[int, FieldDef] | None = dc_field(repr=False)
    fields: dict[int, FieldDef] = dc_field(repr=False)
    field_src_order: dict[int, int] = dc_field(repr=False)
    options: SchemaOptions | None = dc_field(repr=False)
    resolved: ResolvedSchema | None = dc_field(repr=False)
    history: SchemaHistory | None = dc_field(repr=False)
    attributes: node.ClkAttributes | None = dc_field(repr=False)
    programmatically_generated: bool = dc_field(default=False, repr=False)

    @classmethod
    def from_cst(
        cls: type[Schema],
        scope: node.Scope,
        cst_schema: cst.Schema,
        module: node.Module,
    ) -> Schema:
        """Create an IR Schema from a CST Schema.

        This implements the first pass of IR construction; the returned IR Schema will still have unresolved
        identifiers, unevaluated expressions, and potentially unresolved InferenceVars.  The structure will closely
        mirror the syntax tree and include links to the source CST nodes.

        Args:
            scope: The IR scope this schema is defined in.
            cst_schema: The CST node defining this schema.
            module: The IR module (source file) where this schema is defined.
        """
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        attributes = module.handle_outer_attrs(cst_schema.maybe_clk_outer_attrs())
        name = get_span(cst_schema.child_identifier().child_value(), module.terminals)
        uuid_val = (
            uuid.UUID(hex=get_span(uuid_spec.child_uuid(), module.terminals))
            if (uuid_spec := cst_schema.maybe_uuid_spec())
            else None
        )
        inner_scope = scope.make_child_scope(name)
        parameters_block = cst_schema.maybe_schema_parameters_block()
        parameter_defs = (
            [FieldDef.from_cst(cst_node=cst_def, module=module) for cst_def in parameters_block.children_schema_field()]
            if parameters_block
            else []
        )
        parameters = {}
        for parameter in parameter_defs:
            if parameter.num in parameters:
                msg = parameter.append_error_line(f"Duplicate field number {parameter.num}")
                raise ValueError(msg)
            parameters[parameter.num] = parameter
            param_ref = ParameterRef(
                name=parameter.cur_name,
                scope=inner_scope,
                type_info=typesys.InferenceVar.make(context=module, cst_node=parameter.cst_node),
                parameter_def=parameter,
            )
            assert isinstance(parameter.type_info, expr.TypeExpression | typesys.InferenceVar)
            typesys.unify(
                param_ref.type_info,
                (
                    parameter.type_info.inference_var
                    if isinstance(parameter.type_info, expr.TypeExpression)
                    else parameter.type_info
                ),
            )
            inner_scope.define(
                param_ref.name,
                param_ref,
                module.terminals,
            )
        field_block = cst_schema.child_schema_fields_block()
        field_defs = [
            FieldDef.from_cst(cst_node=cst_def, module=module) for cst_def in field_block.children_schema_field()
        ]
        fields = {}
        field_src_order = {}
        for field_src_index, field in enumerate(field_defs):
            if field.num in fields or field.num in parameters:
                msg = field.append_error_line(f"Duplicate field number: {field.num}")
                raise ValueError(msg)
            fields[field.num] = field
            field_ref = FieldRef(
                name=field.cur_name,
                scope=inner_scope,
                type_info=typesys.InferenceVar.make(context=module, cst_node=field.cst_node),
                field_def=field,
            )
            if not isinstance(field.type_info, expr.TypeExpression):
                assert isinstance(field.type_info, typesys.InferenceVar)
                msg = node.append_error_line(
                    field.type_info.cst_node,
                    module,
                    "Fields must have explicit types, not inferred types.",
                )
                raise TypeError(msg)
            typesys.unify(field_ref.type_info, field.type_info.inference_var)
            inner_scope.define(field_ref.name, field_ref, module.terminals)
            field_src_order[field.num] = field_src_index

        schema_options_ir = None
        if schema_options := cst_schema.maybe_schema_options():
            schema_options_ir = SchemaOptions.from_cst(cst_node=schema_options, module=module)

        # Parse history block if present
        history = None
        if history_block := cst_schema.maybe_schema_history_block():
            history = SchemaHistory.from_cst(history_block, module)

        return cls(
            type_info=clkbuiltins.TYPE_TYPE,
            cst_node=cst_schema,
            module=module,
            uuid=uuid_val,
            name=name,
            scope=scope,
            inner_scope=inner_scope,
            doc=node.Doc.from_cst(cst_schema.child_doc(), module),
            parameters=parameters or None,
            fields=fields,
            field_src_order=field_src_order,
            options=schema_options_ir,
            history=history,
            attributes=attributes,
            resolved=None,
        )

    @override
    def generic_parameters(self) -> list[typesys.Parameter] | None:
        """Get the generic parameters for the type.

        Returns:
            The generic parameters, or None if the type is not generic.
        """
        if not self.parameters:
            return None
        result = []
        # First resolve fields, because parameters may have inferred types that
        # depend on their usage in fields.
        for field in self.fields.values():
            field.resolve()
        for _, param in sorted(self.parameters.items()):
            param.resolve()
            assert isinstance(param.type_info, typesys.TypeVal)
            result.append(
                typesys.Parameter(
                    name=param.cur_name,
                    type_bound=param.type_info,
                    default=param.init_value,
                )
            )
        return result

    def _validate_parameters(self) -> None:
        """Validate that parameters with defaults don't appear before parameters without defaults.

        Raises:
            ValueError: If any validation rule is violated
        """
        if not self.parameters:
            return
        seen_default = False
        for _, param in sorted(self.parameters.items()):
            resolved_param = param.get_resolved()
            if resolved_param.init_value:
                seen_default = True
            elif seen_default:
                msg = resolved_param.append_error_line(
                    "Parameters with defaults cannot come before parameters without defaults"
                )
                raise ValueError(msg)

    def _validate_history(self) -> None:
        """Validate schema history.

        Raises:
            ValueError: If any validation rule is violated
        """
        if not self.history:
            return

        current_field_nums = set(self.fields.keys())
        if self.parameters:
            current_field_nums.update(self.parameters.keys())
        historical_nums = set(itertools.chain(self.history.legacy_became.keys(), self.history.removed))
        self._validate_field_number_overlap(current_field_nums, historical_nums)

    def _validate_field_number_overlap(self, current_nums: set[int], historical_nums: set[int]) -> None:
        """Check that current and historical field numbers don't overlap.

        Args:
            current_nums: Set of field numbers in current schema
            historical_nums: Set of field numbers in historical fields

        Raises:
            ValueError: If there is overlap between current and historical field numbers
        """
        overlap = current_nums.intersection(historical_nums)
        if overlap:
            msg = self.append_error_line(f"Field numbers {overlap} are used in both current and historical fields")
            raise ValueError(msg)

    def current_version(self) -> int:
        """Get the current version of the schema."""
        return (
            self.history.version
            if self.history
            else max(list(self.fields.keys()) + (list(self.parameters.keys()) if self.parameters else [-1]))
        )

    def resolve(self) -> ResolvedSchema:
        """Perform finalization of the schema IR.

        This method takes additional passes over the IR to resolve all identifiers, evaluate expressions, and infer
        types.  A consequence of this is that the resolved IR will no longer be a DAG but a general graph, potentially
        with cycles, and many links to the CST nodes will be lost.

        Raises:
            ValueError: If the schema history validation fails
        """
        if self.resolved:
            return self.resolved
        for field in itertools.chain(self.fields.values(), self.parameters.values() if self.parameters else []):
            field.resolve()

        # Validate that parameters with defaults don't come before parameters without defaults
        self._validate_parameters()

        # Fill in history if schema didn't have a history block
        if not self.history:
            self.history = SchemaHistory(version=self.current_version(), legacy_became={}, removed=set())
        self._validate_history()

        self.resolved = ResolvedSchema(
            name=self.name,
            scope=self.scope,
            module=self.module,
            cst_node=self.cst_node,
            doc=self.doc,
            type_info=self.type_info,
            inner_scope=self.inner_scope,
            uuid=self.uuid,
            parameters={param.num: param.get_resolved() for param in self.parameters.values()}
            if self.parameters
            else {},
            fields={fld.num: fld.get_resolved() for fld in self.fields.values()},
            field_src_order=self.field_src_order,
            options=self.options,
            source=self,
            history=self.history,
        )
        return self.resolved

    def get_resolved(self) -> ResolvedSchema:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)
        return self.resolved


@dataclass
class InstantiatedSchema(typesys.TypeVal, typesys.MembershipEntity):
    """A fully instantiated schema with all type substitutions resolved."""

    schema_name: str
    schema_uuid: uuid.UUID | None
    fields: dict[int, InstantiatedFieldDef] = dc_field(repr=False)
    field_src_order: dict[int, int] = dc_field(repr=False)
    options: SchemaOptions | None = dc_field(repr=False)
    schema: ResolvedSchema = dc_field(repr=False)
    arguments: Mapping[str, typesys.Value] | None
    history: SchemaHistory = dc_field(repr=False)

    @staticmethod
    def make(schema: ResolvedSchema, arguments: Mapping[str, typesys.Value]) -> InstantiatedSchema:
        """Make an instantiated schema."""
        return InstantiatedSchema.from_typespec(
            typesys.Instantiation(
                type_info=clkbuiltins.TYPE_TYPE,
                instantiates=schema,
                arguments=arguments,
            )
        )

    @staticmethod
    def from_typespec(
        typespec: Schema | ResolvedSchema | typesys.Instantiation,
    ) -> InstantiatedSchema:
        """Make an instantiated schema.

        Raises:
            TypeError if the typespec is not a schema or instantiated schema.
            ValueError if historical field type changes are incompatible.
        """
        if isinstance(typespec, typesys.Instantiation):
            if not isinstance(typespec.instantiates, Schema | ResolvedSchema):
                msg = f"Can only create InstantiatedSchema from a Schema, not {type(typespec.instantiates)}"
                raise TypeError(msg)
            schema = typespec.instantiates.get_resolved()
            args = typespec.arguments
            result_args = {
                name: value.get_resolved() if isinstance(value, statement.ImmutableBinding) else value
                for name, value in args.items()
            }
            result_args = {
                name: _finalize_type(value) if isinstance(value, typesys.TypeVal) else value
                for name, value in result_args.items()
            }
        else:
            assert isinstance(typespec, Schema | ResolvedSchema)
            schema = typespec.get_resolved()
            # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            args = {}
            result_args = None

        result = InstantiatedSchema(
            type_info=schema.type_info,
            schema_name=schema.name,
            schema_uuid=schema.uuid,
            fields={},
            field_src_order=schema.field_src_order,
            options=schema.options,
            schema=schema,
            arguments=result_args,
            history=schema.history,
        )
        for fld_num, fld in schema.fields.items():
            result.fields[fld_num] = InstantiatedFieldDef(
                module=fld.module,
                cst_node=fld.cst_node,
                doc=fld.doc,
                num=fld_num,
                cur_name=fld.cur_name,
                type_info=substitute_parameter_refs(fld.type_info, args, error_node=typespec),
                init_value=fld.init_value,
                source=fld,
            )

        return result

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        return self.as_instantiation_or_resolved_schema().value_key()

    def as_instantiation_or_resolved_schema(
        self,
    ) -> typesys.Instantiation | ResolvedSchema:
        """Generate an Instantiation or ResolvedSchema for this type."""
        if self.arguments is not None:
            return typesys.Instantiation(
                type_info=clkbuiltins.TYPE_TYPE,
                instantiates=self.schema,
                arguments=self.arguments,
            )
        return self.schema

    def cur_version(self) -> int:
        """Get the current version of the schema."""
        return self.history.version

    @override
    def attribute(self, name: str) -> typesys.Value | None:
        """Look up a field by name and return a ``NamedValue`` with its type.

        Args:
            name: The field name to look up.

        Returns:
            A ``NamedValue`` wrapping the field's type, or ``None`` if not found.
        """
        for field_def in self.fields.values():
            if field_def.cur_name == name:
                return typesys.NamedValue(
                    name=name,
                    scope=self.schema.inner_scope,
                    type_info=field_def.type_info,
                )
        return None


@dataclass
class SchemaOptions(node.CstNode[cst.SchemaOptions]):
    """Options for the Schema type."""

    provide_constructor: bool
    soa_enabled: bool

    @classmethod
    def from_cst(cls: type[SchemaOptions], cst_node: cst.SchemaOptions, module: node.Module) -> SchemaOptions:
        """Create an IR SchemaOptions from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        provide_constructor = False
        soa_enabled = False
        constructor_count = 0
        soa_enabled_count = 0
        for option in cst_node.children_schema_option():
            if constructor := option.maybe_schema_option_constructor():
                constructor_count += 1
                if constructor_count > 1:
                    msg = "Schema option 'constructor' can only be specified once."
                    raise ValueError(msg)
                constructor_type = constructor.child_constructor_expr().maybe_identifier()
                if (
                    not constructor_type
                    or get_span(constructor_type.child_value(), module.terminals) != "source_code_order"
                ):
                    msg = "Unsupported constructor type.  Only `source_code_order` is supported."
                    raise ValueError(msg)
                provide_constructor = True
            elif soa_option := option.maybe_schema_option_soa_enabled():
                soa_enabled_count += 1
                if soa_enabled_count > 1:
                    msg = "Schema option 'soa_enabled' can only be specified once."
                    raise ValueError(msg)
                boolean_cst = soa_option.child_boolean()
                soa_enabled = boolean_cst.maybe_true() is not None
            else:
                msg = "Receieved unsupported schema option."
                raise ValueError(msg)
        return SchemaOptions(
            cst_node=cst_node, module=module, provide_constructor=provide_constructor, soa_enabled=soa_enabled
        )


@dataclass
class InstantiatedFieldDef(node.DocRequiredEntity, node.CstNode[cst.SchemaField]):
    """Fully resolved version of FieldDef node."""

    num: int
    cur_name: str
    type_info: typesys.TypeVal
    init_value: typesys.Value | None
    source: ResolvedFieldDef | None = dc_field(repr=False)


@dataclass
class ResolvedFieldDef(node.DocRequiredEntity, node.CstNode[cst.SchemaField]):
    """Fully resolved version of FieldDef node."""

    num: int
    cur_name: str
    type_info: typesys.TypeVal | ParameterRef
    init_value: typesys.Value | None
    source: FieldDef | None = dc_field(repr=False)


@dataclass
class FieldDef(node.DocableEntity, node.CstNode[cst.SchemaField]):
    """IR Node representing a schema field.

    Attributes:
        num: Field number
        cur_name: The most recent name of this field; it may have had different names in historical versions.
        type_info: The type of this field; will be TypeVal when fully resolved, or Expr until resolved.
        init_value: The initial value for this field, or None.
    """

    num: int
    cur_name: str
    type_info: typesys.TypeVal | ParameterRef | expr.TypeExpression | typesys.InferenceVar
    init_value: typesys.Value | expr.Expr | None
    resolved: ResolvedFieldDef | None = dc_field(repr=False)

    @classmethod
    def from_cst(cls: type[FieldDef], cst_node: cst.SchemaField, module: node.Module) -> FieldDef:
        """Create an IR FieldDef from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        typespec_cst = cst_node.maybe_typespec()
        result = cls(
            module=module,
            cst_node=cst_node,
            doc=node.Doc.maybe_from_cst(cst_doc=cst_node.maybe_doc(), module=module),
            num=int_from_cst(cst_node.child_field_num(), module.terminals),
            cur_name=get_span(cst_node.child_name().child_value(), module.terminals),
            type_info=(
                expr.TypeExpression.make(expr.Expr.from_cst(typespec_cst, module))
                if typespec_cst
                else typesys.InferenceVar.make(context=module, cst_node=cst_node)
            ),
            init_value=(expr.Expr.from_cst(init_cst, module) if (init_cst := cst_node.maybe_init_value()) else None),
            resolved=None,
        )
        assert isinstance(result.type_info, expr.TypeExpression | typesys.InferenceVar)
        if result.init_value:
            typesys.unify(
                result.init_value.type_info,
                (
                    result.type_info.inference_var
                    if isinstance(result.type_info, expr.TypeExpression)
                    else result.type_info
                ),
            )
        return result

    def resolve(self) -> ResolvedFieldDef:
        """Perform IR finalization."""
        if self.resolved:
            return self.resolved
        assert isinstance(self.type_info, expr.TypeExpression | typesys.InferenceVar)

        type_info = _resolve_field_type(self.type_info, self.type_info.cst_node, self.module)

        if isinstance(self.init_value, expr.Expr):
            try:
                typesys.unify(type_info, self.init_value.type_info)
            except TypeError as e:
                msg = self.init_value.append_error_line(
                    f"Initial value expression has wrong type; details follow:\n{e}",
                )
                raise TypeError(msg) from e
            init_value = self.init_value.evaluate()
            self.init_value = init_value
        self.type_info = type_info
        if self.doc is None:
            msg = self.append_error_line(f"Field {self.cur_name} is missing documentation")
            raise ValueError(msg)
        self.resolved = ResolvedFieldDef(
            module=self.module,
            cst_node=self.cst_node,
            doc=self.doc,
            num=self.num,
            cur_name=self.cur_name,
            type_info=self.type_info,
            init_value=self.init_value,
            source=self,
        )
        return self.resolved

    def get_resolved(self) -> ResolvedFieldDef:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)
        return self.resolved


@dataclass
class ParameterRef(node.NamedEntity, typesys.DeferrableType):
    """A reference to a generic parameter."""

    parameter_def: FieldDef

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        msg = f"Attempt to generate a value key for an unsubstituted generic parameter: {self}"
        raise RuntimeError(msg)


def _finalize_type(typ: typesys.TypeVal) -> typesys.TypeVal:
    """If the type is a schema or schema instantiation, convert it to InstantiatedSchema."""
    if isinstance(typ, typesys.Instantiation) and typ.instantiates is clkbuiltins.UUID:
        # Prevent recursion when a schema type is used as a tag type for UUIDs
        return typ
    if isinstance(typ, statement.InstantiateStmt) and isinstance(typ.typespec, typesys.Instantiation):
        # Let it fall through to the rest of the checks.
        typ = typ.typespec
    if isinstance(typ, Schema | ResolvedSchema | typesys.Instantiation):
        try:
            return InstantiatedSchema.from_typespec(typ)
        except TypeError:
            pass

    if isinstance(typ, statement.ImmutableBinding):
        assert isinstance(typ.type_info, typesys.InferenceVar)
        resolution = typ.type_info.resolution()
        assert isinstance(resolution, typesys.TypeVal)
        return resolution

    if isinstance(typ, clkenum.ClkEnum):
        return typ.get_resolved()
    return typ


def _substitute_constants(args: dict[str, typesys.Value]) -> None:
    """Substitute constant values in arguments.

    Args:
        args: Dictionary of arguments to process.
    """
    for arg_name, arg_val in args.items():
        if isinstance(arg_val, statement.ImmutableBinding):
            assert isinstance(arg_val.value, typesys.Value)
            args[arg_name] = arg_val.value


def _substitute_parameter_ref(
    param_ref: ParameterRef,
    args: Mapping[str, typesys.Value],
    error_node: Any,  # noqa: ANN401 (Any required for flexibility in error reporting)
) -> typesys.TypeVal:
    """Substitute a ParameterRef with its value from args.

    Args:
        param_ref: The parameter reference to substitute.
        args: Parameter names and values currently in scope.
        error_node: Node to use for error reporting.

    Returns:
        The substituted type value.

    Raises:
        TypeError: If the parameter value is not a type.
    """
    param_val = args[param_ref.name]
    if not isinstance(param_val, typesys.TypeVal):
        msg = node.enrich_error_if_possible(
            error_node, f"Parameter {param_ref.name} used in type context, but it has non-type value {param_val}"
        )
        raise TypeError(msg)
    return _finalize_type(param_val)


def _substitute_instantiation_base(
    instantiation: typesys.Instantiation,
    args: Mapping[str, typesys.Value],
    error_node: Any,  # noqa: ANN401 (Any required for flexibility in error reporting)
) -> None:
    """Substitute the base type of an instantiation if it's a parameter reference.

    Args:
        instantiation: The instantiation to process.
        args: Parameter names and values currently in scope.
        error_node: Node to use for error reporting.

    Raises:
        TypeError: If the parameter value is not a type.
    """
    if isinstance(instantiation.instantiates, ParameterRef):
        name = instantiation.instantiates.parameter_def.cur_name
        type_param = args[name]
        if not isinstance(type_param, typesys.TypeVal):
            msg = node.enrich_error_if_possible(
                error_node, f"Parameter {name} used in instantiation expression, but it has non-type value {type_param}"
            )
            raise TypeError(msg)
        instantiation.instantiates = _finalize_type(type_param)


def _process_single_argument(
    arg_val: typesys.Value,
    args: Mapping[str, typesys.Value],
    error_node: Any,  # noqa: ANN401 (Any required for flexibility in error reporting)
) -> typesys.Value:
    """Process a single argument in an instantiation.

    Args:
        arg_val: The argument value to process.
        args: Parameter names and values currently in scope.
        error_node: Node to use for error reporting.

    Returns:
        The processed argument value.

    Raises:
        ValueError: If a referenced parameter is not found in arguments.
    """
    if isinstance(arg_val, ParameterRef):
        try:
            return args[arg_val.parameter_def.cur_name]
        except KeyError as e:
            msg = node.enrich_error_if_possible(
                error_node, f"Parameter {arg_val.parameter_def.cur_name} not found in arguments"
            )
            raise ValueError(msg) from e
    elif isinstance(arg_val, typesys.Instantiation):
        return substitute_parameter_refs(arg_val, args, error_node)

    return arg_val


def _substitute_instantiation_arguments(
    instantiation: typesys.Instantiation,
    args: Mapping[str, typesys.Value],
    result_args: dict[str, typesys.Value],
    error_node: Any,  # noqa: ANN401 (Any required for flexibility in error reporting)
) -> None:
    """Substitute all arguments in an instantiation.

    Args:
        instantiation: The instantiation containing the arguments.
        args: Parameter names and values currently in scope.
        result_args: Dictionary to populate with processed arguments.
        error_node: Node to use for error reporting.
    """
    for arg_name, arg_val in instantiation.arguments.items():
        arg_result = _process_single_argument(arg_val, args, error_node)

        # Trying to finalize UUID tag types can lead to recursion
        if isinstance(arg_result, typesys.TypeVal) and instantiation.instantiates is not clkbuiltins.UUID:
            arg_result = _finalize_type(arg_result)

        result_args[arg_name] = arg_result


def substitute_parameter_refs(
    type_info: typesys.TypeVal | ParameterRef,
    args: Mapping[str, typesys.Value],
    error_node: Any,  # noqa: ANN401 (Any required for flexibility in error reporting)
) -> typesys.TypeVal:
    """Substitute ParameterRefs recursively in types.

    Note: Any schema types which are encountered, recursively, will be
    transformed to InstantiatedSchema.

    Args:
        type_info: Any type, possibly an instantiated type, in which to substitute parameters.
        args: Parameter names and values currently in scope and eligible for instantiation.
        error_node: Node to use for error reporting.

    Returns:
        A type with all parameters substituted (potentially the same type that
        was passed in).
    """
    if isinstance(type_info, ParameterRef):
        return _substitute_parameter_ref(type_info, args, error_node)

    if not isinstance(type_info, typesys.Instantiation):
        return _finalize_type(type_info)

    result_args: dict[str, typesys.Value] = {}
    result = typesys.Instantiation(
        type_info=type_info.type_info,
        instantiates=type_info.instantiates,
        arguments=result_args,
    )

    _substitute_instantiation_base(result, args, error_node)
    _substitute_instantiation_arguments(type_info, args, result_args, error_node)
    _substitute_constants(result_args)

    return _finalize_type(result)


@dataclass
class FieldRef(node.NamedEntity, typesys.Value):
    """A reference to a generic parameter."""

    field_def: FieldDef

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        msg = f"Attempt to generate a value key for an unsubstituted field reference: {self}"
        raise RuntimeError(msg)


def retrieve_schema(value: typesys.Value | None) -> InstantiatedSchema | str:
    """Try to convert an argument to a Schema.

    Args:
        value: Either a schema or an instantiation of a schema.

    Return: A schema or an error message.
    """
    if isinstance(value, statement.InstantiateStmt):
        if not isinstance(value.typespec, typesys.Instantiation):
            return "Value must be an instantiation."
        if not isinstance(value.typespec.instantiates, Schema | ResolvedSchema):
            return "Instantiation must be a Schema."
        value = value.typespec
    if not isinstance(value, Schema | typesys.Instantiation):
        return "Argument must be a Schema or Instantiated Schema."
    schema_ir = value if isinstance(value, Schema) else value.instantiates
    if not isinstance(schema_ir, Schema):
        return f"Interface must be instantiated for a schema: {schema_ir}"
    return InstantiatedSchema.from_typespec(value)


@dataclass
class SchemaInstance(typesys.ObjectIdentityValue):
    """An instance of a schema."""

    schema: InstantiatedSchema
    data: dict[str, typesys.Value]

    @classmethod
    def from_unresolved_bindings(
        cls: type[SchemaInstance],
        schema_ir: InstantiatedSchema,
        bindings: Iterable[statement.ImmutableBinding],
        error_report_node: node.CstNodeProtocol | None,
        error_report_module: node.Module,
    ) -> SchemaInstance:
        """Create a SchemaInstance from a set of unresolved binding statements.

        Note: As a side effect, this will resolve the bindings.
        """
        data = []
        for binding in bindings:
            binding.resolve()
            data.append((binding.name, binding.value))
        return cls.from_args(
            schema_ir=schema_ir,
            args=data,
            error_report_node=error_report_node,
            error_report_module=error_report_module,
        )

    @classmethod
    def from_args(
        cls: type[SchemaInstance],
        schema_ir: InstantiatedSchema,
        args: Iterable[tuple[str, typesys.Value]],
        error_report_node: node.CstNodeProtocol | None,
        error_report_module: node.Module,
    ) -> SchemaInstance:
        """Create a SchemaInstance from a set of arguments."""
        data = {}
        fields = {fld.cur_name: fld for fld in schema_ir.fields.values()}
        for arg_name, arg_value in args:
            try:
                field = fields[arg_name]
            except KeyError:
                msg = node.append_error_line(
                    error_report_node,
                    error_report_module,
                    f"No such field {arg_name} in schema {schema_ir.schema_name}, or field specified more than once",
                )
                raise AttributeError(msg)  # noqa: B904
            del fields[arg_name]
            try:
                typesys.unify(arg_value.type_info, field.type_info)
            except TypeError:
                if (
                    isinstance(field.type_info, typesys.Instantiation)
                    and field.type_info.instantiates is clkbuiltins.OPTIONAL
                ):
                    optional_type = field.type_info.arguments["type"]
                    assert isinstance(optional_type, typesys.TypeVal)
                    typesys.unify(arg_value.type_info, optional_type)
                else:
                    raise
            data[arg_name] = arg_value
        for field in fields.values():
            if not field.init_value:
                msg = node.append_error_line(
                    error_report_node,
                    error_report_module,
                    f"Missing value for field {field.cur_name} of schema {schema_ir.schema_name}",
                )
                raise ValueError(msg)
            data[field.cur_name] = field.init_value
        return cls(type_info=schema_ir, schema=schema_ir, data=data)


# We have to suppress PLR0913 (too many args) because this is already an extremely simple function
# that can't be split but still needs all these args. The args are all different types so mypy will
# catch any mixups in the call sites, and we have made the args kwonly as extra assurance.
def make_field(  # noqa: PLR0913 (see above)
    module: node.Module,
    num: int,
    name: str,
    typespec: typesys.TypeVal,
    init_value: typesys.Value | None = None,
    doc: str = "Programmatically-generated field",
) -> FieldDef:
    """Factory function for programmatically-generated fields."""
    doc_ir = node.Doc(module=module, cst_node=None, value=doc)
    result = FieldDef(
        module=module,
        cst_node=None,
        doc=doc_ir,
        num=num,
        cur_name=name,
        type_info=typespec,
        init_value=init_value,
        resolved=None,
    )
    result.resolved = ResolvedFieldDef(
        module=module,
        cst_node=None,
        doc=doc_ir,
        num=num,
        cur_name=name,
        type_info=typespec,
        init_value=init_value,
        source=result,
    )
    return result


def make_schema_options(
    module: node.Module, provide_constructor: bool = False, soa_enabled: bool = False
) -> SchemaOptions:
    """Factory function for creating schemas programmatically."""
    return SchemaOptions(module=module, cst_node=None, provide_constructor=provide_constructor, soa_enabled=soa_enabled)


# We have to suppress PLR0913 (too many args) because this is already an extremely simple function
# that can't be split but still needs all these args. The args are all different types so mypy will
# catch any mixups in the call sites, and we have made the args kwonly as extra assurance.
def make_schema_class(  # noqa: PLR0913 (see above)
    name: str,
    module: node.Module,
    parameters: Iterable[FieldDef] = (),
    fields: Iterable[FieldDef] = (),
    doc: str = "Programmatically-generated schema",
    options: SchemaOptions | None = None,
    uuid: uuid.UUID | None = None,
) -> InstantiatedSchema:
    """Utility function for creating schemas progammatically."""
    if not options:
        options = make_schema_options(module)
    schema = Schema(
        name=name,
        module=module,
        scope=module.inner_scope,
        cst_node=None,
        doc=node.Doc(module=module, cst_node=None, value=doc),
        type_info=clkbuiltins.TYPE_TYPE,
        inner_scope=module.inner_scope.make_child_scope(name),
        uuid=uuid,
        parameters={param.num: param for param in parameters},
        fields={fld.num: fld for fld in fields},
        field_src_order={fld.num: i for i, fld in enumerate(fields)},
        options=options,
        resolved=None,
        history=None,
        attributes=None,
        programmatically_generated=True,
    )
    schema.resolve()
    return InstantiatedSchema.from_typespec(schema)


def _validate_bitset_size(
    bitset: typesys.Instantiation,
    cst_node: node.CstNodeProtocol | None,
    module: node.Module,
) -> None:
    """Reject a zero-sized Bitset field type."""
    bitset_size = bitset.arguments["size"]
    if isinstance(bitset_size, statement.ImmutableBinding):
        bitset_size = bitset_size.get_resolved()
    if isinstance(bitset_size, primitive.DecimalValue) and primitive.unsigned_decimal_to_int(bitset_size) == 0:
        msg = node.append_error_line(cst_node=cst_node, module=module, msg="Bitset size must be greater than zero")
        raise ValueError(msg)


def _resolve_field_type(
    type_info: typesys.TypeVal | ParameterRef | expr.TypeExpression | typesys.InferenceVar,
    cst_node: node.CstNodeProtocol | None,
    module: node.Module,
) -> typesys.TypeVal | ParameterRef:
    """Resolve a field type expression into a TypeVal or ParameterRef.

    Args:
        type_info: The type to resolve
        cst_node: CST node to use for error reporting
        module: Module context for error reporting

    Returns:
        A resolved TypeVal or ParameterRef
    """
    result: typesys.TypeVal | ParameterRef
    if isinstance(type_info, typesys.InferenceVar):
        resolution = type_info.resolution()
        if not isinstance(resolution, typesys.TypeVal):
            msg = node.append_error_line(
                cst_node=cst_node,
                module=module,
                msg="Type inference failed; not enough information to infer type",
            )
            raise TypeError(msg)
        result = resolution
    elif isinstance(type_info, expr.TypeExpression):
        eval_result = type_info.evaluate()
        if not isinstance(eval_result, typesys.TypeVal | ParameterRef):
            msg = node.append_error_line(
                cst_node=cst_node,
                module=module,
                msg="Error in type exression; received Deferrable other than ParameterRef",
            )
            raise TypeError(msg)
        result = eval_result
    else:
        result = type_info

    if isinstance(result, _INVALID_FIELD_TYPES):
        msg = node.append_error_line(
            cst_node=None,
            module=module,
            msg=f"Type {result.name} is not a valid field type.",
        )
        raise TypeError(msg)

    if isinstance(result, typesys.Instantiation) and result.instantiates is tensor_builtins.TENSOR:
        tensor_builtins.resolve_tensor_parameters(result)

    if isinstance(result, typesys.Instantiation) and result.instantiates is clkbuiltins.BITSET:
        _validate_bitset_size(result, cst_node, module)

    if isinstance(result, typesys.TypeVal) and result.generic_parameters() is not None:
        msg = node.append_error_line(
            cst_node=cst_node,
            module=module,
            msg="Generic type must be instantiated when used as a field type",
        )
        raise TypeError(msg)
    return result


def get_python_type(typ: typesys.TypeVal) -> type[Any]:  # noqa: C901, PLR0911 (need to handle all the types)
    """Get the Python type that corresponds to a Clockwork type.

    NB: This only handles types that resolve to a primitive or to `str`. It does not handle container types or Optional.

    Args:
        typ: The Clockwork type to map

    Returns:
        The corresponding Python type

    Raises:
        TypeError: If the type cannot be mapped to a Python type
    """
    if isinstance(typ, clkbuiltins.PrimitiveType):
        if typ is clkbuiltins.BOOL:
            return bool
        if typ in (
            clkbuiltins.BYTE,
            clkbuiltins.INT8,
            clkbuiltins.INT16,
            clkbuiltins.INT32,
            clkbuiltins.INT64,
            clkbuiltins.UINT8,
            clkbuiltins.UINT16,
            clkbuiltins.UINT32,
            clkbuiltins.UINT64,
        ):
            return int
        if typ in (clkbuiltins.FLOAT32, clkbuiltins.FLOAT64):
            return float

    if typ in (clkbuiltins.DURATION, clkbuiltins.SYNC_TIME):
        return int

    if isinstance(typ, strongtypes.StrongType):
        if not isinstance(typ.typespec, typesys.TypeVal):
            msg = f"Attempt to access Python type of unresolved StrongType {typ}"
            raise RuntimeError(msg)  # noqa: TRY004  RuntimeError because this is probably a compiler bug
        return get_python_type(typ.typespec)

    if isinstance(typ, typesys.Instantiation):
        if typ.instantiates is clkbuiltins.VAR_STRING:
            return str
        if typ.instantiates is clkbuiltins.UUID:
            return uuid.UUID

    msg = f"Cannot map {typ} to a Python type"
    raise TypeError(msg)


def _is_container_type(typ: typesys.TypeVal) -> bool:
    """Check if a type is a container type (Optional, VarArray, FixedArray)."""
    if not isinstance(typ, typesys.Instantiation):
        return False
    return typ.instantiates in (
        # keep-sorted start
        clkbuiltins.FIXED_ARRAY,
        clkbuiltins.FIXED_SOA,
        clkbuiltins.OPTIONAL,
        clkbuiltins.VAR_ARRAY,
        clkbuiltins.VAR_SOA,
        clkbuiltins.VAR_STRING,
        # keep-sorted end
    )


def _get_container_inner_type(typ: typesys.TypeVal) -> typesys.TypeVal:
    """Get the inner type of a container type."""
    if not isinstance(typ, typesys.Instantiation):
        msg = f"Not a container type: {typ}"
        raise TypeError(msg)
    if typ.instantiates is clkbuiltins.VAR_STRING:
        return clkbuiltins.BYTE
    result = typ.arguments["type"]
    if not isinstance(result, typesys.TypeVal):
        msg = f"Invalid inner type for container type {typ}: {result}"
        raise TypeError(msg)
    return result


def _check_identical_types(old_type: typesys.TypeVal, new_type: typesys.TypeVal) -> bool | None:
    """Check if two types are identical.

    Args:
        old_type: The type of the historical field
        new_type: The type of the field that replaced it

    Returns:
        True if the types are identical, None otherwise to indicate further checks needed
    """
    if old_type.value_key() == new_type.value_key():
        return True
    return None


def _check_python_type_compatibility(old_type: typesys.TypeVal, new_type: typesys.TypeVal) -> bool | None:
    """Check if two types have compatible Python types.

    Args:
        old_type: The type of the historical field
        new_type: The type of the field that replaced it

    Returns:
        True if Python types match, False if they don't, None if can't determine
    """
    try:
        old_python_type = get_python_type(old_type)
        new_python_type = get_python_type(new_type)
    except TypeError:
        # Not primitive types that can be mapped to Python types
        return None
    else:
        return old_python_type is new_python_type


def _check_container_compatibility(old_type: typesys.TypeVal, new_type: typesys.TypeVal) -> bool | None:
    """Check compatibility of container types.

    Args:
        old_type: The type of the historical field
        new_type: The type of the field that replaced it

    Returns:
        True if containers are compatible, False if not, None if not applicable
    """
    old_is_container = _is_container_type(old_type)
    new_is_container = _is_container_type(new_type)

    if not (old_is_container or new_is_container):
        return None

    if old_is_container and new_is_container:
        # Both are containers, check their inner types
        return _are_types_compatible(_get_container_inner_type(old_type), _get_container_inner_type(new_type))

    if old_is_container:
        # [T] -> U: check if T is compatible with U
        return _are_types_compatible(_get_container_inner_type(old_type), new_type)

    assert new_is_container
    # T -> [U]: check if T is compatible with U
    return _are_types_compatible(old_type, _get_container_inner_type(new_type))


def _check_varstring_compatibility(old_type: typesys.TypeVal, new_type: typesys.TypeVal) -> bool | None:
    """Check compatibility of primitive type with string type.

    Args:
        old_type: The type of the historical field
        new_type: The type of the field that replaced it

    Returns:
        True if types are compatible, False if not, None if not applicable
    """
    return (
        True
        if old_type in (clkbuiltins.INT8, clkbuiltins.UINT8, clkbuiltins.BYTE)
        and isinstance(new_type, typesys.Instantiation)
        and new_type.instantiates is clkbuiltins.VAR_STRING
        else None
    )


def _check_enum_compatibility(old_type: typesys.TypeVal, new_type: typesys.TypeVal) -> bool | None:
    """Check if an integer type is compatible with an enum.

    Args:
        old_type: The type of the historical field
        new_type: The type of the field that replaced it

    Returns:
        True if compatible, None if not applicable
    """
    if isinstance(old_type, clkbuiltins.IntegerPrimitiveType) and isinstance(new_type, clkenum.ResolvedEnum):
        # The enum must have explicitly assigned values to be compatible with an integer field
        return new_type.has_explicit_values
    return None


def _check_schema_compatibility(old_type: typesys.TypeVal, new_type: typesys.TypeVal) -> bool | None:
    """Check compatibility between schema types.

    Args:
        old_type: The type of the historical field
        new_type: The type of the field that replaced it

    Returns:
        True if schemas are compatible, False if not, None if not applicable
    """
    if not (isinstance(new_type, InstantiatedSchema) and isinstance(old_type, InstantiatedSchema)):
        return None

    if new_type.schema.uuid != old_type.schema.uuid:
        return False

    # Different instantiations of the same schema are compatible if each of their fields are compatible
    # This will recursively descend into fields with schema types
    for field_num, field in new_type.fields.items():
        old_field = old_type.fields[field_num]
        if not _are_types_compatible(old_field.type_info, field.type_info):
            return False
    return True


def _are_types_compatible(old_type: typesys.TypeVal, new_type: typesys.TypeVal) -> bool:
    """Check if two types are compatible for field type changes.

    Args:
        old_type: The type of the historical field
        new_type: The type of the field that replaced it

    Returns:
        True if the types are compatible, False otherwise
    """
    compatibility_checks = [
        _check_identical_types,
        _check_varstring_compatibility,
        _check_python_type_compatibility,
        _check_container_compatibility,
        _check_enum_compatibility,
        _check_schema_compatibility,
    ]

    for check in compatibility_checks:
        result = check(old_type, new_type)
        if result is not None:
            return result

    # If none of the checks determined compatibility, types are incompatible
    return False
