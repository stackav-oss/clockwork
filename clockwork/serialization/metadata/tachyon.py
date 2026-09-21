# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Metadata generation and utilities for Tachyon representations."""

from __future__ import annotations

from decimal import Decimal
from typing import TYPE_CHECKING
from uuid import UUID

from clockwork.dsl.ir import clkbuiltins, clkenum, expr, node, primitive, schema, statement, strongtypes, typesys
from clockwork.dsl.serialization import tachyon_layout_reg, tachyon_reg
from clockwork.serialization.metadata import tachyon_model as model
from clockwork.serialization.metadata import tachyon_model_pb2 as model_pb2

if TYPE_CHECKING:
    from clockwork.dsl.compiler_context import CompilerContext


def _init_value_from_decimal_value(
    fld: schema.InstantiatedFieldDef,
    value: primitive.DecimalValue,
) -> model.InitialValue | None:
    """Get a field initial value from a decimal value.

    Arguments:
        fld: Instantiated schema field.
        value: Decimal value.

    Returns:
        Field initial value.
    """
    value_type = fld.type_info
    if isinstance(value_type, strongtypes.StrongType):
        value_type = value_type.get_underlying_type()
    if isinstance(value_type, clkbuiltins.IntegerPrimitiveType) and value_type.signed:
        return model.SignedInitialValue(value=int(value.value))
    if isinstance(value_type, clkbuiltins.IntegerPrimitiveType) and not value_type.signed:
        return model.UnsignedInitialValue(value=int(value.value))
    if isinstance(value_type, clkbuiltins.FloatingPointPrimitiveType):
        return model.FloatInitialValue(value=value.value)
    msg = fld.append_error_line("Unsupported initial value: {value_type}")
    raise TypeError(msg)


def _init_value_from_enum_value(fld: schema.InstantiatedFieldDef, value: clkenum.ValueRef) -> model.InitialValue | None:
    """Get a field initial value from an enum value.

    Arguments:
        fld: Instantiated schema field.
        value: Enum value.

    Returns:
        Field initial value.
    """
    enum_value = value.value_def.integer_value
    if isinstance(enum_value, expr.Expr):
        enum_value = enum_value.evaluate()
    assert isinstance(enum_value, int)
    if not isinstance(fld.type_info, clkenum.ResolvedEnum):
        msg = fld.append_error_line("Unsupported initial value")
        raise TypeError(msg)
    if fld.type_info.underlying_type in [clkbuiltins.INT8, clkbuiltins.INT16, clkbuiltins.INT32, clkbuiltins.INT64]:
        return model.SignedInitialValue(value=enum_value)
    if fld.type_info.underlying_type in [clkbuiltins.UINT8, clkbuiltins.UINT16, clkbuiltins.UINT32, clkbuiltins.UINT64]:
        return model.UnsignedInitialValue(value=enum_value)
    msg = fld.append_error_line("Unsupported initial value")
    raise TypeError(msg)


def _init_value_from_schema_field(
    schema_ir: schema.InstantiatedSchema, fld: schema.InstantiatedFieldDef
) -> model.InitialValue | None:
    """Get the initial value from a typesys value.

    Arguments:
        schema_ir: Instantiated schema definition.
        fld: Instantiated field definition.

    Returns:
        Initial value or None.
    """
    value = fld.init_value
    if isinstance(value, schema.ParameterRef):
        if not schema_ir.arguments or value.name not in schema_ir.arguments:
            msg = fld.append_error_line(f"Unknown schema parameter {value.name} in initial value")
            raise TypeError(msg)
        value = schema_ir.arguments[value.name]

    if value is None or isinstance(value, clkbuiltins.Nullopt):
        return None

    if isinstance(value, primitive.DecimalValue):
        return _init_value_from_decimal_value(fld, value)

    if value is clkbuiltins.FALSE_VALUE:
        return model.BoolInitialValue(value=False)

    if value is clkbuiltins.TRUE_VALUE:
        return model.BoolInitialValue(value=True)

    if isinstance(value, clkenum.ValueRef):
        return _init_value_from_enum_value(fld, value)

    msg = fld.append_error_line("Unsupported initial value")
    raise TypeError(msg)


def _init_value_to_protobuf(value: model.InitialValue | None, pb_fld: model_pb2.SchemaField) -> None:
    """Convert model initial value to proto initial value in a schema field.

    Arguments:
        value: Model initial value.
        pb_fld: Schema field protobuf.
    """
    if value is None:
        return
    if isinstance(value, model.SignedInitialValue):
        pb_fld.init_value.signed_value = value.value
        return
    if isinstance(value, model.UnsignedInitialValue):
        pb_fld.init_value.unsigned_value = value.value
        return
    if isinstance(value, model.FloatInitialValue):
        pb_fld.init_value.float_value = str(value.value)
        return
    pb_fld.init_value.bool_value = value.value


def _init_value_from_pb(pb_fld: model_pb2.SchemaField) -> model.InitialValue | None:
    """Convert proto initial value to model initial value.

    Arguments:
        pb_fld: Schema field protobuf.

    Returns:
        Model initial value or None.
    """
    if not pb_fld.HasField("init_value"):
        return None
    if pb_fld.init_value.HasField("signed_value"):
        return model.SignedInitialValue(value=pb_fld.init_value.signed_value)
    if pb_fld.init_value.HasField("unsigned_value"):
        return model.UnsignedInitialValue(value=pb_fld.init_value.unsigned_value)
    if pb_fld.init_value.HasField("float_value"):
        return model.FloatInitialValue(value=Decimal(pb_fld.init_value.float_value))
    if pb_fld.init_value.HasField("bool_value"):
        return model.BoolInitialValue(value=pb_fld.init_value.bool_value)

    msg = f"Unsupported value proto: {pb_fld.init_value}"
    raise TypeError(msg)


class Builder:
    """Tachyon Metadata Builder."""

    def __init__(self, compiler_context: CompilerContext) -> None:
        """Create a Builder."""
        self.compiler_context = compiler_context
        self.types: list[model.ClkType] = []
        self.value_key_to_id: dict[str, int] = {}

    def get_metadata(self, outer_type_id: int) -> model.TachyonMetadata:
        """Create a TachyonMetadata."""
        return model.TachyonMetadata(outer_type_id=outer_type_id, types=self.types[:])

    def handle_type(self, typ: typesys.TypeVal) -> int:  # noqa: PLR0911 (need to handle all the types)
        """Register a type if needed and return the type ID."""
        key = typ.value_key()
        try:
            return self.value_key_to_id[key]
        except KeyError:
            pass
        if isinstance(typ, statement.InstantiateStmt):
            assert isinstance(typ.typespec, typesys.Instantiation)
            return self._handle_schema(schema.InstantiatedSchema.from_typespec(typ.typespec), key)
        if isinstance(typ, schema.InstantiatedSchema):
            return self._handle_schema(typ, key)
        if isinstance(typ, clkenum.ResolvedEnum):
            return self._handle_enum(typ, key)
        if isinstance(typ, strongtypes.Tag):
            return self._handle_tag(typ, key)
        if isinstance(typ, strongtypes.StrongType):
            return self._handle_strong_type(typ, key)
        inner = _get_inner_typedef(typ)
        builtin_type = clkbuiltins.BUILTINS_SCOPE.lookup(inner.name, recursive=False)
        if builtin_type is not inner:
            msg = f"Unrecognized type {key} {type(typ)}"
            raise TypeError(msg)
        if isinstance(typ, typesys.Instantiation) and typ.instantiates in (
            clkbuiltins.FIXED_SOA,
            clkbuiltins.VAR_SOA,
        ):
            return self._handle_soa(typ, key)
        return self._handle_builtin(typ, key)

    def _handle_builtin(self, typ: typesys.TypeVal, key: str) -> int:
        inner = _get_inner_typedef(typ)
        assert inner.scope is clkbuiltins.BUILTINS_SCOPE
        if not isinstance(inner, clkbuiltins.SerializableBuiltin):
            msg = node.enrich_error_if_possible(inner, f"Builtin type {key} {type(inner)} is not serializable")
            raise TypeError(msg)
        constraint = tachyon_reg.constraint_for_type(self.compiler_context, typ)
        if constraint is None:
            msg = f"No Tachyon representation for {key}"
            raise ValueError(msg)
        result = model.BuiltInType(
            fqn=inner.fqn,
            uuid=inner.uuid,
            size=constraint.size,
            alignment=constraint.alignment,
            arguments=(),
        )
        type_id = len(self.types)
        self.types.append(result)
        self.value_key_to_id[key] = type_id
        args = []
        if isinstance(typ, typesys.Instantiation):
            params = inner.generic_parameters()
            assert params is not None
            for param in params:
                arg = typ.arguments[param.name]
                arg_val: int | str
                if param.type_bound is clkbuiltins.TYPE_TYPE or isinstance(arg, typesys.TypeVal):
                    assert param.type_bound is clkbuiltins.TYPE_TYPE
                    assert isinstance(arg, typesys.TypeVal)
                    if typ.instantiates is clkbuiltins.UUID and isinstance(arg, schema.Schema):
                        arg = schema.InstantiatedSchema.from_typespec(arg)
                    arg_val = self.handle_type(arg)
                else:
                    arg_val = arg.value_key()
                args.append(arg_val)
        result.arguments = tuple(args)
        result.get_hash(self.types)
        return type_id

    def _handle_schema(self, schema_ir: schema.InstantiatedSchema, key: str) -> int:
        if schema_ir.schema_uuid is None:
            msg = f"Cannot generate Tachyon metadata for schema without UUID: {schema_ir.value_key()}"
            raise ValueError(msg)
        constraint = tachyon_reg.constraint_for_type(self.compiler_context, schema_ir)
        layout = tachyon_layout_reg.layout_for_type(self.compiler_context, schema_ir)
        if constraint is None or layout is None:
            msg = f"No Tachyon representation for {key}"
            raise ValueError(msg)
        result = model.SchemaType(
            fqn=schema_ir.schema.value_key(),
            size=constraint.size,
            alignment=constraint.alignment,
            schema_uuid=schema_ir.schema_uuid,
            version=schema_ir.cur_version(),
            arguments=(),
            fields=(),
            removed=schema_ir.history.removed,
            became=schema_ir.history.legacy_became,
        )
        type_id = len(self.types)
        self.types.append(result)
        self.value_key_to_id[key] = type_id
        args = []
        if schema_ir.arguments:
            for schema_arg in schema_ir.arguments:
                arg = schema_ir.arguments[schema_arg]
                arg_val: int | str = self.handle_type(arg) if isinstance(arg, typesys.TypeVal) else arg.value_key()
                args.append(arg_val)
        result.arguments = tuple(args)
        fields = []
        for field_span in layout.field_number_order_fields():
            fld = schema_ir.fields[field_span.field_num]
            fields.append(
                model.SchemaField(
                    offset=field_span.offset,
                    num=fld.num,
                    name=fld.cur_name,
                    type_id=self.handle_type(fld.type_info),
                    init_value=_init_value_from_schema_field(schema_ir, fld),
                )
            )
        result.fields = tuple(fields)
        result.get_hash(self.types)
        return type_id

    def _handle_enum(self, enum_ir: clkenum.ResolvedEnum, key: str) -> int:
        if enum_ir.uuid is None:
            msg = f"Cannot generate Tachyon metadata for enum without UUID: {enum_ir.value_key()}"
            raise ValueError(msg)
        values = []
        for fld_num, value in sorted(enum_ir.values.items()):
            values.append(model.EnumValue(num=fld_num, value=value.integer_value, name=value.name))
        result = model.ClkEnumType(
            fqn=enum_ir.value_key(),
            options=model.ClkEnumType.Options.flags if enum_ir.bit_flags else model.ClkEnumType.Options(0),
            underlying_type_id=self.handle_type(enum_ir.underlying_type),
            enum_uuid=enum_ir.uuid,
            version=enum_ir.cur_version(),
            values=tuple(values),
            removed=enum_ir.history.removed,
            became=enum_ir.history.legacy_became,
        )
        type_id = len(self.types)
        self.types.append(result)
        self.value_key_to_id[key] = type_id
        result.get_hash(self.types)
        return type_id

    def _handle_strong_type(self, strong_type: strongtypes.StrongType, key: str) -> int:
        underlying_type = strong_type.typespec
        assert isinstance(underlying_type, clkbuiltins.PrimitiveType)
        result = model.StrongType(fqn=strong_type.value_key(), underlying_type_id=self.handle_type(underlying_type))
        type_id = len(self.types)
        self.types.append(result)
        self.value_key_to_id[key] = type_id
        result.get_hash(self.types)
        return type_id

    def _handle_tag(self, tag: strongtypes.Tag, key: str) -> int:
        result = model.TagType(
            fqn=tag.value_key(),
        )
        type_id = len(self.types)
        self.types.append(result)
        self.value_key_to_id[key] = type_id
        result.get_hash(self.types)
        return type_id

    def _handle_soa(self, typ: typesys.Instantiation, key: str) -> int:
        """Handle SoA types (FixedSoa and VarSoa)."""
        inner = _get_inner_typedef(typ)
        assert inner.scope is clkbuiltins.BUILTINS_SCOPE
        assert isinstance(inner, clkbuiltins.SerializableBuiltin)

        element_type = typ.arguments["type"]
        if not isinstance(element_type, schema.InstantiatedSchema):
            msg = f"SoA element type must be a schema, got {element_type.value_key()}"
            raise TypeError(msg)

        is_var = typ.instantiates is clkbuiltins.VAR_SOA
        size_arg = typ.arguments["max_size" if is_var else "size"]
        if not isinstance(size_arg, primitive.DecimalValue):
            msg = f"Bad value type for size parameter: {size_arg}"
            raise TypeError(msg)
        container_size = primitive.unsigned_decimal_to_int(size_arg)

        constraint = tachyon_reg.constraint_for_type(self.compiler_context, typ)
        if constraint is None:
            msg = f"No Tachyon representation for {key}"
            raise ValueError(msg)

        layout_info = tachyon_reg.compute_soa_layout(
            self.compiler_context, element_type, container_size, include_size_field=is_var
        )

        # Make sure the non-SoA version of the schema is recorded first.
        schema_type_id = self.handle_type(element_type)

        # And then we separately record the SoA layout.
        field_layouts = []
        size_field_offset = None
        size_field_type_id = None

        for field_layout in layout_info.field_layouts:
            if field_layout.field_num == tachyon_reg.SIZE_FIELD_NUM:
                size_field_offset = field_layout.offset
                size_type_val, _ = tachyon_reg.get_compact_size_type_info(container_size)
                size_field_type_id = self.handle_type(size_type_val)
                continue

            schema_field = element_type.fields[field_layout.field_num]
            field_type_id = self.handle_type(schema_field.type_info)

            field_layouts.append(
                model.SchemaField(
                    offset=field_layout.offset,
                    num=field_layout.field_num,
                    name=schema_field.cur_name,
                    type_id=field_type_id,
                    init_value=None,  # Init metadata already stored in schema metadata
                )
            )

        result = model.SoaType(
            fqn=inner.fqn,
            uuid=inner.uuid,
            size=constraint.size,
            alignment=constraint.alignment,
            schema_type_id=schema_type_id,
            container_size=container_size,
            field_layouts=tuple(field_layouts),
            size_field_offset=size_field_offset,
            size_field_type_id=size_field_type_id,
        )

        type_id = len(self.types)
        self.types.append(result)
        self.value_key_to_id[key] = type_id
        result.get_hash(self.types)
        return type_id


def _get_inner_typedef(typ: typesys.TypeVal) -> typesys.TypeDef:
    result = typ.instantiates if isinstance(typ, typesys.Instantiation) else typ
    if not isinstance(result, typesys.TypeDef):
        msg = f"Attempt to compute metadata for non-typedef type {result}"
        raise TypeError(msg)
    return result


def _builtin_to_protobuf(
    metadata: model.TachyonMetadata, type_desc: model.BuiltInType, pb_desc: model_pb2.TypeDesc
) -> None:
    """Convert a BuiltInType to Protobuf."""
    pb_desc.built_in.fqn = type_desc.fqn
    pb_desc.built_in.uuid = type_desc.uuid.bytes
    pb_desc.built_in.size = type_desc.size
    pb_desc.built_in.alignment = type_desc.alignment
    pb_desc.built_in.hash = type_desc.get_hash(metadata.types)
    for arg in type_desc.arguments:
        pb_arg = pb_desc.built_in.arguments.add()
        if isinstance(arg, int):
            pb_arg.type_id = arg
        else:
            pb_arg.value = arg


def _schema_to_protobuf(
    metadata: model.TachyonMetadata, type_desc: model.SchemaType, pb_desc: model_pb2.TypeDesc
) -> None:
    """Convert a SchemaType to Protobuf."""
    pb_desc.schema.fqn = type_desc.fqn
    pb_desc.schema.size = type_desc.size
    pb_desc.schema.alignment = type_desc.alignment
    pb_desc.schema.schema_uuid = type_desc.schema_uuid.bytes
    pb_desc.schema.version = type_desc.version
    pb_desc.schema.hash = type_desc.get_hash(metadata.types)
    for fld in type_desc.fields:
        pb_fld = pb_desc.schema.fields.add()
        pb_fld.offset = fld.offset
        pb_fld.num = fld.num
        pb_fld.name = fld.name
        pb_fld.type_id = fld.type_id
        _init_value_to_protobuf(fld.init_value, pb_fld)
    for removed in type_desc.removed:
        pb_desc.schema.history.removed.append(removed)
    for old_num, new_num in type_desc.became.items():
        pb_desc.schema.history.became[old_num] = new_num
    for arg in type_desc.arguments:
        pb_arg = pb_desc.schema.arguments.add()
        if isinstance(arg, int):
            pb_arg.type_id = arg
        else:
            pb_arg.value = arg


def _enum_to_protobuf(
    metadata: model.TachyonMetadata, type_desc: model.ClkEnumType, pb_desc: model_pb2.TypeDesc
) -> None:
    """Convert a ClkEnumType to Protobuf."""
    pb_desc.clk_enum.fqn = type_desc.fqn
    pb_desc.clk_enum.options = int(type_desc.options)
    pb_desc.clk_enum.underlying_type_id = type_desc.underlying_type_id
    pb_desc.clk_enum.enum_uuid = type_desc.enum_uuid.bytes
    pb_desc.clk_enum.version = type_desc.version
    pb_desc.clk_enum.hash = type_desc.get_hash(metadata.types)
    for val in type_desc.values:
        pb_val = pb_desc.clk_enum.values.add()
        pb_val.num = val.num
        pb_val.value = val.value
        pb_val.name = val.name
    for removed in type_desc.removed:
        pb_desc.clk_enum.history.removed.append(removed)
    for old_num, new_num in type_desc.became.items():
        pb_desc.clk_enum.history.became[old_num] = new_num


def _soa_to_protobuf(metadata: model.TachyonMetadata, type_desc: model.SoaType, pb_desc: model_pb2.TypeDesc) -> None:
    """Convert a SoaType to Protobuf."""
    pb_desc.soa_type.fqn = type_desc.fqn
    pb_desc.soa_type.uuid = type_desc.uuid.bytes
    pb_desc.soa_type.size = type_desc.size
    pb_desc.soa_type.alignment = type_desc.alignment
    pb_desc.soa_type.schema_type_id = type_desc.schema_type_id
    pb_desc.soa_type.container_size = type_desc.container_size
    pb_desc.soa_type.hash = type_desc.get_hash(metadata.types)
    for fld in type_desc.field_layouts:
        pb_fld = pb_desc.soa_type.field_layouts.add()
        pb_fld.offset = fld.offset
        pb_fld.num = fld.num
        pb_fld.name = fld.name
        pb_fld.type_id = fld.type_id
        # Note: init_value is always None for SoA fields, so we don't serialize it
    if type_desc.size_field_offset is not None:
        pb_desc.soa_type.size_field_offset = type_desc.size_field_offset
    if type_desc.size_field_type_id is not None:
        pb_desc.soa_type.size_field_type_id = type_desc.size_field_type_id


def to_protobuf(metadata: model.TachyonMetadata) -> model_pb2.TachyonMetadata:
    """Convert a TachyonMetadata to Protobuf."""
    result = model_pb2.TachyonMetadata(outer_type_id=metadata.outer_type_id, version=metadata.version)
    result.python_required = metadata.python_required
    for type_desc in metadata.types:
        pb_desc = result.types.add()
        if isinstance(type_desc, model.BuiltInType):
            _builtin_to_protobuf(metadata, type_desc, pb_desc)
        elif isinstance(type_desc, model.SchemaType):
            _schema_to_protobuf(metadata, type_desc, pb_desc)
        elif isinstance(type_desc, model.ClkEnumType):
            _enum_to_protobuf(metadata, type_desc, pb_desc)
        elif isinstance(type_desc, model.TagType):
            pb_desc.tag.fqn = type_desc.fqn
            pb_desc.tag.hash = type_desc.get_hash(metadata.types)
        elif isinstance(type_desc, model.StrongType):
            pb_desc.strong_type.fqn = type_desc.fqn
            pb_desc.strong_type.underlying_type_id = type_desc.underlying_type_id
            pb_desc.strong_type.hash = type_desc.get_hash(metadata.types)
        elif isinstance(type_desc, model.SoaType):  # pyright: ignore[reportUnnecessaryIsInstance] # For error reporting
            _soa_to_protobuf(metadata, type_desc, pb_desc)
        else:
            msg = f"Unrecognized metadata type {type(type_desc)}"
            raise NotImplementedError(msg)
    return result


def _from_pb_arg(pb_arg: model_pb2.Argument) -> int | str:
    if pb_arg.HasField("type_id"):
        return pb_arg.type_id
    return pb_arg.value


def from_protobuf(pb_metadata: model_pb2.TachyonMetadata) -> model.TachyonMetadata:
    """Create a TachyonMetadata from Protobuf."""
    types: list[model.ClkType] = []
    for pb_desc in pb_metadata.types:
        if pb_desc.HasField("built_in"):
            builtin_uuid = (
                UUID(bytes=pb_desc.built_in.uuid)
                if pb_metadata.version >= model.BUILT_IN_UUID_ADDED_IN_VERSION
                else UUID(int=0)
            )
            types.append(
                model.BuiltInType(
                    fqn=pb_desc.built_in.fqn,
                    uuid=builtin_uuid,
                    size=pb_desc.built_in.size,
                    alignment=pb_desc.built_in.alignment,
                    hash=pb_desc.built_in.hash,
                    arguments=tuple(_from_pb_arg(pb_arg) for pb_arg in pb_desc.built_in.arguments),
                )
            )
        elif pb_desc.HasField("schema"):
            types.append(
                model.SchemaType(
                    fqn=pb_desc.schema.fqn,
                    size=pb_desc.schema.size,
                    alignment=pb_desc.schema.alignment,
                    schema_uuid=UUID(bytes=pb_desc.schema.schema_uuid),
                    version=pb_desc.schema.version,
                    hash=pb_desc.schema.hash,
                    arguments=tuple(_from_pb_arg(pb_arg) for pb_arg in pb_desc.schema.arguments),
                    fields=tuple(
                        model.SchemaField(
                            offset=pb_fld.offset,
                            num=pb_fld.num,
                            name=pb_fld.name,
                            type_id=pb_fld.type_id,
                            init_value=_init_value_from_pb(pb_fld),
                        )
                        for pb_fld in pb_desc.schema.fields
                    ),
                    became=dict(pb_desc.schema.history.became.items()),
                    removed=set(pb_desc.schema.history.removed),
                )
            )
        elif pb_desc.HasField("clk_enum"):
            types.append(
                model.ClkEnumType(
                    fqn=pb_desc.clk_enum.fqn,
                    options=model.ClkEnumType.Options(pb_desc.clk_enum.options),
                    underlying_type_id=pb_desc.clk_enum.underlying_type_id,
                    enum_uuid=UUID(bytes=pb_desc.clk_enum.enum_uuid),
                    version=pb_desc.clk_enum.version,
                    hash=pb_desc.clk_enum.hash,
                    values=tuple(
                        model.EnumValue(num=pb_val.num, value=pb_val.value, name=pb_val.name)
                        for pb_val in pb_desc.clk_enum.values
                    ),
                    became=dict(pb_desc.clk_enum.history.became.items()),
                    removed=set(pb_desc.clk_enum.history.removed),
                )
            )
        elif pb_desc.HasField("tag"):
            types.append(model.TagType(fqn=pb_desc.tag.fqn, hash=pb_desc.tag.hash))
        elif pb_desc.HasField("strong_type"):
            types.append(
                model.StrongType(
                    fqn=pb_desc.strong_type.fqn,
                    underlying_type_id=pb_desc.strong_type.underlying_type_id,
                    hash=pb_desc.strong_type.hash,
                )
            )
        elif pb_desc.HasField("soa_type"):
            types.append(
                model.SoaType(
                    fqn=pb_desc.soa_type.fqn,
                    uuid=UUID(bytes=pb_desc.soa_type.uuid),
                    size=pb_desc.soa_type.size,
                    alignment=pb_desc.soa_type.alignment,
                    schema_type_id=pb_desc.soa_type.schema_type_id,
                    container_size=pb_desc.soa_type.container_size,
                    hash=pb_desc.soa_type.hash,
                    field_layouts=tuple(
                        model.SchemaField(
                            offset=pb_fld.offset,
                            num=pb_fld.num,
                            name=pb_fld.name,
                            type_id=pb_fld.type_id,
                            init_value=None,  # SoA fields don't have init values
                        )
                        for pb_fld in pb_desc.soa_type.field_layouts
                    ),
                    size_field_offset=pb_desc.soa_type.size_field_offset
                    if pb_desc.soa_type.HasField("size_field_offset")
                    else None,
                    size_field_type_id=pb_desc.soa_type.size_field_type_id
                    if pb_desc.soa_type.HasField("size_field_type_id")
                    else None,
                )
            )
        else:
            msg = f"Unrecognized metadata type {pb_desc.WhichOneof('type_desc')}"
            raise NotImplementedError(msg)
    return model.TachyonMetadata(
        outer_type_id=pb_metadata.outer_type_id,
        types=types,
        version=pb_metadata.version,
        python_required=pb_metadata.python_required,
    )


def get_metadata(compiler_context: CompilerContext, schema_ir: schema.InstantiatedSchema) -> model.TachyonMetadata:
    """Generate Tachyon metadata for a schema."""
    builder = Builder(compiler_context)
    outer_type_id = builder.handle_type(schema_ir)
    return builder.get_metadata(outer_type_id)


def get_serialized_metadata(compiler_context: CompilerContext, schema_ir: schema.InstantiatedSchema) -> bytes:
    """Generate and serialize Tachyon metadata to Protobuf bytes."""
    metadata = get_metadata(compiler_context, schema_ir)
    metadata_pb = to_protobuf(metadata)
    # Despite the name SerializeToString, this actually returns bytes.
    return metadata_pb.SerializeToString()


def get_metadata_from_protobuf(pb_data: bytes) -> model.TachyonMetadata:
    """Deserialize Protobuf metadata and convert to our model class."""
    metadata_pb = model_pb2.TachyonMetadata()
    metadata_pb.ParseFromString(pb_data)
    return from_protobuf(metadata_pb)
