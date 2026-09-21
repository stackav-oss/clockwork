# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Tachyon Python Serializer Registry."""

from __future__ import annotations

import contextlib
import dataclasses
import enum
import math
import operator
import re
import struct
import uuid
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from functools import reduce
from typing import TYPE_CHECKING, Any, Final, Generic, TypeAlias, TypeVar, cast

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.ir import clkbuiltins, clkenum, tensor_builtins, typesys
from clockwork.dsl.serialization import tachyon_reg
from clockwork.serialization.metadata import tachyon as tachyon_metadata
from clockwork.serialization.metadata import tachyon_model
from typing_extensions import override

if TYPE_CHECKING:
    from clockwork.serialization.py import protocol

T = TypeVar("T")

Serializer: TypeAlias = Callable[[T, memoryview], None]
Deserializer: TypeAlias = Callable[[memoryview], T]


@dataclass
class SerDes(Generic[T]):
    """Holds a serializer and deserializer for type T."""

    type_: type[T]
    fqn: str
    constraint: tachyon_reg.FieldConstraint
    serializer: Serializer[T]
    deserializer: Deserializer[T]


# Module-level SerDes instances for built-in types
BOOL_SERDES: Final = SerDes(
    type_=bool,
    fqn=clkbuiltins.BOOL.fqn,
    constraint=tachyon_reg.BOOL_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<?", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<?", buffer, 0)[0],
)

BYTE_SERDES: Final = SerDes(
    type_=int,
    fqn=clkbuiltins.BYTE.fqn,
    constraint=tachyon_reg.BYTE_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<B", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<B", buffer, 0)[0],
)

FLOAT32_SERDES: Final = SerDes(
    type_=float,
    fqn=clkbuiltins.FLOAT32.fqn,
    constraint=tachyon_reg.FLOAT32_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<f", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<f", buffer, 0)[0],
)

FLOAT64_SERDES: Final = SerDes(
    type_=float,
    fqn=clkbuiltins.FLOAT64.fqn,
    constraint=tachyon_reg.FLOAT64_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<d", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<d", buffer, 0)[0],
)

INT64_SERDES: Final = SerDes(
    type_=int,
    fqn=clkbuiltins.INT64.fqn,
    constraint=tachyon_reg.INT64_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<q", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<q", buffer, 0)[0],
)

INT32_SERDES: Final = SerDes(
    type_=int,
    fqn=clkbuiltins.INT32.fqn,
    constraint=tachyon_reg.INT32_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<i", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<i", buffer, 0)[0],
)

INT16_SERDES: Final = SerDes(
    type_=int,
    fqn=clkbuiltins.INT16.fqn,
    constraint=tachyon_reg.INT16_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<h", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<h", buffer, 0)[0],
)

INT8_SERDES: Final = SerDes(
    type_=int,
    fqn=clkbuiltins.INT8.fqn,
    constraint=tachyon_reg.INT8_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<b", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<b", buffer, 0)[0],
)

UINT64_SERDES: Final = SerDes(
    type_=int,
    fqn=clkbuiltins.UINT64.fqn,
    constraint=tachyon_reg.UINT64_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<Q", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<Q", buffer, 0)[0],
)

UINT32_SERDES: Final = SerDes(
    type_=int,
    fqn=clkbuiltins.UINT32.fqn,
    constraint=tachyon_reg.UINT32_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<I", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<I", buffer, 0)[0],
)

UINT16_SERDES: Final = SerDes(
    type_=int,
    fqn=clkbuiltins.UINT16.fqn,
    constraint=tachyon_reg.UINT16_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<H", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<H", buffer, 0)[0],
)

UINT8_SERDES: Final = SerDes(
    type_=int,
    fqn=clkbuiltins.UINT8.fqn,
    constraint=tachyon_reg.UINT8_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<B", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<B", buffer, 0)[0],
)

DURATION_SERDES: Final = SerDes(
    type_=int,
    fqn=clkbuiltins.DURATION.fqn,
    constraint=tachyon_reg.DURATION_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<q", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<q", buffer, 0)[0],
)

SYNC_TIME_SERDES: Final = SerDes(
    type_=int,
    fqn=clkbuiltins.SYNC_TIME.fqn,
    constraint=tachyon_reg.SYNC_TIME_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<q", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<q", buffer, 0)[0],
)

# Dictionary mapping Clockwork types to their SerDes instances
BUILTIN_SERDES: Final[dict[str, SerDes[Any]]] = {
    clkbuiltins.BOOL.fqn: BOOL_SERDES,
    clkbuiltins.BYTE.fqn: BYTE_SERDES,
    clkbuiltins.FLOAT32.fqn: FLOAT32_SERDES,
    clkbuiltins.FLOAT64.fqn: FLOAT64_SERDES,
    clkbuiltins.INT64.fqn: INT64_SERDES,
    clkbuiltins.INT32.fqn: INT32_SERDES,
    clkbuiltins.INT16.fqn: INT16_SERDES,
    clkbuiltins.INT8.fqn: INT8_SERDES,
    clkbuiltins.UINT64.fqn: UINT64_SERDES,
    clkbuiltins.UINT32.fqn: UINT32_SERDES,
    clkbuiltins.UINT16.fqn: UINT16_SERDES,
    clkbuiltins.UINT8.fqn: UINT8_SERDES,
    clkbuiltins.DURATION.fqn: DURATION_SERDES,
    clkbuiltins.SYNC_TIME.fqn: SYNC_TIME_SERDES,
}


class TachyonDynMetadataRegistry(Context):
    """Registry for Tachyon Python serializers from metadata."""

    def __init__(self) -> None:
        """Create a new, empty Tachyon serializer registry."""
        self.type_registry: dict[str, SerDes[Any]] = {}
        self.generic_type_registry: dict[
            str,
            Callable[
                [CompilerContext, int, Sequence[tachyon_model.ClkType], dict[int, SerDes[Any]]], SerDes[Any] | None
            ],
        ] = {}

    @override
    def import_from(self, other: TachyonDynMetadataRegistry) -> None:
        """Combine this context with items from another.

        Raises:
            RuntimeError: If a type already exists with a different SerDes.
        """
        for key, serdes in other.type_registry.items():
            if key in self.type_registry and self.type_registry[key] != serdes:
                msg = f"Type {key} has conflicting SerDes: {self.type_registry[key]} vs {serdes}"
                raise RuntimeError(msg)
            self.type_registry[key] = serdes

        for generic_key, factory in other.generic_type_registry.items():
            if generic_key in self.generic_type_registry and self.generic_type_registry[generic_key] is not factory:
                msg = f"Generic factory {generic_key} has conflicting registrations"
                raise RuntimeError(msg)
            self.generic_type_registry[generic_key] = factory


class TachyonDynMetadataRegistryKey(ContextKey[TachyonDynMetadataRegistry]):
    """Compiler context key for Tachyon Python serializer registry from metadata."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> TachyonDynMetadataRegistry:
        """Create a default instance of the registry with built-in types."""
        registry = TachyonDynMetadataRegistry()

        # Directly add built-in types to the registry instead of using register_type
        # This avoids the infinite recursion caused by register_type accessing the registry
        registry.type_registry.update(BUILTIN_SERDES)

        # Directly add generic type factories to the registry
        registry.generic_type_registry.update(
            {
                clkbuiltins.UUID.fqn: _uuid_factory,
                clkbuiltins.BITSET.fqn: _bitset_factory,
                clkbuiltins.FIXED_ARRAY.fqn: _fixed_array_factory,
                tensor_builtins.TENSOR.fqn: _tensor_factory,
                clkbuiltins.OPTIONAL.fqn: _optional_factory,
                clkbuiltins.VAR_ARRAY.fqn: _var_array_factory,
                clkbuiltins.VAR_STRING.fqn: _var_string_factory,
            }
        )

        return registry


TACHYON_DYN_METADATA_REGISTRY_KEY: Final = TachyonDynMetadataRegistryKey("TachyonDynMetadataRegistry")


# Suppressing PLR0913 (too many args) because this can't really be split. Args are kw_only to mitigate confusion.
def _maybe_serdes_for_type(  # noqa: PLR0913
    *,
    compiler_context: CompilerContext,
    type_id: int,
    metadata_name: str | None,
    types: Sequence[tachyon_model.ClkType],
    serdeses: dict[int, SerDes[Any]],
    auto_create: bool = True,
) -> SerDes[Any] | None:
    """Look up a serializer/deserializer (SerDes) for a type.

    Returns:
        The SerDes if found (or registered automatically), else None
    """
    with contextlib.suppress(KeyError):
        return serdeses[type_id]
    typ = types[type_id]

    registry = compiler_context[TACHYON_DYN_METADATA_REGISTRY_KEY]

    if isinstance(typ, tachyon_model.BuiltInType):
        if typ.arguments:
            factory = registry.generic_type_registry.get(typ.fqn)
            if factory is not None:
                serdes = factory(compiler_context, type_id, types, serdeses)
                if serdes is not None:
                    serdeses[type_id] = serdes
                return serdes
        serdes = registry.type_registry.get(typ.fqn)
        if serdes is not None:
            serdeses[type_id] = serdes
            return serdes
    if auto_create:
        return _try_create_serdes(
            compiler_context=compiler_context,
            type_id=type_id,
            metadata_name=metadata_name,
            typ=typ,
            types=types,
            serdeses=serdeses,
        )
    return None


# Suppressing PLR0913 (too many args) because this can't really be split. Args are kw_only to mitigate confusion.
def _try_create_serdes(  # noqa: PLR0913
    *,
    compiler_context: CompilerContext,
    type_id: int,
    metadata_name: str | None,
    typ: tachyon_model.ClkType,
    types: Sequence[tachyon_model.ClkType],
    serdeses: dict[int, SerDes[Any]],
) -> SerDes[Any] | None:
    """Try to create and register a SerDes for the given type."""
    if isinstance(typ, tachyon_model.ClkEnumType):
        enum_serdes = _enum_factory(compiler_context, typ, types, serdeses)
        if enum_serdes:
            serdeses[type_id] = enum_serdes
        return enum_serdes
    if isinstance(typ, tachyon_model.StrongType):
        strong_serdes = _strong_type_factory(compiler_context, typ, types, serdeses)
        if strong_serdes:
            serdeses[type_id] = strong_serdes
        return strong_serdes
    if isinstance(typ, tachyon_model.SoaType):
        soa_serdes = _soa_type_factory(compiler_context, type_id, typ, types, serdeses)
        if soa_serdes:
            serdeses[type_id] = soa_serdes
        return soa_serdes
    if isinstance(typ, tachyon_model.SchemaType):
        schema_serdes: SchemaSerDes[Any] = SchemaSerDes.make(
            compiler_context=compiler_context,
            type_id=type_id,
            metadata_name=metadata_name,
            schema=typ,
            types=types,
            serdeses=serdeses,
        )
        serdes = schema_serdes.make_serdes()
        serdeses[type_id] = serdes
        return serdes
    return None


def _serdes_for_type(
    compiler_context: CompilerContext,
    type_id: int,
    metadata_name: str | None,
    types: Sequence[tachyon_model.ClkType],
    serdeses: dict[int, SerDes[Any]],
) -> SerDes[Any]:
    """Look up the SerDes for a type.

    Returns:
        The SerDes

    Raises:
        NotImplementedError if the type is not supported.
    """
    result = _maybe_serdes_for_type(
        compiler_context=compiler_context, type_id=type_id, metadata_name=metadata_name, types=types, serdeses=serdeses
    )
    if result is None:
        msg = f"Cannot create Python serializer for {type_id}."
        raise NotImplementedError(msg)
    return result


def register_type(compiler_context: CompilerContext, typ: typesys.TypeDef, serdes: SerDes[Any]) -> None:
    """Register SerDes for a non-generic type.

    Args:
        compiler_context: Compiler context containing the registry.
        typ: The type to register; must be fully instantiated.
        serdes: The SerDes for the type.

    Raises:
        RuntimeError if the type is not fully instantiated or is already registered with different SerDes
    """
    if typ.generic_parameters():
        msg = f"Attempt to register generic type {typ}"
        raise RuntimeError(msg)

    registry = compiler_context[TACHYON_DYN_METADATA_REGISTRY_KEY]
    existing = registry.type_registry.get(typ.fqn)
    if existing:
        if serdes != existing:
            msg = (
                f"Attempt to register SerDes {serdes} "
                f"which does not match previous registration {existing} "
                f"for type {typ}"
            )
            raise RuntimeError(msg)
        return
    registry.type_registry[typ.fqn] = serdes


def register_generic_type(
    compiler_context: CompilerContext,
    typ: typesys.TypeDef,
    factory: Callable[
        [CompilerContext, int, Sequence[tachyon_model.ClkType], dict[int, SerDes[Any]]], SerDes[Any] | None
    ],
) -> None:
    """Register a SerDes factory for a generic type.

    Args:
        compiler_context: Compiler context containing the registry.
        typ: The generic type to register
        factory: A factory function that generates a SerDes for an instantiation.

    Raises:
        RuntimeError if the type is not a generic type or is already registered.
    """
    if not typ.generic_parameters():
        msg = f"Attempt to register a factory for non-generic type {typ}"
        raise RuntimeError(msg)

    registry = compiler_context[TACHYON_DYN_METADATA_REGISTRY_KEY]
    if typ.fqn in registry.generic_type_registry:
        msg = f"Attempt to register already-registered generic type {typ}"
        raise RuntimeError(msg)
    registry.generic_type_registry[typ.fqn] = factory


def _get_constraint(compiler_context: CompilerContext, clk_type: typesys.TypeVal) -> tachyon_reg.FieldConstraint:
    constraint = tachyon_reg.constraint_for_type(compiler_context, clk_type)
    if constraint is None:
        msg = f"No Tachyon registration for type; did you forget to instantiate it? {clk_type}"
        raise ValueError(msg)
    return constraint


class PrimitiveSerDes(Generic[T]):
    """SerDes for primitive types."""

    def __init__(
        self, compiler_context: CompilerContext, clk_type: typesys.TypeVal, py_type: type[T], fmt: str
    ) -> None:
        """Create a SerDes for primitive types."""
        self.compiler_context = compiler_context
        self.clk_type = clk_type
        self.py_type = py_type
        self.fmt = f"<{fmt}"
        self.struct = struct.Struct(self.fmt)

    def serialize(self, obj: T, buffer: memoryview) -> None:
        """Serializer for type T."""
        self.struct.pack_into(buffer, 0, obj)

    def deserialize(self, buffer: memoryview) -> Any:  # noqa: ANN401
        """Deserializer for type T."""
        return self.struct.unpack_from(buffer, 0)[0]

    def make_serdes(self) -> SerDes[T]:
        """Construct a SerDes for the registry."""
        return SerDes(
            type_=self.py_type,
            fqn=self.clk_type.value_key(),
            constraint=_get_constraint(self.compiler_context, self.clk_type),
            serializer=self.serialize,
            deserializer=self.deserialize,
        )


def uuid_serialize(obj: uuid.UUID, buffer: memoryview) -> None:
    """Serializer for UUIDs."""
    buffer[:] = obj.bytes


def uuid_deserialize(buffer: memoryview) -> uuid.UUID:
    """Deserializer for UUIDs."""
    return uuid.UUID(bytes=buffer.tobytes())


UUID_SERDES: Final = SerDes(
    type_=uuid.UUID,
    fqn="UUID",
    constraint=tachyon_reg.UUID_CONSTRAINT,
    serializer=uuid_serialize,
    deserializer=uuid_deserialize,
)


def _uuid_factory(
    compiler_context: CompilerContext,  # noqa: ARG001 (needed to conform to expected signature)
    type_id: int,
    types: Sequence[tachyon_model.ClkType],
    _: dict[int, SerDes[Any]],
) -> SerDes[uuid.UUID] | None:
    """Helper function for Uuid."""
    typ = types[type_id]
    if typ.fqn != clkbuiltins.UUID.fqn:
        msg = f"Expected Uuid but got {typ.fqn}"
        raise RuntimeError(msg)
    # We align UUIDs to 8 bytes so they can be treated as two int64's, e.g. for fast hash computation.
    return UUID_SERDES


def _bitset_factory(
    compiler_context: CompilerContext,  # noqa: ARG001 (needed to conform to expected signature)
    type_id: int,
    types: Sequence[tachyon_model.ClkType],
    _: dict[int, SerDes[Any]],
) -> SerDes[int] | None:
    """Create a serializer for Bitset metadata."""
    typ = types[type_id]
    if typ.fqn != clkbuiltins.BITSET.fqn:
        msg = f"Expected Bitset but got {typ.fqn}"
        raise RuntimeError(msg)
    assert isinstance(typ, tachyon_model.BuiltInType)
    if len(typ.arguments) != 1 or not isinstance(typ.arguments[0], str):
        msg = f"Invalid Bitset metadata arguments: {typ.arguments}"
        raise ValueError(msg)
    try:
        bit_size = int(typ.arguments[0])
    except ValueError as exc:
        msg = f"Invalid Bitset metadata arguments: {typ.arguments}"
        raise ValueError(msg) from exc
    byte_size = bit_size // 8 + (bit_size % 8 != 0)
    if bit_size <= 0 or typ.size != byte_size or typ.alignment != 1:
        msg = f"Invalid Bitset metadata for size {bit_size}"
        raise ValueError(msg)
    maximum_value = (1 << bit_size) - 1
    constraint = tachyon_reg.FieldConstraint(size=typ.size, alignment=typ.alignment)

    def _serialize(obj: int, buffer: memoryview) -> None:
        if obj < 0 or obj > maximum_value:
            msg = f"Bitset<{bit_size}> value must be in [0, {maximum_value}]"
            raise ValueError(msg)
        buffer[:] = obj.to_bytes(constraint.size, byteorder="little", signed=False)

    def _deserialize(buffer: memoryview) -> int:
        return int.from_bytes(buffer, byteorder="little", signed=False) & maximum_value

    return SerDes(
        type_=int,
        fqn=typ.fqn,
        constraint=constraint,
        serializer=_serialize,
        deserializer=_deserialize,
    )


class EnumSerDes:
    """SerDes for an enum type."""

    def __init__(
        self,
        compiler_context: CompilerContext,
        typ: tachyon_model.ClkEnumType,
        types: Sequence[tachyon_model.ClkType],
        serdeses: dict[int, SerDes[Any]],
    ) -> None:
        """Create a SerDes for array types."""
        self.model_type = typ
        underlying_type = types[typ.underlying_type_id]
        assert isinstance(underlying_type, tachyon_model.BuiltInType)
        self.underlying_type = underlying_type
        values = [
            (value_def.name, value_def.value) for value_def in sorted(self.model_type.values, key=lambda x: x.num)
        ]
        impl: type = enum.Flag if tachyon_model.ClkEnumType.Options.flags in typ.options else enum.Enum
        self.py_type: Any = impl(self.model_type.fqn.rpartition("::")[-1], values)
        underlying_serdes = _serdes_for_type(
            compiler_context=compiler_context,
            type_id=self.model_type.underlying_type_id,
            metadata_name=None,
            types=types,
            serdeses=serdeses,
        )
        self.underlying_serializer = underlying_serdes.serializer
        self.underlying_deserializer = underlying_serdes.deserializer

    def serialize(self, obj: enum.Enum, buffer: memoryview) -> None:
        """Serializer for enums."""
        self.underlying_serializer(obj.value, buffer)

    def deserialize(self, buffer: memoryview) -> Any:  # noqa: ANN401
        """Deserializer for enums."""
        underlying_int = self.underlying_deserializer(buffer)
        return self.py_type(underlying_int)

    def make_serdes(self) -> SerDes[enum.Enum]:
        """Construct a SerDes for the registry."""
        return SerDes(
            type_=self.py_type,
            fqn=self.model_type.fqn,
            constraint=tachyon_reg.FieldConstraint(
                size=self.underlying_type.size, alignment=self.underlying_type.alignment
            ),
            serializer=self.serialize,
            deserializer=self.deserialize,
        )


def _enum_factory(
    compiler_context: CompilerContext,
    typ: tachyon_model.ClkEnumType,
    types: Sequence[tachyon_model.ClkType],
    serdeses: dict[int, SerDes[Any]],
) -> SerDes[enum.Enum]:
    """Get a serializer for a dynamic Enum type."""
    return EnumSerDes(compiler_context, typ, types, serdeses).make_serdes()


def _strong_type_factory(
    compiler_context: CompilerContext,
    typ: tachyon_model.StrongType,
    types: Sequence[tachyon_model.ClkType],
    serdeses: dict[int, SerDes[Any]],
) -> SerDes[float]:
    """Get a serializer for a dynamic StrongType."""
    underlying = types[typ.underlying_type_id]
    if not isinstance(underlying, tachyon_model.BuiltInType):
        msg = f"Cannot create SerDes for unresolved strong type {typ.fqn}"
        raise RuntimeError(msg)  # noqa: TRY004
    return _serdes_for_type(
        compiler_context=compiler_context,
        type_id=typ.underlying_type_id,
        metadata_name=None,
        types=types,
        serdeses=serdeses,
    )


class TensorSerDes(Generic[T]):
    """SerDes for a tensor of T type."""

    # Suppressing PLR0913 (too many args) because this can't really be split. Args are kwonly to mitigate confusion.
    def __init__(  # noqa: PLR0913 (see above)
        self,
        *,
        compiler_context: CompilerContext,
        shape: list[int],
        strides: list[int],
        element_type_id: int,
        constraint: tachyon_reg.FieldConstraint,
        types: Sequence[tachyon_model.ClkType],
        serdeses: dict[int, SerDes[Any]],
    ) -> None:
        """Create a SerDes for array types."""
        self.constraint = constraint
        self.shape = shape
        self.strides = strides
        self.num_elements = reduce(operator.mul, self.shape)
        element_serdes = _serdes_for_type(
            compiler_context=compiler_context,
            type_id=element_type_id,
            metadata_name=None,
            types=types,
            serdeses=serdeses,
        )
        self.element_serializer = element_serdes.serializer
        self.element_deserializer = element_serdes.deserializer
        element_constraint = element_serdes.constraint
        self.element_stride = element_constraint.array_stride()

    def serialize(self, obj: tensor_builtins.TensorData[T], buffer: memoryview) -> None:
        """Serializer for Tensors of T."""
        if len(obj.data) != self.num_elements:
            msg = f"Attempt to serialize tensor of length {len(obj.data)}, expected {self.num_elements}"
            raise ValueError(msg)

        offset = 0
        for elem in obj.data:
            next_offset = offset + self.element_stride
            self.element_serializer(elem, buffer[offset:next_offset])
            offset = next_offset

    def deserialize(self, buffer: memoryview) -> tensor_builtins.TensorData[T]:
        """Deserializer for type Tensors of T."""
        offset = 0
        data = [cast("T", None)] * self.num_elements
        for i in range(self.num_elements):
            next_offset = offset + self.element_stride
            data[i] = self.element_deserializer(buffer[offset:next_offset])
            offset = next_offset

        return tensor_builtins.TensorData(data=data, shape=self.shape, strides=self.strides)

    def make_serdes(self) -> SerDes[tensor_builtins.TensorData[T]]:
        """Construct a SerDes for the registry."""
        return SerDes(
            type_=tensor_builtins.TensorData,
            fqn=tensor_builtins.TENSOR.fqn,
            constraint=self.constraint,
            serializer=self.serialize,
            deserializer=self.deserialize,
        )


class FixedArraySerDes(Generic[T]):
    """SerDes for a fixed-size array of T type."""

    # Suppressing PLR0913 (too many args) because this can't really be split. Args are kwonly to mitigate confusion.
    def __init__(  # noqa: PLR0913
        self,
        *,
        compiler_context: CompilerContext,
        size: int,
        element_type_id: int,
        constraint: tachyon_reg.FieldConstraint,
        types: Sequence[tachyon_model.ClkType],
        serdeses: dict[int, SerDes[Any]],
    ) -> None:
        """Create a SerDes for array types."""
        self.constraint = constraint
        self.size = size
        element_serdes = _serdes_for_type(
            compiler_context=compiler_context,
            type_id=element_type_id,
            metadata_name=None,
            types=types,
            serdeses=serdeses,
        )
        self.element_serializer = element_serdes.serializer
        self.element_deserializer = element_serdes.deserializer
        element_constraint = element_serdes.constraint
        self.stride = element_constraint.array_stride()

    def serialize(self, obj: Sequence[T], buffer: memoryview) -> None:
        """Serializer for type T."""
        if len(obj) != self.size:
            msg = f"Attempt to serialize array of length {len(obj)}, expected {self.size}"
            raise ValueError(msg)
        offset = 0
        for elem in obj:
            next_offset = offset + self.stride
            self.element_serializer(elem, buffer[offset:next_offset])
            offset = next_offset

    def deserialize(self, buffer: memoryview) -> list[T]:
        """Deserializer for type T."""
        offset = 0
        result = [cast("T", None)] * self.size
        for i in range(self.size):
            next_offset = offset + self.stride
            result[i] = self.element_deserializer(buffer[offset:next_offset])
            offset = next_offset
        return result

    def make_serdes(self) -> SerDes[list[T]]:
        """Construct a SerDes for the registry."""
        return SerDes(
            type_=list, fqn="", constraint=self.constraint, serializer=self.serialize, deserializer=self.deserialize
        )


class VarArraySerDes(Generic[T]):
    """SerDes for a variable array of T type."""

    # Suppressing PLR0913 (too many args) because this can't really be split. Args are kwonly to mitigate confusion.
    def __init__(  # noqa: PLR0913
        self,
        *,
        compiler_context: CompilerContext,
        max_size: int,
        element: int | SerDes[Any],
        constraint: tachyon_reg.FieldConstraint,
        types: Sequence[tachyon_model.ClkType],
        serdeses: dict[int, SerDes[Any]],
    ) -> None:
        """Create a SerDes for array types."""
        self.constraint = constraint
        self.max_size = max_size
        element_serdes = (
            element
            if isinstance(element, SerDes)
            else _serdes_for_type(
                compiler_context=compiler_context, type_id=element, metadata_name=None, types=types, serdeses=serdeses
            )
        )
        self.element_serializer = element_serdes.serializer
        self.element_deserializer = element_serdes.deserializer
        element_constraint = element_serdes.constraint
        self.stride = element_constraint.array_stride()
        size_field_offset = (
            math.ceil((self.stride * self.max_size) / tachyon_reg.SIZE_FIELD_ALIGNMENT)
            * tachyon_reg.SIZE_FIELD_ALIGNMENT
        )
        self.size_slice = slice(size_field_offset, size_field_offset + tachyon_reg.SIZE_FIELD_SIZE)

        registry = compiler_context[TACHYON_DYN_METADATA_REGISTRY_KEY]
        self.size_serdes = cast("SerDes[int]", registry.type_registry[clkbuiltins.UINT64.fqn])

    def serialize(self, obj: Sequence[T], buffer: memoryview) -> None:
        """Serializer for type T."""
        size = len(obj)
        if size > self.max_size:
            msg = f"Attempt to serialize array of length {len(obj)}, max {self.max_size}"
            raise ValueError(msg)
        offset = 0
        for elem in obj:
            next_offset = offset + self.stride
            self.element_serializer(elem, buffer[offset:next_offset])
            offset = next_offset
        self.size_serdes.serializer(size, buffer[self.size_slice])

    def deserialize(self, buffer: memoryview) -> list[T]:
        """Deserializer for type T."""
        size = self.size_serdes.deserializer(buffer[self.size_slice])
        result: list[T] = [cast("T", None)] * size
        offset = 0
        for i in range(size):
            next_offset = offset + self.stride
            result[i] = self.element_deserializer(buffer[offset:next_offset])
            offset = next_offset
        return result

    def make_serdes(self) -> SerDes[list[T]]:
        """Construct a SerDes for the registry."""
        return SerDes(
            type_=list, fqn="", constraint=self.constraint, serializer=self.serialize, deserializer=self.deserialize
        )


def _tensor_factory(
    compiler_context: CompilerContext,
    type_id: int,
    types: Sequence[tachyon_model.ClkType],
    serdeses: dict[int, SerDes[Any]],
) -> SerDes[Any] | None:
    """SerDes factory for FixedArray."""
    typ = types[type_id]
    if typ.fqn != tensor_builtins.TENSOR.fqn:
        msg = f"Expected Tensor but got {typ.fqn}"
        raise RuntimeError(msg)
    assert isinstance(typ, tachyon_model.BuiltInType)
    element_type_id, shape_spec, layout_spec = typ.arguments
    assert isinstance(element_type_id, int)

    assert isinstance(shape_spec, str)
    matches = re.match(r"\[([0-9,]+)\]", shape_spec)
    # LIST's value_key will always be formated like above
    assert matches is not None
    shape = list(map(int, matches.group(1).split(",")))

    assert isinstance(layout_spec, str)
    layout_id = int(layout_spec.split("::")[-1])
    strides = tensor_builtins.compute_tensor_strides(
        shape, clkenum.ValueRef.make(tensor_builtins.TENSOR_LAYOUT_ENUM.values[layout_id])
    )

    constraint = tachyon_reg.FieldConstraint(size=typ.size, alignment=typ.alignment)
    return TensorSerDes(
        compiler_context=compiler_context,
        shape=shape,
        strides=strides,
        element_type_id=element_type_id,
        constraint=constraint,
        types=types,
        serdeses=serdeses,
    ).make_serdes()


def _fixed_array_factory(
    compiler_context: CompilerContext,
    type_id: int,
    types: Sequence[tachyon_model.ClkType],
    serdeses: dict[int, SerDes[Any]],
) -> SerDes[Any] | None:
    """SerDes factory for FixedArray."""
    typ = types[type_id]
    if typ.fqn != clkbuiltins.FIXED_ARRAY.fqn:
        msg = f"Expected FixedArray but got {typ.fqn}"
        raise RuntimeError(msg)
    assert isinstance(typ, tachyon_model.BuiltInType)
    element_type_id, size_str = typ.arguments
    assert isinstance(element_type_id, int)
    assert isinstance(size_str, str)
    size = int(size_str)
    constraint = tachyon_reg.FieldConstraint(size=typ.size, alignment=typ.alignment)
    return FixedArraySerDes(
        compiler_context=compiler_context,
        size=size,
        element_type_id=element_type_id,
        constraint=constraint,
        types=types,
        serdeses=serdeses,
    ).make_serdes()


def _var_array_factory(
    compiler_context: CompilerContext,
    type_id: int,
    types: Sequence[tachyon_model.ClkType],
    serdeses: dict[int, SerDes[Any]],
) -> SerDes[list[Any]] | None:
    """SerDes factory for VarArray."""
    typ = types[type_id]
    if typ.fqn != clkbuiltins.VAR_ARRAY.fqn:
        msg = f"Expected VarArray but got {typ.fqn}"
        raise RuntimeError(msg)
    assert isinstance(typ, tachyon_model.BuiltInType)
    element_type_id, size_str = typ.arguments
    assert isinstance(element_type_id, int)
    assert isinstance(size_str, str)
    size = int(size_str)
    constraint = tachyon_reg.FieldConstraint(size=typ.size, alignment=typ.alignment)
    return VarArraySerDes(
        compiler_context=compiler_context,
        max_size=size,
        element=element_type_id,
        constraint=constraint,
        types=types,
        serdeses=serdeses,
    ).make_serdes()


def _var_string_factory(
    compiler_context: CompilerContext,
    type_id: int,
    types: Sequence[tachyon_model.ClkType],
    serdeses: dict[int, SerDes[Any]],
) -> SerDes[str] | None:
    """SerDes factory for VarString."""
    typ = types[type_id]
    if typ.fqn != clkbuiltins.VAR_STRING.fqn:
        msg = f"Expected VarString but got {typ.fqn}"
        raise RuntimeError(msg)
    assert isinstance(typ, tachyon_model.BuiltInType)
    (size_str,) = typ.arguments
    assert isinstance(size_str, str)
    size = int(size_str)
    constraint = tachyon_reg.FieldConstraint(size=typ.size, alignment=typ.alignment)

    registry = compiler_context[TACHYON_DYN_METADATA_REGISTRY_KEY]
    array_serdes = cast(
        "SerDes[list[int]]",
        VarArraySerDes(
            compiler_context=compiler_context,
            max_size=size,
            element=registry.type_registry[clkbuiltins.BYTE.fqn],
            constraint=constraint,
            types=types,
            serdeses=serdeses,
        ).make_serdes(),
    )
    assert array_serdes.constraint == constraint

    def _serialize(obj: str, buffer: memoryview) -> None:
        array_serdes.serializer([ord(x) for x in obj], buffer)

    def _deserialize(buffer: memoryview) -> str:
        return "".join(chr(x) for x in array_serdes.deserializer(buffer))

    return SerDes(type_=str, fqn="", constraint=constraint, serializer=_serialize, deserializer=_deserialize)


class OptionalSerDes(Generic[T]):
    """SerDes for Optional<T>."""

    def __init__(
        self,
        compiler_context: CompilerContext,
        type_id: int,
        types: Sequence[tachyon_model.ClkType],
        serdeses: dict[int, SerDes[Any]],
    ) -> None:
        """Create a SerDes for array types."""
        self.model_type = types[type_id]
        assert isinstance(self.model_type, tachyon_model.BuiltInType)
        self.constraint = tachyon_reg.FieldConstraint(size=self.model_type.size, alignment=self.model_type.alignment)
        (self.element_type_id,) = self.model_type.arguments
        assert isinstance(self.element_type_id, int)
        self.element_type = types[self.element_type_id]
        underlying_serdes = _serdes_for_type(
            compiler_context=compiler_context,
            type_id=self.element_type_id,
            metadata_name=None,
            types=types,
            serdeses=serdeses,
        )
        self.underlying_py_type = underlying_serdes.type_
        self.underlying_serializer = underlying_serdes.serializer
        self.underlying_deserializer = underlying_serdes.deserializer
        element_constraint = underlying_serdes.constraint
        self.underlying_slice = slice(0, element_constraint.size)
        self.bool_slice = slice(element_constraint.size, element_constraint.size + 1)

        registry = compiler_context[TACHYON_DYN_METADATA_REGISTRY_KEY]
        self.bool_serdes: SerDes[bool] = cast("SerDes[bool]", registry.type_registry[clkbuiltins.BOOL.fqn])

    def serialize(self, obj: T | None, buffer: memoryview) -> None:
        """Serializer for type T."""
        if obj is None:
            self.bool_serdes.serializer(False, buffer[self.bool_slice])
        else:
            self.underlying_serializer(obj, buffer[self.underlying_slice])
            self.bool_serdes.serializer(True, buffer[self.bool_slice])

    def deserialize(self, buffer: memoryview) -> T | None:
        """Deserializer for type T."""
        flag = self.bool_serdes.deserializer(buffer[self.bool_slice])
        if flag:
            return cast("T", self.underlying_deserializer(buffer[self.underlying_slice]))
        return None

    def make_serdes(self) -> SerDes[T | None]:
        """Construct a SerDes for the registry."""
        return SerDes(
            type_=self.underlying_py_type | None,  # pyright: ignore[reportArgumentType] Type not known ahead of time
            fqn="",
            constraint=self.constraint,
            serializer=self.serialize,
            deserializer=self.deserialize,
        )


def _optional_factory(
    compiler_context: CompilerContext,
    type_id: int,
    types: Sequence[tachyon_model.ClkType],
    serdeses: dict[int, SerDes[Any]],
) -> SerDes[Any] | None:
    """SerDes factory for Optional."""
    typ = types[type_id]
    if typ.fqn != clkbuiltins.OPTIONAL.fqn:
        msg = f"Expected Optional but got {typ.fqn}"
        raise RuntimeError(msg)
    return OptionalSerDes(compiler_context, type_id, types, serdeses).make_serdes()


class SoaSerDes(Generic[T]):
    """SerDes for Struct-of-Arrays (SoA) types (both FixedSoa and VarSoa).

    SoA layout stores fields as separate contiguous arrays rather than array of structs.
    For example, FixedSoa<Point3f, 100> stores:
        x: [x0, x1, ..., x99]
        y: [y0, y1, ..., y99]
        z: [z0, z1, ..., z99]

    The Python representation is a dataclass with lists for each field.
    """

    # Suppressing PLR0913 (too many args) because this is a constructor. Args are kwonly to mitigate confusion.
    def __init__(  # noqa: PLR0913
        self,
        *,
        compiler_context: CompilerContext,
        schema_name: str,
        field_layouts: Sequence[tachyon_model.SchemaField],
        max_size: int,
        constraint: tachyon_reg.FieldConstraint,
        types: Sequence[tachyon_model.ClkType],
        serdeses: dict[int, SerDes[Any]],
        size_field_offset: int | None = None,
        size_field_type_id: int | None = None,
    ) -> None:
        """Create a SerDes for SoA types.

        Args:
            compiler_context: The compiler context.
            schema_name: Name of the underlying schema (for class name generation).
            field_layouts: Field layout information from SoaType metadata.
            max_size: Maximum number of elements (size for FixedSoa, max_size for VarSoa).
            constraint: Field constraint for the SoA type.
            types: All types from the metadata.
            serdeses: Cache of SerDes instances.
            size_field_offset: Offset of the size field (None for FixedSoa).
            size_field_type_id: Type ID of the size field (None for FixedSoa).
        """
        self.compiler_context = compiler_context
        self.schema_name = schema_name
        self.max_size = max_size
        self.constraint = constraint
        self.is_var = size_field_offset is not None
        self.types = types
        self.serdeses = serdeses

        # Tuple contents: [(field_name, offset, type_id, element_serdes)]
        self.field_serializers: list[tuple[str, int, int, SerDes[Any]]] = []

        for layout_field in field_layouts:
            element_serdes = _serdes_for_type(
                compiler_context=compiler_context,
                type_id=layout_field.type_id,
                metadata_name=None,
                types=types,
                serdeses=serdeses,
            )
            self.field_serializers.append(
                (layout_field.name, layout_field.offset, layout_field.type_id, element_serdes)
            )

        self.size_offset: int = 0
        self.size_field_size: int = 0
        self.size_serdes: SerDes[int] | None = None

        if self.is_var:
            assert size_field_offset is not None
            assert size_field_type_id is not None
            self.size_offset = size_field_offset
            size_field_serdes = _serdes_for_type(
                compiler_context=compiler_context,
                type_id=size_field_type_id,
                metadata_name=None,
                types=types,
                serdeses=serdeses,
            )
            self.size_serdes = cast("SerDes[int]", size_field_serdes)
            self.size_field_size = size_field_serdes.constraint.size

        self.py_class = self._create_dataclass()

    def _create_dataclass(self) -> type[Any]:
        """Create a dataclass with field arrays for this SoA type."""
        # Generate class name using schema name like the IR-based implementation
        class_name = f"{'Var' if self.is_var else 'Fixed'}Soa_{self.schema_name}_{self.max_size}"

        dataclass_fields: list[tuple[str, Any, Any] | tuple[str, Any]] = []
        for field_name, _offset, _type_id, element_serdes in self.field_serializers:
            element_type = element_serdes.type_

            if self.is_var:
                # VarSoa: default to empty list
                dataclass_fields.append(
                    (
                        field_name,
                        list[element_type],
                        field(default_factory=list),
                    )
                )
            else:
                # FixedSoa: no default, required field
                dataclass_fields.append(
                    (
                        field_name,
                        list[element_type],
                    )
                )

        return dataclasses.make_dataclass(class_name, fields=dataclass_fields, kw_only=True, slots=True, eq=True)

    def serialize(self, obj: object, buffer: memoryview) -> None:
        """Serialize SoA dataclass to buffer.

        Args:
            obj: SoA dataclass with field arrays (lists)
            buffer: Target buffer
        """
        if self.field_serializers:
            first_field_name, _, _, _ = self.field_serializers[0]
            actual_size = len(getattr(obj, first_field_name))
        else:
            actual_size = 0

        if self.is_var:
            if actual_size > self.max_size:
                msg = f"{self.py_class.__name__}: SoA size {actual_size} exceeds max_size {self.max_size}"
                raise ValueError(msg)
        elif actual_size != self.max_size:
            msg = (
                f"{self.py_class.__name__}: FixedSoa field array has wrong size {actual_size}, expected {self.max_size}"
            )
            raise ValueError(msg)

        for field_name, offset, _type_id, element_serdes in self.field_serializers:
            field_array = getattr(obj, field_name)
            if len(field_array) != actual_size:
                msg = f"{self.py_class.__name__}: All SoA field arrays must have same size, but {field_name} has {len(field_array)}, expected {actual_size}"
                raise ValueError(msg)

            element_size = element_serdes.constraint.size
            for i in range(actual_size):
                element_offset = offset + i * element_size
                element_serdes.serializer(field_array[i], buffer[element_offset : element_offset + element_size])

        if self.is_var and self.size_serdes is not None:
            self.size_serdes.serializer(actual_size, buffer[self.size_offset : self.size_offset + self.size_field_size])

    def deserialize(self, buffer: memoryview) -> object:
        """Deserialize buffer to SoA dataclass.

        Args:
            buffer: Source buffer

        Returns:
            SoA dataclass with lists for each field
        """
        if self.is_var and self.size_serdes is not None:
            actual_size = self.size_serdes.deserializer(
                buffer[self.size_offset : self.size_offset + self.size_field_size]
            )
        else:
            actual_size = self.max_size

        field_values = {}
        for field_name, offset, _type_id, element_serdes in self.field_serializers:
            field_array: list[Any] = [cast("Any", None)] * actual_size
            element_size = element_serdes.constraint.size
            for i in range(actual_size):
                element_offset = offset + i * element_size
                field_array[i] = element_serdes.deserializer(buffer[element_offset : element_offset + element_size])
            field_values[field_name] = field_array

        # pyrefly: ignore[no-any-return-implicit] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return self.py_class(**field_values)

    def make_serdes(self) -> SerDes[T]:
        """Create a SerDes for the registry."""
        # pyrefly: ignore[bad-return] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return SerDes(
            type_=self.py_class,
            fqn="",
            constraint=self.constraint,
            serializer=self.serialize,
            deserializer=self.deserialize,
        )


def _soa_type_factory(
    compiler_context: CompilerContext,
    type_id: int,  # noqa: ARG001 required for consistent factory signature
    soa_type: tachyon_model.SoaType,
    types: Sequence[tachyon_model.ClkType],
    serdeses: dict[int, SerDes[Any]],
) -> SerDes[Any] | None:
    """SerDes factory for SoaType metadata."""
    constraint = tachyon_reg.FieldConstraint(size=soa_type.size, alignment=soa_type.alignment)

    # Get the schema name for class name generation
    schema_type = types[soa_type.schema_type_id]
    assert isinstance(schema_type, tachyon_model.SchemaType)
    # Extract the simple name from the FQN (e.g., "Point3f" from "@repo::namespace::Point3f")
    schema_name = schema_type.fqn.rpartition("::")[-1]

    return SoaSerDes(
        compiler_context=compiler_context,
        schema_name=schema_name,
        field_layouts=soa_type.field_layouts,
        max_size=soa_type.container_size,
        constraint=constraint,
        types=types,
        serdeses=serdeses,
        size_field_offset=soa_type.size_field_offset,
        size_field_type_id=soa_type.size_field_type_id,
    ).make_serdes()


@dataclass
class SchemaSerDes(Generic[T]):
    """SerDes for a schema type."""

    model_type: tachyon_model.SchemaType
    py_class: type[T]
    field_serdeses: list[tuple[str, slice, SerDes[Any]]]

    def serialize(self, obj: Any, buffer: memoryview) -> None:  # noqa: ANN401 type information is not known ahead of time.
        """Serializer for schema type."""
        for fld_name, slice_, serdes in self.field_serdeses:
            serdes.serializer(getattr(obj, fld_name), buffer[slice_])

    def deserialize(self, buffer: memoryview) -> Any:  # noqa: ANN401 type information is not known ahead of time.
        """Deserializer for type T."""
        return self.py_class(
            **{fld_name: serdes.deserializer(buffer[slice_]) for fld_name, slice_, serdes in self.field_serdeses}
        )

    @classmethod
    # Suppressing PLR0913 (too many args) because this can't really be split. Args are kwonly to mitigate confusion.
    def make(  # noqa: PLR0913
        cls: type[SchemaSerDes[T]],
        *,
        compiler_context: CompilerContext,
        type_id: int,
        metadata_name: str | None,
        schema: tachyon_model.SchemaType,
        types: Sequence[tachyon_model.ClkType],
        serdeses: dict[int, SerDes[Any]],
    ) -> SchemaSerDes[T]:
        """Construct a SerDes for the schema."""
        field_serdeses = []
        dataclass_fields = []

        for schema_field in sorted(schema.fields, key=lambda x: x.num):
            serdes = _serdes_for_type(
                compiler_context=compiler_context,
                type_id=schema_field.type_id,
                metadata_name=None,
                types=types,
                serdeses=serdeses,
            )
            field_serdeses.append(
                (schema_field.name, slice(schema_field.offset, schema_field.offset + serdes.constraint.size), serdes)
            )
            dataclass_fields.append((schema_field.name, serdes.type_))
        schema_name = schema.fqn.rpartition("::")[-1]
        py_class: Any = dataclasses.make_dataclass(schema_name, fields=dataclass_fields, kw_only=True, slots=True)
        schema_serdes: SchemaSerDes[Any] = SchemaSerDes(
            model_type=schema, py_class=py_class, field_serdeses=field_serdeses
        )

        def serialize_tachyon(self: Any, buffer: memoryview) -> None:  # noqa: ANN401 type information is not known ahead of time.
            schema_serdes.serialize(self, buffer)

        # pyrefly: ignore[invalid-decorator] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        @classmethod
        def deserialize_tachyon(_: type, buffer: memoryview) -> Any:  # noqa: ANN401 type information is not known ahead of time.
            return schema_serdes.deserialize(buffer)

        constraint = tachyon_reg.FieldConstraint(size=schema.size, alignment=schema.alignment)

        # pyrefly: ignore[invalid-decorator] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        @classmethod
        def get_tachyon_constraint(_: type) -> tachyon_reg.FieldConstraint:
            return constraint

        # pyrefly: ignore[invalid-decorator] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        @classmethod
        def get_tachyon_metadata_name(_: type) -> str:
            if metadata_name is None:
                msg = "Tachyon metadata name is undefined for inner generated types"
                raise RuntimeError(msg)
            return metadata_name

        py_class_metadata = tachyon_model.TachyonMetadata(outer_type_id=type_id, types=types)

        # pyrefly: ignore[invalid-decorator] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        @classmethod
        def get_tachyon_metadata(_: type) -> tachyon_model.TachyonMetadata:
            return py_class_metadata

        py_class.serialize_tachyon = serialize_tachyon
        py_class.deserialize_tachyon = deserialize_tachyon
        py_class.get_tachyon_constraint = get_tachyon_constraint
        py_class.get_tachyon_metadata_name = get_tachyon_metadata_name
        py_class.get_tachyon_metadata = get_tachyon_metadata

        return schema_serdes

    def make_serdes(self) -> SerDes[T]:
        """Create a SerDes for this schema."""
        return SerDes(
            type_=self.py_class,
            fqn=self.model_type.fqn,
            constraint=tachyon_reg.FieldConstraint(size=self.model_type.size, alignment=self.model_type.alignment),
            serializer=self.serialize,
            deserializer=self.deserialize,
        )


def py_type_from_metadata(
    compiler_context: CompilerContext, metadata_name: str, metadata: tachyon_model.TachyonMetadata
) -> tuple[type[protocol.Tachyon[Any]], dict[str, Any]]:
    """Create a (set of) PyTachyon classes from Tachyon metadata.

    Returns:
        A tuple of the "outer" schema type and a dictionary of sub-types used by
        that schema recursively, keyed by the value key of the types.
    """
    serdeses: dict[int, SerDes[Any]] = {}
    serdes = _serdes_for_type(
        compiler_context=compiler_context,
        type_id=metadata.outer_type_id,
        metadata_name=metadata_name,
        types=metadata.types,
        serdeses=serdeses,
    )
    type_by_fqn = {serdes.fqn: cast("protocol.Tachyon[Any]", serdes.type_) for serdes in serdeses.values()}
    return serdes.type_, type_by_fqn


def py_type_from_pb_metadata(
    compiler_context: CompilerContext, metadata_name: str, data: bytes
) -> tuple[Any, dict[str, Any]]:
    """Create a (set of) PyTachyon classes from PB2-serialized Tachyon metadata.

    Returns:
        A tuple of the "outer" schema type and a dictionary of sub-types used by
        that schema recursively, keyed by the value key of the types.
    """
    return py_type_from_metadata(compiler_context, metadata_name, tachyon_metadata.get_metadata_from_protobuf(data))
