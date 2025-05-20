# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Metadata generation and utilities for Tachyon representations."""

from __future__ import annotations

from typing import TYPE_CHECKING
from uuid import UUID

from clockwork.dsl.ir import clkbuiltins, clkenum, node, schema, strongtypes, typesys
from clockwork.dsl.serialization import tachyon_layout_reg, tachyon_reg
from clockwork.serialization.metadata import tachyon_model as model
from clockwork.serialization.metadata import tachyon_model_pb2 as model_pb2

if TYPE_CHECKING:
    from clockwork.dsl.compiler_context import CompilerContext


class Builder:
    """Tachyon Metadata Builder."""

    def __init__(self, compiler_context: CompilerContext) -> None:  # pyright: ignore[reportMissingSuperCall] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        """Create a Builder."""
        self.compiler_context = compiler_context
        self.types: list[model.ClkType] = []
        self.value_key_to_id: dict[str, int] = {}

    def get_metadata(self, outer_type_id: int) -> model.TachyonMetadata:
        """Create a TachyonMetadata."""
        return model.TachyonMetadata(outer_type_id=outer_type_id, types=self.types[:])

    def handle_type(self, typ: typesys.TypeVal) -> int:
        """Register a type if needed and return the type ID."""
        key = typ.value_key()
        try:
            return self.value_key_to_id[key]
        except KeyError:
            pass
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
        return self._handle_builtin(typ, key)

    def _handle_builtin(self, typ: typesys.TypeVal, key: str) -> int:
        inner = _get_inner_typedef(typ)
        assert inner.scope is clkbuiltins.BUILTINS_SCOPE  # noqa: S101  (sanity check; enforced in handle_type)
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
            assert params is not None  # noqa: S101  (invariant: Instantiated types have parameters)
            for param in params:
                arg = typ.arguments[param.name]
                arg_val: int | str
                if param.type_bound is clkbuiltins.TYPE_TYPE or isinstance(arg, typesys.TypeVal):
                    assert param.type_bound is clkbuiltins.TYPE_TYPE  # noqa: S101  (sanity check)
                    assert isinstance(arg, typesys.TypeVal)  # noqa: S101  (sanity check)
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
            fields=(),
        )
        type_id = len(self.types)
        self.types.append(result)
        self.value_key_to_id[key] = type_id
        fields = []
        for field_span in layout.field_number_order_fields():
            fld = schema_ir.fields[field_span.field_num]
            fields.append(
                model.SchemaField(
                    offset=field_span.offset, num=fld.num, name=fld.cur_name, type_id=self.handle_type(fld.type_info)
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
        )
        type_id = len(self.types)
        self.types.append(result)
        self.value_key_to_id[key] = type_id
        result.get_hash(self.types)
        return type_id

    def _handle_strong_type(self, strong_type: strongtypes.StrongType, key: str) -> int:
        underlying_type = strong_type.typespec
        assert isinstance(underlying_type, clkbuiltins.PrimitiveType)  # noqa: S101  (invariant)
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


def _get_inner_typedef(typ: typesys.TypeVal) -> typesys.TypeDef:
    result = typ.instantiates if isinstance(typ, typesys.Instantiation) else typ
    if not isinstance(result, typesys.TypeDef):
        msg = f"Attempt to compute metadata for non-typedef type {result}"
        raise TypeError(msg)
    return result


def to_protobuf(metadata: model.TachyonMetadata) -> model_pb2.TachyonMetadata:  # noqa: C901
    """Convert a TachyonMetadata to Protobuf."""
    result = model_pb2.TachyonMetadata(outer_type_id=metadata.outer_type_id, version=metadata.version)
    for type_desc in metadata.types:
        pb_desc = result.types.add()
        if isinstance(type_desc, model.BuiltInType):
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
        elif isinstance(type_desc, model.SchemaType):
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
        elif isinstance(type_desc, model.ClkEnumType):
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
        elif isinstance(type_desc, model.TagType):
            pb_desc.tag.fqn = type_desc.fqn
            pb_desc.tag.hash = type_desc.get_hash(metadata.types)
        elif isinstance(type_desc, model.StrongType):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            pb_desc.strong_type.fqn = type_desc.fqn
            pb_desc.strong_type.underlying_type_id = type_desc.underlying_type_id
            pb_desc.strong_type.hash = type_desc.get_hash(metadata.types)
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
                    fields=tuple(
                        model.SchemaField(
                            offset=pb_fld.offset, num=pb_fld.num, name=pb_fld.name, type_id=pb_fld.type_id
                        )
                        for pb_fld in pb_desc.schema.fields
                    ),
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
        else:
            msg = f"Unrecognized metadata type {pb_desc.WhichOneof('type_desc')}"
            raise NotImplementedError(msg)
    return model.TachyonMetadata(outer_type_id=pb_metadata.outer_type_id, types=types, version=pb_metadata.version)


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
