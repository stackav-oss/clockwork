# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Helper functions to produce python bindings from schemas."""

from __future__ import annotations

from collections.abc import Callable
from typing import TYPE_CHECKING, Final, TypeAlias, cast

from clockwork.dsl.bazel import targets
from clockwork.dsl.ir import clkbuiltins, schema, typesys
from clockwork.dsl.python import typereg as py_typereg
from clockwork.dsl.python.py_context import PythonChunks
from clockwork.serialization.metadata import tachyon
from clockwork.serialization.metadata import tachyon_model as model

if TYPE_CHECKING:
    from collections.abc import Sequence

    from clockwork.dsl.compiler_context import CompilerContext
    from clockwork.dsl.ir.interface import InterfaceInstantiation

MUTABLE_BYTES_MODULE: Final = "jewels.memory.py_bytes"
MUTABLE_BYTES: Final = f"{MUTABLE_BYTES_MODULE}.MutableBytes"

VAR_ARRAY_MODULE: Final = "jewels.container.tap.py_var_array"
VAR_ARRAY: Final = "jewels.container.tap.py_var_array.VarArray"
VAR_ARRAY_SERDES: Final = "jewels.container.tap.py_var_array.VarArraySerDes"

SER_DES_MODULE: Final = "jewels.container.tap.value_serdes"

PRIMITIVE_TYPE_HINT_MAP: Final = {
    ".Bool": "bool",
    ".Float32": "float",
    ".Float64": "float",
    ".Int8": "int",
    ".Int16": "int",
    ".Int32": "int",
    ".Int64": "int",
    ".UInt8": "int",
    ".UInt16": "int",
    ".UInt32": "int",
    ".UInt64": "int",
}
"""Mapping from primitive type to type hint."""


STRUCT_FORMAT_MAP: Final = {
    ".Bool": "?",
    ".Float32": "f",
    ".Float64": "d",
    ".Int8": "b",
    ".Int16": "h",
    ".Int32": "l",
    ".Int64": "q",
    ".UInt8": "B",
    ".UInt16": "H",
    ".UInt32": "L",
    ".UInt64": "Q",
}
"""Mapping from primitive type to strut format specifier."""


ENDIAN: Final = "<"
"""All values are in little endian order."""

IdentifierLookup: TypeAlias = Callable[[str], py_typereg.PySymbol]


def get_type_hint(field_type: model.ClkType) -> str:
    """Get type hint for clockwork type."""
    if isinstance(field_type, model.BuiltInType):
        return PRIMITIVE_TYPE_HINT_MAP[field_type.fqn]

    msg = f"Unable to provide type hint for type: {field_type.fqn}"
    raise NotImplementedError(msg)


def get_struct_format_char(field_type: model.BuiltInType) -> str:
    """Convert a primitive type to a format specifier from the struct module."""
    return STRUCT_FORMAT_MAP[field_type.fqn]


def remove_outer_newlines(string: str) -> str:
    """Remove leading and trailing newlines."""
    return string.strip("\n")


def render_init(chunks: PythonChunks) -> None:
    """Render a schema's __init__ method."""
    chunks.imports.add(f"import {MUTABLE_BYTES_MODULE}")
    chunks.impl.append(
        remove_outer_newlines(
            f"""
    def __init__(self, buffer: {MUTABLE_BYTES}) -> None:
        self._validate_size(buffer)
        self._buffer = buffer
"""
        )
    )


def render_size_validation(num_bytes: int, chunks: PythonChunks) -> None:
    """Render a method that validates the size of a buffer."""
    chunks.imports.add(f"import {MUTABLE_BYTES_MODULE}")
    chunks.impl.append(
        remove_outer_newlines(
            f"""
    def _validate_size(self, buffer: {MUTABLE_BYTES}) -> None:
        if len(buffer) != {num_bytes}:
            msg = f"Expected buffer of size {num_bytes} but received {{len(buffer)}}."
            raise ValueError(msg)
"""
        )
    )


def render_primitive_field_property(
    field: model.SchemaField, field_type: model.BuiltInType, chunks: PythonChunks
) -> None:
    """Render a field with a primitive type."""
    field_type_hint = get_type_hint(field_type)
    struct_format_char = get_struct_format_char(field_type)
    chunks.system_imports.add("import struct")
    chunks.impl.append(
        remove_outer_newlines(
            f"""
    @property
    def {field.name}(self) -> {field_type_hint}:
        value, = struct.unpack_from("{ENDIAN}{struct_format_char}", self._buffer, {field.offset})
        return value
    @{field.name}.setter
    def {field.name}(self, value: {field_type_hint}) -> None:
        struct.pack_into("{ENDIAN}{struct_format_char}", self._buffer, {field.offset}, value)
"""
        )
    )


def render_serdes(field_type: model.ClkType, types: Sequence[model.ClkType]) -> str:
    """Render the serdes hint for a field."""
    if is_primitive_field(field_type):
        return f"{SER_DES_MODULE}.{field_type.fqn[1:]}SerDes"
    if isinstance(field_type, model.BuiltInType) and field_type.fqn == ".VarArray":
        value_type, capacity = field_type.arguments
        value_type_serdes = render_serdes(types[cast("int", value_type)], types)
        return f"{VAR_ARRAY_SERDES}({capacity}, {value_type_serdes})"
    msg = f"Unknown serdes type: {field_type.fqn}"
    raise NotImplementedError(msg)


def render_type_hint(field_type: model.ClkType, types: Sequence[model.ClkType]) -> str:
    """Render the type hint for a field."""
    if is_primitive_field(field_type):
        return PRIMITIVE_TYPE_HINT_MAP[field_type.fqn]
    if isinstance(field_type, model.BuiltInType) and field_type.fqn == ".VarArray":
        value_type, _ = field_type.arguments
        value_type_hint = render_type_hint(types[cast("int", value_type)], types)
        return f"{VAR_ARRAY}[{value_type_hint}]"
    msg = f"Unknown serdes type: {field_type.fqn}"
    raise NotImplementedError(msg)


def render_vararray_field_property(
    field: model.SchemaField, field_type: model.BuiltInType, types: Sequence[model.ClkType], chunks: PythonChunks
) -> None:
    """Render property methods for a var array field."""
    type_hint = render_type_hint(field_type, types)
    serdes = render_serdes(field_type, types)

    byte_start = field.offset
    byte_end = byte_start + field_type.size

    chunks.system_imports.add(f"import {VAR_ARRAY_MODULE}")
    chunks.system_imports.add(f"import {SER_DES_MODULE}")
    chunks.impl.append(
        remove_outer_newlines(
            f"""
    @property
    def {field.name}(self) -> {type_hint}:
        return {serdes}.deserialize(memoryview(self._buffer)[{byte_start}:{byte_end}])
    @{field.name}.setter
    def {field.name}(self, value: {type_hint}) -> None:
        self._buffer[{byte_start}:{byte_end}] = value._buf
"""
        )
    )


def render_bitset_field_property(field: model.SchemaField, field_type: model.BuiltInType, chunks: PythonChunks) -> None:
    """Render property methods for a fixed-size bitset field."""
    if len(field_type.arguments) != 1 or not isinstance(field_type.arguments[0], str):
        msg = f"Invalid Bitset metadata arguments: {field_type.arguments}"
        raise ValueError(msg)
    try:
        bit_size = int(field_type.arguments[0])
    except ValueError as exc:
        msg = f"Invalid Bitset metadata arguments: {field_type.arguments}"
        raise ValueError(msg) from exc
    byte_size = bit_size // 8 + (bit_size % 8 != 0)
    if bit_size <= 0 or field_type.size != byte_size or field_type.alignment != 1:
        msg = f"Invalid Bitset metadata for size {bit_size}"
        raise ValueError(msg)
    maximum_value = (1 << bit_size) - 1
    byte_start = field.offset
    byte_end = byte_start + field_type.size
    chunks.impl.append(
        remove_outer_newlines(
            f"""
    @property
    def {field.name}(self) -> int:
        return int.from_bytes(memoryview(self._buffer)[{byte_start}:{byte_end}], byteorder="little") & {maximum_value}
    @{field.name}.setter
    def {field.name}(self, value: int) -> None:
        if value < 0 or value > {maximum_value}:
            msg = "Bitset<{bit_size}> value must be in [0, {maximum_value}]"
            raise ValueError(msg)
        self._buffer[{byte_start}:{byte_end}] = value.to_bytes({field_type.size}, byteorder="little", signed=False)
"""
        )
    )


def render_builtin_field_property(
    field: model.SchemaField, field_type: model.BuiltInType, types: Sequence[model.ClkType], chunks: PythonChunks
) -> None:
    """Render property methods for a builtin type."""
    if is_primitive_field(field_type):
        return render_primitive_field_property(field, field_type, chunks)
    if field_type.fqn == ".Bitset":
        return render_bitset_field_property(field, field_type, chunks)
    if field_type.fqn == ".VarArray":
        return render_vararray_field_property(field, field_type, types, chunks)

    msg = f"Python bindings are unable to render builtin field of type: {field_type.fqn}"
    raise NotImplementedError(msg)


def render_schema_field_property(
    field: model.SchemaField, field_type: model.SchemaType, identifier_lookup: IdentifierLookup, chunks: PythonChunks
) -> None:
    """Render a field with a schema type."""
    identifier = identifier_lookup(field_type.fqn)
    if identifier.import_spec:
        chunks.imports.add(f"import {identifier.import_spec}")
        field_py_type = f"{identifier.import_spec}.{identifier.class_name}"
    else:
        field_py_type = identifier.class_name

    start = field.offset
    end = start + field_type.size
    chunks.impl.append(
        remove_outer_newlines(
            f"""
    @property
    def {field.name}(self) -> {field_py_type}:
        return {field_py_type}(memoryview(self._buffer)[{start}:{end}])
    @{field.name}.setter
    def {field.name}(self, value: {field_py_type}) -> None:
        memoryview(self._buffer)[{start}:{end}] = value._buffer
"""
        )
    )


def render_schema_fields(
    clk_type: model.SchemaType,
    types: Sequence[model.ClkType],
    identifier_lookup: IdentifierLookup,
    chunks: PythonChunks,
) -> None:
    """Render all fields in a schema."""
    for field in clk_type.fields:
        field_type = types[field.type_id]
        if isinstance(field_type, model.BuiltInType):
            render_builtin_field_property(field, field_type, types, chunks)
        elif isinstance(field_type, model.SchemaType):
            render_schema_field_property(field, field_type, identifier_lookup, chunks)
        else:
            msg = f"Unsupported field of type {type(field_type)}"
            raise NotImplementedError(msg)


def render_basic_schema(clk_type: model.SchemaType) -> PythonChunks:
    """Render all common schema methods and properties."""
    schema_name = clk_type.fqn.rsplit("::", 1)[-1]
    chunks = PythonChunks()
    chunks.impl.append(
        remove_outer_newlines(
            f"""
class {schema_name}:
"""
        )
    )
    render_init(chunks)
    render_size_validation(clk_type.size, chunks)
    return chunks


def schema_bindings_from_model(metadata: model.TachyonMetadata, identifier_lookup: IdentifierLookup) -> PythonChunks:
    """Produce schema bindings from a tachyon model object."""
    clk_type = metadata.types[metadata.outer_type_id]
    assert isinstance(clk_type, model.SchemaType)

    chunks = render_basic_schema(clk_type)
    render_schema_fields(clk_type, metadata.types, identifier_lookup, chunks)
    return chunks


def metadata_for_interfaces(
    compiler_context: CompilerContext, interfaces: list[InterfaceInstantiation]
) -> tuple[list[int], tachyon.Builder]:
    """Produce metadata for all interfaces."""
    builder = tachyon.Builder(compiler_context)
    metadata_indexes = [builder.handle_type(get_schema_from_interface(interface)) for interface in interfaces]
    return metadata_indexes, builder


def get_schema_from_interface(interface: InterfaceInstantiation) -> schema.InstantiatedSchema:
    """Get the schema IR from an interface while validating preconditions.

    The preconditions should already be checked in compiler.py.
    """
    assert isinstance(interface.typespec, typesys.Instantiation)
    # This is already checked in compiler.py.
    assert interface.typespec.instantiates == clkbuiltins.TAP

    representation = interface.representation
    # This is already checked in compiler.py.
    assert representation is not None
    assert isinstance(representation.typespec, typesys.Instantiation)
    assert representation.typespec.instantiates == clkbuiltins.TACHYON

    return representation.schema_ir


def is_primitive_field(field_type: model.ClkType) -> bool:
    """Check if a field is a primitive type."""
    return field_type.fqn in PRIMITIVE_TYPE_HINT_MAP


def bazel_deps_for_interfaces(
    compiler_context: CompilerContext, interfaces: list[InterfaceInstantiation], repo: str, import_spec: str
) -> set[targets.Label]:
    """Produce metadata for all interfaces."""
    metadata_indexes, builder = metadata_for_interfaces(compiler_context, interfaces)
    deps = set()
    for index in metadata_indexes:
        metadata = builder.get_metadata(index)
        clk_type = metadata.types[index]
        assert isinstance(clk_type, model.SchemaType)
        for field in clk_type.fields:
            field_type = metadata.types[field.type_id]
            if is_primitive_field(field_type):
                continue
            if field_type.fqn == ".Bitset":
                continue
            if field_type.fqn == ".VarArray":
                continue
            py_type = py_typereg.get_py_type(compiler_context, field_type.fqn)
            if py_type.repo == repo and py_type.import_spec == import_spec:
                # Same module so no need to add a label.
                continue
            assert py_type.import_spec is not None
            import_spec_parts = py_type.import_spec.split(".")
            repo_str = f"@{py_type.repo}" if py_type.repo != repo else ""
            package_str = "/".join(import_spec_parts[:-1])
            label_str = f"{repo_str}//{package_str}:{import_spec_parts[-1]}"
            deps.add(targets.Label(label_str))
    return deps


def bindings_for_interfaces(
    compiler_context: CompilerContext, interfaces: list[InterfaceInstantiation], identifier_lookup: IdentifierLookup
) -> PythonChunks:
    """Produce bindings schema interfaces."""
    metadata_indexes, builder = metadata_for_interfaces(compiler_context, interfaces)

    python_chunks = PythonChunks()
    for index in metadata_indexes:
        metadata = builder.get_metadata(index)
        python_chunks.append(schema_bindings_from_model(metadata, identifier_lookup))

    return python_chunks
