# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Tachyon Python Serializer Registry."""

from __future__ import annotations

import copy
import dataclasses
import enum
import math
import operator
import struct
import uuid
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from decimal import Decimal
from functools import reduce
from typing import TYPE_CHECKING, Any, Final, Generic, TypeAlias, TypeVar, cast

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.ir import (
    clkbuiltins,
    clkenum,
    node,
    primitive,
    schema,
    statement,
    strongtypes,
    tensor_builtins,
    typesys,
)
from clockwork.dsl.serialization import tachyon_layout, tachyon_layout_reg, tachyon_reg
from clockwork.serialization.metadata import tachyon as tachyon_meta
from clockwork.serialization.metadata import tachyon_model
from typing_extensions import override

if TYPE_CHECKING:
    from clockwork.serialization.py.protocol import Tachyon

T = TypeVar("T")

Serializer: TypeAlias = Callable[[T, memoryview], None]
Deserializer: TypeAlias = Callable[[memoryview], T]


@dataclass
class ScopeLookup:
    """A Value that needs to be looked up."""

    name: str
    module: node.Module | None
    arguments: dict[str, typesys.Value | float | ScopeLookup] | None = None


@dataclass(eq=True, frozen=True)
class SerDes(Generic[T]):  # noqa: PLW1641 __hash__ function not needed.
    """Holds a serializer and deserializer for type T."""

    type_: type[T] = field(repr=False)
    constraint: tachyon_reg.FieldConstraint
    serializer: Serializer[T] = field(repr=False)
    deserializer: Deserializer[T] = field(repr=False)
    clk_type: typesys.TypeVal

    @override
    def __eq__(self, other: object) -> bool:
        """Equality comparison."""
        return (
            isinstance(other, SerDes)
            and self.type_ == other.type_
            and self.constraint == other.constraint
            and self.clk_type == other.clk_type
        )


def _get_constraint(compiler_context: CompilerContext, clk_type: typesys.TypeVal) -> tachyon_reg.FieldConstraint:
    """Get constraint for a type, handling the initialization case."""
    constraint = tachyon_reg.constraint_for_type(compiler_context, clk_type)
    if constraint is None:
        msg = f"No Tachyon registration for type; did you forget to instantiate it? {clk_type}"
        raise ValueError(msg)
    return constraint


TypeKey: TypeAlias = str


class TachyonDynRegistry(Context):
    """Registry for Tachyon Python serializers."""

    def __init__(self, name: str | None) -> None:
        """Create a new, empty Tachyon serializer registry."""
        self.name = name
        self.type_registry: dict[TypeKey, tuple[SerDes[Any], str | None]] = {}
        self.generic_type_registry: dict[
            TypeKey, Callable[[CompilerContext, typesys.Instantiation], SerDes[Any] | None]
        ] = {}

    @override
    def import_from(self, other: TachyonDynRegistry) -> None:
        """Combine this context with items from another.

        Raises:
            RuntimeError: If a type already exists with a different SerDes.
        """
        for key, (serdes, name) in other.type_registry.items():
            if key in self.type_registry and self.type_registry[key][0] != serdes:
                msg = f"Type {key} has conflicting SerDes: {self.type_registry[key][0]} vs {serdes}\nImporting {other.name} into {self.name}\nOurs comes from {self.type_registry[key][1]}\nOther comes from {name}"
                raise RuntimeError(msg)
            self.type_registry[key] = (serdes, name)

        for generic_key, factory in other.generic_type_registry.items():
            if generic_key in self.generic_type_registry and self.generic_type_registry[generic_key] is not factory:
                msg = f"Generic factory {generic_key} has conflicting registrations"
                raise RuntimeError(msg)
            self.generic_type_registry[generic_key] = factory


def maybe_serdes_for_type(
    compiler_context: CompilerContext,
    typ: typesys.TypeVal,
    auto_create: bool = True,
) -> SerDes[Any] | None:
    """Look up a serializer/deserializer (SerDes) for a type.

    Args:
        compiler_context: Compiler context containing the registry.
        typ: The type to look up.
        auto_create: If True and the type is not registered, register it automatically if possible.

    Returns:
        The SerDes if found (or registered automatically), else None
    """
    registry = compiler_context[TACHYON_DYN_REGISTRY_KEY]
    try:
        return registry.type_registry[typ.value_key()][0]
    except KeyError:
        pass

    if isinstance(typ, typesys.Instantiation):
        # Only attempt instantiations if we have a context
        try:
            factory = registry.generic_type_registry[typ.instantiates.value_key()]
            serdes = factory(compiler_context, typ)
            if serdes is not None:
                registry.type_registry[typ.value_key()] = (serdes, registry.name)
            return serdes  # noqa: TRY300
        except KeyError:
            pass

    if auto_create:
        return try_create_serdes(compiler_context, typ)
    return None


def try_create_serdes(
    compiler_context: CompilerContext,
    typ: typesys.TypeVal,
) -> SerDes[Any] | None:
    """Try to create and register a SerDes for the given type."""
    if isinstance(typ, clkenum.ResolvedEnum):
        enum_serdes = _enum_factory(compiler_context, typ)
        register_type(compiler_context, typ, enum_serdes)
        return enum_serdes
    if isinstance(typ, strongtypes.StrongType):
        strong_serdes = _strong_type_factory(compiler_context, typ)
        register_type(compiler_context, typ, strong_serdes)
        return strong_serdes
    if isinstance(typ, schema.Schema | schema.ResolvedSchema | typesys.Instantiation):
        try:
            typ = schema.InstantiatedSchema.from_typespec(typ)
        except TypeError:
            return None
    if not isinstance(typ, schema.InstantiatedSchema):
        return None
    schema_serdes: SchemaSerDes[Any] = SchemaSerDes.make(compiler_context, typ)
    serdes = schema_serdes.make_serdes()
    register_type(compiler_context, typ, serdes)
    return serdes


def serdes_for_type(compiler_context: CompilerContext, typ: typesys.TypeVal) -> SerDes[Any]:
    """Look up the SerDes for a type.

    Args:
        compiler_context: Compiler context containing the registry.
        typ: The type to look up.

    Returns:
        The SerDes

    Raises:
        NotImplementedError if the type is not supported.
    """
    result = maybe_serdes_for_type(compiler_context, typ)
    if result is None:
        msg = f"Cannot create Python serializer for {typ}."
        raise NotImplementedError(msg)
    return result


def register_type(compiler_context: CompilerContext, typ: typesys.TypeVal, serdes: SerDes[Any]) -> None:
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
    existing = maybe_serdes_for_type(compiler_context, typ, auto_create=False)
    if existing:
        if serdes != existing:
            msg = (
                f"Attempt to register SerDes {serdes} "
                f"which does not match previous registration {existing} "
                f"for type {typ}"
            )
            raise RuntimeError(msg)
        return
    compiler_context[TACHYON_DYN_REGISTRY_KEY].type_registry[typ.value_key()] = (serdes, compiler_context.name)


def register_generic_type(
    compiler_context: CompilerContext,
    typ: typesys.TypeVal,
    factory: Callable[[CompilerContext, typesys.Instantiation], SerDes[Any] | None],
) -> None:
    """Register a SerDes factory for a generic type.

    Args:
        compiler_context: Compiler context containing the registry.
        typ: The generic type to register
        factory: A factory function that generates a SerDes for an instantiation.

    Raises:
        RuntimeError if the type is not a generic type or is already registered.
    """
    registry = compiler_context[TACHYON_DYN_REGISTRY_KEY]
    if not typ.generic_parameters():
        msg = f"Attempt to register a factory for non-generic type {typ}"
        raise RuntimeError(msg)
    if typ.value_key() in registry.generic_type_registry:
        msg = f"Attempt to register already-registered generic type {typ}"
        raise RuntimeError(msg)
    registry.generic_type_registry[typ.value_key()] = factory


class TachyonDynRegistryKey(ContextKey[TachyonDynRegistry]):
    """Compiler context key for Tachyon Python serializer registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> TachyonDynRegistry:
        """Create a default instance of the registry with built-in types."""
        registry = TachyonDynRegistry(compiler_context.name)
        # Register all built-in types from the module-level constants
        registry.type_registry.update(BUILTIN_SERDES)

        # Register generic types with their factories
        registry.generic_type_registry[clkbuiltins.UUID.value_key()] = _uuid_factory
        registry.generic_type_registry[clkbuiltins.BITSET.value_key()] = _bitset_factory
        registry.generic_type_registry[clkbuiltins.FIXED_ARRAY.value_key()] = _fixed_array_factory
        registry.generic_type_registry[tensor_builtins.TENSOR.value_key()] = _tensor_factory
        registry.generic_type_registry[clkbuiltins.OPTIONAL.value_key()] = _optional_factory
        registry.generic_type_registry[clkbuiltins.VAR_ARRAY.value_key()] = _var_array_factory
        registry.generic_type_registry[clkbuiltins.VAR_STRING.value_key()] = _var_string_factory
        registry.generic_type_registry[clkbuiltins.FIXED_SOA.value_key()] = _fixed_soa_factory
        registry.generic_type_registry[clkbuiltins.VAR_SOA.value_key()] = _var_soa_factory
        return registry


TACHYON_DYN_REGISTRY_KEY: Final = TachyonDynRegistryKey("TachyonDynRegistry")


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

    def deserialize(self, buffer: memoryview) -> Any:  # noqa: ANN401 type information is not known ahead of time.
        """Deserializer for type T."""
        return self.struct.unpack_from(buffer, 0)[0]

    def make_serdes(self) -> SerDes[T]:
        """Construct a SerDes for the registry."""
        return SerDes(
            type_=self.py_type,
            constraint=_get_constraint(self.compiler_context, self.clk_type),
            serializer=self.serialize,
            deserializer=self.deserialize,
            clk_type=self.clk_type,
        )


def uuid_serialize(obj: uuid.UUID, buffer: memoryview) -> None:
    """Serializer for UUIDs."""
    buffer[:] = obj.bytes


def uuid_deserialize(buffer: memoryview) -> uuid.UUID:
    """Deserializer for UUIDs."""
    return uuid.UUID(bytes=buffer.tobytes())


UUID_SERDES: Final = SerDes(
    type_=uuid.UUID,
    constraint=tachyon_reg.UUID_CONSTRAINT,
    serializer=uuid_serialize,
    deserializer=uuid_deserialize,
    clk_type=clkbuiltins.UUID,
)


# ARG001 suppressed because we need to conform to a specific call signature
def _uuid_factory(compiler_context: CompilerContext, typ: typesys.Instantiation) -> SerDes[uuid.UUID] | None:  # noqa: ARG001
    """Helper function for Uuid."""
    if typ.instantiates is not clkbuiltins.UUID:
        msg = f"Expected Uuid but got {typ.instantiates}"
        raise RuntimeError(msg)
    # We align UUIDs to 8 bytes so they can be treated as two int64's, e.g. for fast hash computation.
    return UUID_SERDES


def _bitset_factory(compiler_context: CompilerContext, typ: typesys.Instantiation) -> SerDes[int] | None:
    """Create a serializer for Bitset."""
    if typ.instantiates is not clkbuiltins.BITSET:
        msg = f"Expected Bitset but got {typ.instantiates}"
        raise RuntimeError(msg)
    size_arg = typ.arguments["size"]
    if not isinstance(size_arg, primitive.DecimalValue):
        msg = f"Bad value type for Bitset size parameter: {size_arg}"
        raise TypeError(msg)
    bit_size = primitive.unsigned_decimal_to_int(size_arg)
    if bit_size <= 0:
        msg = "Bitset size must be greater than zero"
        raise ValueError(msg)
    constraint = _get_constraint(compiler_context, typ)
    maximum_value = (1 << bit_size) - 1

    def _serialize(obj: int, buffer: memoryview) -> None:
        if obj < 0 or obj > maximum_value:
            msg = f"Bitset<{bit_size}> value must be in [0, {maximum_value}]"
            raise ValueError(msg)
        buffer[:] = obj.to_bytes(constraint.size, byteorder="little", signed=False)

    def _deserialize(buffer: memoryview) -> int:
        return int.from_bytes(buffer, byteorder="little", signed=False) & maximum_value

    return SerDes(type_=int, constraint=constraint, serializer=_serialize, deserializer=_deserialize, clk_type=typ)


def _make_type(mcls: type, name: str, bases: tuple[type, ...], members: dict[str, Any], **kwds: dict[str, Any]) -> type:
    """This replicates class creation with metaclass support.

    class name(*bases, **kwds, metaclass=mcls):
        **members

    """
    clsdict = mcls.__prepare__(name, bases, **kwds)
    clsdict.update(members)
    # pyrefly: ignore[no-any-return-implicit] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    return mcls(name, bases, clsdict, **kwds)


class EnumSerDes:  # noqa: PLW1641 Intentionally leaving out __hash__ because this is a mutable type.
    """SerDes for an enum type."""

    def __init__(self, compiler_context: CompilerContext, clk_type: clkenum.ResolvedEnum) -> None:
        """Create a SerDes for array types."""
        self.compiler_context = compiler_context
        self.clk_type = clk_type
        self.underlying_type = clk_type.underlying_type
        values = [(value_def.name, value_def.integer_value) for _, value_def in sorted(self.clk_type.values.items())]
        if self.clk_type.uuid is not None:

            class UuidEnumCmp:
                uuid_ = self.clk_type.uuid

                @override
                def __eq__(self: UuidEnumCmp, other: object) -> bool:
                    assert isinstance(self, enum.Enum)
                    if isinstance(other, type(self)):
                        # pyrefly: ignore[no-any-return-implicit] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                        return self is other
                    # pyrefly: ignore[missing-attribute] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                    if isinstance(other, enum.Enum) and getattr(type(other), "uuid_", object()) == type(self).uuid_:
                        return self.value == other.value
                    return NotImplemented

                @override
                def __hash__(self: UuidEnumCmp) -> int:
                    assert isinstance(self, enum.Enum)
                    return hash(self.value)

            impl = _make_type(
                enum.EnumMeta,
                self.clk_type.name + "Type",
                (UuidEnumCmp, enum.Flag if clk_type.bit_flags else enum.Enum),
                {},
            )
        else:
            impl: type = enum.Flag if clk_type.bit_flags else enum.Enum
        self.py_type: Any = impl(self.clk_type.name, values)
        underlying_serdes = serdes_for_type(compiler_context, self.underlying_type)
        self.underlying_serializer = underlying_serdes.serializer
        self.underlying_deserializer = underlying_serdes.deserializer

    @override
    def __eq__(self, other: object) -> bool:
        """Equality comparison."""
        return (
            isinstance(other, EnumSerDes)
            and self.clk_type is other.clk_type
            and self.underlying_type is other.underlying_type
        )

    def serialize(self, obj: enum.Enum, buffer: memoryview) -> None:
        """Serializer for enums."""
        self.underlying_serializer(obj.value, buffer)

    def deserialize(self, buffer: memoryview) -> Any:  # noqa: ANN401 type information is not known ahead of time.
        """Deserializer for enums."""
        underlying_int = self.underlying_deserializer(buffer)
        return self.py_type(underlying_int)

    def make_serdes(self) -> SerDes[enum.Enum]:
        """Construct a SerDes for the registry."""
        return SerDes(
            type_=self.py_type,
            constraint=_get_constraint(self.compiler_context, self.clk_type),
            serializer=self.serialize,
            deserializer=self.deserialize,
            clk_type=self.clk_type,
        )


def _enum_factory(compiler_context: CompilerContext, clk_enum: clkenum.ResolvedEnum) -> SerDes[enum.Enum]:
    """Get a serializer for a dynamic Enum type."""
    return EnumSerDes(compiler_context, clk_enum).make_serdes()


def _strong_type_factory(compiler_context: CompilerContext, strong_type: strongtypes.StrongType) -> SerDes[float]:
    """Get a serializer for a dynamic StrongType."""
    underlying = strong_type.typespec
    if not isinstance(underlying, clkbuiltins.PrimitiveType):
        msg = f"Cannot create SerDes for unresolved strong type {strong_type}"
        raise RuntimeError(msg)  # noqa: TRY004
    return serdes_for_type(compiler_context, underlying)


class FixedArraySerDes(Generic[T]):
    """SerDes for a fixed-size array of T type."""

    def __init__(
        self,
        compiler_context: CompilerContext,
        size: int,
        underlying_type: typesys.TypeVal,
        constraint: tachyon_reg.FieldConstraint,
    ) -> None:
        """Create a SerDes for array types."""
        self.constraint = constraint
        self.size = size
        underlying_serdes = serdes_for_type(compiler_context, underlying_type)
        self.underlying_serializer = underlying_serdes.serializer
        self.underlying_deserializer = underlying_serdes.deserializer
        element_constraint = _get_constraint(compiler_context, underlying_type)
        self.stride = element_constraint.array_stride()

    def serialize(self, obj: Sequence[T], buffer: memoryview) -> None:
        """Serializer for type T."""
        if len(obj) != self.size:
            msg = f"Attempt to serialize array of length {len(obj)}, expected {self.size}"
            raise ValueError(msg)
        offset = 0
        for elem in obj:
            next_offset = offset + self.stride
            self.underlying_serializer(elem, buffer[offset:next_offset])
            offset = next_offset

    def deserialize(self, buffer: memoryview) -> list[T]:
        """Deserializer for type T."""
        offset = 0
        result = [cast("T", None)] * self.size
        for i in range(self.size):
            next_offset = offset + self.stride
            result[i] = self.underlying_deserializer(buffer[offset:next_offset])
            offset = next_offset
        return result

    def make_serdes(self) -> SerDes[list[T]]:
        """Construct a SerDes for the registry."""
        return SerDes(
            type_=list,
            constraint=self.constraint,
            serializer=self.serialize,
            deserializer=self.deserialize,
            clk_type=clkbuiltins.FIXED_ARRAY,
        )


class TensorSerDes(Generic[T]):
    """SerDes for a tensor of T type."""

    def __init__(
        self,
        compiler_context: CompilerContext,
        shape: list[int],
        strides: list[int],
        element_type: typesys.TypeVal,
        constraint: tachyon_reg.FieldConstraint,
    ) -> None:
        """Create a SerDes for array types."""
        self.constraint = constraint
        self.shape = shape
        self.strides = strides
        self.num_elements = reduce(operator.mul, self.shape)
        element_serdes = serdes_for_type(compiler_context, element_type)
        self.element_serializer = element_serdes.serializer
        self.element_deserializer = element_serdes.deserializer
        element_constraint = _get_constraint(compiler_context, element_type)
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
            constraint=self.constraint,
            serializer=self.serialize,
            deserializer=self.deserialize,
            clk_type=tensor_builtins.TENSOR,
        )


class VarArraySerDes(Generic[T]):
    """SerDes for a variable array of T type."""

    def __init__(
        self,
        compiler_context: CompilerContext,
        max_size: int,
        underlying_type: typesys.TypeVal,
        constraint: tachyon_reg.FieldConstraint,
    ) -> None:
        """Create a SerDes for array types."""
        self.constraint = constraint
        self.max_size = max_size
        underlying_serdes = serdes_for_type(compiler_context, underlying_type)
        self.underlying_serializer = underlying_serdes.serializer
        self.underlying_deserializer = underlying_serdes.deserializer
        element_constraint = _get_constraint(compiler_context, underlying_type)
        self.stride = element_constraint.array_stride()
        size_field_offset = (
            math.ceil((self.stride * self.max_size) / tachyon_reg.SIZE_FIELD_ALIGNMENT)
            * tachyon_reg.SIZE_FIELD_ALIGNMENT
        )
        self.size_slice = slice(size_field_offset, size_field_offset + tachyon_reg.SIZE_FIELD_SIZE)
        self.size_serdes = cast("SerDes[int]", serdes_for_type(compiler_context, clkbuiltins.UINT64))

    def serialize(self, obj: Sequence[T], buffer: memoryview) -> None:
        """Serializer for type T."""
        size = len(obj)
        if size > self.max_size:
            msg = f"Attempt to serialize array of length {len(obj)}, max {self.max_size}"
            raise ValueError(msg)
        offset = 0
        for elem in obj:
            next_offset = offset + self.stride
            self.underlying_serializer(elem, buffer[offset:next_offset])
            offset = next_offset
        self.size_serdes.serializer(size, buffer[self.size_slice])

    def deserialize(self, buffer: memoryview) -> list[T]:
        """Deserializer for type T."""
        size = self.size_serdes.deserializer(buffer[self.size_slice])
        result: list[T] = [cast("T", None)] * size
        offset = 0
        for i in range(size):
            next_offset = offset + self.stride
            result[i] = self.underlying_deserializer(buffer[offset:next_offset])
            offset = next_offset
        return result

    def make_serdes(self) -> SerDes[list[T]]:
        """Construct a SerDes for the registry."""
        return SerDes(
            type_=list,
            constraint=self.constraint,
            serializer=self.serialize,
            deserializer=self.deserialize,
            clk_type=clkbuiltins.VAR_ARRAY,
        )


def _array_type_checker(
    compiler_context: CompilerContext,
    clk_type: typesys.TypeVal,
    element_type: typesys.Value,
    size: typesys.Value,
) -> tuple[typesys.TypeVal, int, SerDes[Any], tachyon_reg.FieldConstraint]:
    """Helper function to extract size, type, and underlying serializer for array-like types."""
    if not isinstance(element_type, typesys.TypeVal):
        msg = f"Bad value type for type parameter: {element_type}"
        raise RuntimeError(msg)  # noqa: TRY004
    if not isinstance(size, primitive.DecimalValue) or int(size.value) != size.value:
        msg = f"Bad value type for size parameter: {size}"
        raise RuntimeError(msg)
    element_serdes = serdes_for_type(compiler_context, element_type)
    constraint = _get_constraint(compiler_context, clk_type)
    return element_type, int(size.value), element_serdes, constraint


def _tensor_factory(compiler_context: CompilerContext, typ: typesys.Instantiation) -> SerDes[Any] | None:
    """SerDes factory for FixedArray."""
    element_type = tensor_builtins.get_tensor_element_type(typ)
    shape = tensor_builtins.get_tensor_shape(typ)
    strides = tensor_builtins.get_tensor_strides(typ)
    constraint = _get_constraint(compiler_context, typ)
    return TensorSerDes(compiler_context, shape, strides, element_type, constraint).make_serdes()


def _fixed_array_factory(compiler_context: CompilerContext, typ: typesys.Instantiation) -> SerDes[Any] | None:
    """SerDes factory for FixedArray."""
    if typ.instantiates is not clkbuiltins.FIXED_ARRAY:
        msg = f"Expected FixedArray but got {typ.instantiates}"
        raise RuntimeError(msg)
    element_type = typ.arguments["type"]
    size_arg = typ.arguments["size"]
    underlying_type, size, _serdes, constraint = _array_type_checker(compiler_context, typ, element_type, size_arg)
    return FixedArraySerDes(compiler_context, size, underlying_type, constraint).make_serdes()


def _var_array_factory(compiler_context: CompilerContext, typ: typesys.Instantiation) -> SerDes[list[Any]] | None:
    """SerDes factory for VarArray."""
    if typ.instantiates is not clkbuiltins.VAR_ARRAY:
        msg = f"Expected VarArray but got {typ.instantiates}"
        raise RuntimeError(msg)
    element_type = typ.arguments["type"]
    size_arg = typ.arguments["max_size"]
    underlying_type, max_size, _serdes, constraint = _array_type_checker(compiler_context, typ, element_type, size_arg)
    return VarArraySerDes(compiler_context, max_size, underlying_type, constraint).make_serdes()


def _var_string_factory(compiler_context: CompilerContext, typ: typesys.Instantiation) -> SerDes[str] | None:
    """SerDes factory for VarString."""
    if typ.instantiates is not clkbuiltins.VAR_STRING:
        msg = f"Expected VarString but got {typ.instantiates}"
        raise RuntimeError(msg)
    size_arg = typ.arguments["max_size"]
    underlying_type, max_size, _serdes, constraint = _array_type_checker(
        compiler_context, typ, clkbuiltins.BYTE, size_arg
    )
    array_serdes = cast(
        "SerDes[list[int]]", VarArraySerDes(compiler_context, max_size, underlying_type, constraint).make_serdes()
    )

    def _serialize(obj: str, buffer: memoryview) -> None:
        array_serdes.serializer([ord(x) for x in obj], buffer)

    def _deserialize(buffer: memoryview) -> str:
        return "".join(chr(x) for x in array_serdes.deserializer(buffer))

    return SerDes(
        str, _get_constraint(compiler_context, typ), _serialize, _deserialize, clk_type=clkbuiltins.VAR_STRING
    )


class SoaSerDes(Generic[T]):
    """SerDes for Struct-of-Arrays (SoA) types (both FixedSoa and VarSoa).

    SoA layout stores fields as separate contiguous arrays rather than array of structs.
    For example, FixedSoa<Point3f, 100> stores:
        x: [x0, x1, ..., x99]
        y: [y0, y1, ..., y99]
        z: [z0, z1, ..., z99]

    The Python representation is a dataclass with lists for each field.
    """

    def __init__(
        self,
        compiler_context: CompilerContext,
        schema_ir: schema.InstantiatedSchema,
        max_size: int,
        constraint: tachyon_reg.FieldConstraint,
        *,
        is_var: bool = False,
    ) -> None:
        """Create a SerDes for SoA types.

        Args:
            compiler_context: The compiler context.
            schema_ir: The instantiated schema for the element type.
            max_size: Maximum number of elements (size for FixedSoa, max_size for VarSoa).
            constraint: Field constraint for the SoA type.
            is_var: True for VarSoa, False for FixedSoa.
        """
        self.compiler_context = compiler_context
        self.schema_ir = schema_ir
        self.max_size = max_size
        self.constraint = constraint
        self.is_var = is_var

        self.layout_info = tachyon_reg.compute_soa_layout(
            compiler_context, schema_ir, max_size, include_size_field=is_var
        )
        assert self.constraint == tachyon_reg.FieldConstraint(
            size=self.layout_info.total_size, alignment=self.layout_info.max_alignment
        )  # Sanity check

        # Build field serializers: [(field_num, field_name, offset, element_serdes)]
        self.field_serializers: list[tuple[int, str, int, SerDes[Any]]] = []
        self.size_offset: int = 0
        self.size_field_size: int = 0
        self.size_serdes: SerDes[int] | None = None

        for field_layout in self.layout_info.field_layouts:
            if field_layout.field_num == tachyon_reg.SIZE_FIELD_NUM:
                self.size_offset = field_layout.offset
                self.size_field_size = field_layout.size
            else:
                field_def = schema_ir.fields[field_layout.field_num]
                element_serdes = serdes_for_type(compiler_context, field_def.type_info)
                self.field_serializers.append(
                    (
                        field_layout.field_num,
                        field_def.cur_name,
                        field_layout.offset,
                        element_serdes,
                    )
                )

        if is_var:
            size_type, _size_constraint = tachyon_reg.get_compact_size_type_info(max_size)
            self.size_serdes = cast("SerDes[int]", serdes_for_type(compiler_context, size_type))

        self.py_class = self._create_dataclass()

    def _create_dataclass(self) -> type[Any]:
        """Create a dataclass with field arrays for this SoA type."""
        class_name = f"{'Var' if self.is_var else 'Fixed'}Soa_{self.schema_ir.schema_name}_{self.max_size}"

        dataclass_fields: list[tuple[str, Any, Any] | tuple[str, Any]] = []
        for field_num, field_name, _offset, element_serdes in self.field_serializers:
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
                # FixedSoa: default to list of default values
                # Use _get_default_for_field to respect init_value declarations
                field_def = self.schema_ir.fields[field_num]
                has_default, element_default = _get_default_for_field(
                    self.compiler_context, self.schema_ir, field_def, element_type
                )

                if has_default:

                    def _make_default_factory(
                        default_val: object = element_default, size: int = self.max_size
                    ) -> Callable[[], list[object]]:
                        return lambda: [default_val for _ in range(size)]

                    dataclass_fields.append(
                        (
                            field_name,
                            list[element_type],
                            field(default_factory=_make_default_factory()),
                        )
                    )
                else:
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
            _, first_field_name, _, _ = self.field_serializers[0]
            actual_size = len(getattr(obj, first_field_name))
        else:
            actual_size = 0

        if self.is_var:
            if actual_size > self.max_size:
                msg = f"SoA size {actual_size} exceeds max_size {self.max_size}"
                raise ValueError(msg)
        elif actual_size != self.max_size:
            msg = f"FixedSoa field array has wrong size {actual_size}, expected {self.max_size}"
            raise ValueError(msg)

        for _field_num, field_name, offset, element_serdes in self.field_serializers:
            field_array = getattr(obj, field_name)
            if len(field_array) != actual_size:
                msg = f"All SoA field arrays must have same size, but {field_name} has {len(field_array)}, expected {actual_size}"
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
        for _field_num, field_name, offset, element_serdes in self.field_serializers:
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
            constraint=self.constraint,
            serializer=self.serialize,
            deserializer=self.deserialize,
            clk_type=clkbuiltins.VAR_SOA if self.is_var else clkbuiltins.FIXED_SOA,
        )


class OptionalSerDes(Generic[T]):
    """SerDes for Optional<T>."""

    def __init__(
        self,
        compiler_context: CompilerContext,
        constraint: tachyon_reg.FieldConstraint,
        underlying_type: typesys.TypeVal,
        underlying_serdes: SerDes[T],
    ) -> None:
        """Create a SerDes for array types."""
        self.constraint = constraint
        self.underlying_py_type = underlying_serdes.type_
        self.underlying_serializer = underlying_serdes.serializer
        self.underlying_deserializer = underlying_serdes.deserializer
        element_constraint = _get_constraint(compiler_context, underlying_type)
        self.underlying_slice = slice(0, element_constraint.size)
        self.bool_slice = slice(element_constraint.size, element_constraint.size + 1)
        self.bool_serdes: SerDes[bool] = cast("SerDes[bool]", serdes_for_type(compiler_context, clkbuiltins.BOOL))

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
            return self.underlying_deserializer(buffer[self.underlying_slice])
        return None

    def make_serdes(self) -> SerDes[T | None]:
        """Construct a SerDes for the registry."""
        return SerDes(
            type_=self.underlying_py_type | None,  # pyright: ignore[reportArgumentType] Type not known ahead of time
            constraint=self.constraint,
            serializer=self.serialize,
            deserializer=self.deserialize,
            clk_type=clkbuiltins.OPTIONAL,
        )


def _optional_factory(compiler_context: CompilerContext, typ: typesys.Instantiation) -> SerDes[Any] | None:
    """SerDes factory for Optional."""
    if typ.instantiates is not clkbuiltins.OPTIONAL:
        msg = f"Expected Optional but got {typ.instantiates}"
        raise RuntimeError(msg)
    value_type = typ.arguments["type"]
    if not isinstance(value_type, typesys.TypeVal):
        msg = f"Bad value type for type parameter: {value_type}"
        raise TypeError(msg)
    serdes = serdes_for_type(compiler_context, value_type)
    return OptionalSerDes(compiler_context, _get_constraint(compiler_context, typ), value_type, serdes).make_serdes()


def _fixed_soa_factory(compiler_context: CompilerContext, typ: typesys.Instantiation) -> SerDes[Any] | None:
    """SerDes factory for FixedSoa."""
    if typ.instantiates is not clkbuiltins.FIXED_SOA:
        msg = f"Expected FixedSoa but got {typ.instantiates}"
        raise RuntimeError(msg)

    element_type = typ.arguments["type"]
    size_arg = typ.arguments["size"]

    if not isinstance(element_type, schema.InstantiatedSchema):
        msg = f"FixedSoa element type must be a schema, got {element_type.value_key()}"
        raise TypeError(msg)

    if not isinstance(size_arg, primitive.DecimalValue):
        msg = f"Invalid size parameter: {size_arg}"
        raise TypeError(msg)

    size = int(size_arg.value)
    constraint = _get_constraint(compiler_context, typ)

    return SoaSerDes(compiler_context, element_type, size, constraint, is_var=False).make_serdes()


def _var_soa_factory(compiler_context: CompilerContext, typ: typesys.Instantiation) -> SerDes[list[Any]] | None:
    """SerDes factory for VarSoa."""
    if typ.instantiates is not clkbuiltins.VAR_SOA:
        msg = f"Expected VarSoa but got {typ.instantiates}"
        raise RuntimeError(msg)

    element_type = typ.arguments["type"]
    max_size_arg = typ.arguments["max_size"]

    if not isinstance(element_type, schema.InstantiatedSchema):
        msg = f"VarSoa element type must be a schema, got {element_type.value_key()}"
        raise TypeError(msg)

    if not isinstance(max_size_arg, primitive.DecimalValue):
        msg = f"Invalid max_size parameter: {max_size_arg}"
        raise TypeError(msg)

    max_size = int(max_size_arg.value)
    constraint = _get_constraint(compiler_context, typ)

    return SoaSerDes(compiler_context, element_type, max_size, constraint, is_var=True).make_serdes()


@dataclass
class SchemaSerDes(Generic[T]):
    """SerDes for a schema type."""

    compiler_context: CompilerContext = field(repr=False)
    clk_type: schema.InstantiatedSchema
    py_class: type[T] = field(repr=False)
    field_serdeses: list[tuple[str, slice, SerDes[Any]]]

    def serialize(self, obj: Any, buffer: memoryview) -> None:  # noqa: ANN401 type information is not known ahead of time.
        """Serializer for schema type."""
        for fld_name, slice_, serdes in self.field_serdeses:
            try:
                serdes.serializer(getattr(obj, fld_name), buffer[slice_])
            except Exception as e:
                msg = f"Object {type(obj)} failed to serialize {fld_name}"
                raise type(e)(msg) from e

    def deserialize(self, buffer: memoryview) -> Any:  # noqa: ANN401 type information is not known ahead of time.
        """Deserializer for type T."""
        return self.py_class(
            **{fld_name: serdes.deserializer(buffer[slice_]) for fld_name, slice_, serdes in self.field_serdeses}
        )

    @classmethod
    def _process_fields(
        cls,
        compiler_context: CompilerContext,
        schema_ir: schema.InstantiatedSchema,
        layout: tachyon_layout.Layout,
    ) -> tuple[list[tuple[str, slice, SerDes[Any]]], list[tuple[str, Any, Any] | tuple[str, Any]]]:
        """Process schema fields to create serializers and dataclass field definitions."""
        field_serdeses: list[tuple[str, slice, SerDes[Any]]] = []
        dataclass_fields: list[tuple[str, Any, Any] | tuple[str, Any]] = []

        for field_span in layout.field_number_order_fields():
            fld_def = schema_ir.fields[field_span.field_num]
            serdes = serdes_for_type(compiler_context, fld_def.type_info)
            field_serdeses.append(
                (fld_def.cur_name, slice(field_span.offset, field_span.offset + field_span.size), serdes)
            )

            # Handle init_value conversion
            has_default, default_value = _get_default_for_field(compiler_context, schema_ir, fld_def, serdes.type_)
            if has_default:
                if isinstance(default_value, list):

                    def _default_factory(default: list[Any] = default_value) -> list[Any]:
                        return default

                    dataclass_fields.append(
                        (
                            fld_def.cur_name,
                            serdes.type_,
                            field(default_factory=_default_factory),
                        )
                    )
                elif type(default_value).__hash__ is None:
                    # Python 3.12+ rejects mutable defaults (e.g., dataclass instances
                    # without frozen=True) in dataclass fields. Use default_factory to
                    # construct a fresh copy for each parent.
                    _default = default_value
                    dataclass_fields.append(
                        (fld_def.cur_name, serdes.type_, field(default_factory=lambda _d=_default: copy.deepcopy(_d)))
                    )
                else:
                    dataclass_fields.append((fld_def.cur_name, serdes.type_, field(default=default_value)))
            else:
                dataclass_fields.append((fld_def.cur_name, serdes.type_))

        return field_serdeses, dataclass_fields

    @classmethod
    def _add_tachyon_methods(  # noqa: C901 (we inherintly have a branch each method in the Tachyon interface)
        cls,
        py_class: type[Any],
        schema_serdes: SchemaSerDes[Any],
        schema_ir: schema.InstantiatedSchema,
        constraint: tachyon_reg.FieldConstraint,
    ) -> None:
        """Add Tachyon serialization methods to the dataclass."""

        def serialize_tachyon(self: Any, buffer: memoryview) -> None:  # noqa: ANN401 type information is not known ahead of time.
            schema_serdes.serialize(self, buffer)

        # pyrefly: ignore[invalid-decorator] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        @classmethod
        def deserialize_tachyon(_: type, buffer: memoryview) -> Any:  # noqa: ANN401 type information is not known ahead of time.
            return schema_serdes.deserialize(buffer)

        # pyrefly: ignore[invalid-decorator] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        @classmethod
        def get_tachyon_constraint(_: type) -> tachyon_reg.FieldConstraint:
            return constraint

        try:
            py_class_metadata = tachyon_meta.get_metadata(schema_serdes.compiler_context, schema_ir)
        except ValueError:
            py_class_metadata = None

        # pyrefly: ignore[invalid-decorator] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        @classmethod
        def get_tachyon_metadata_name(_: type) -> str | None:
            return schema_ir.value_key()

        # pyrefly: ignore[invalid-decorator] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        @classmethod
        def get_tachyon_metadata(_: type) -> tachyon_model.TachyonMetadata | None:
            return py_class_metadata

        # pyrefly: ignore[invalid-decorator] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        @classmethod
        def get_tachyon_module_name(_: type) -> str:
            return schema_ir.schema.module.module_id.repo

        # pyrefly: ignore[invalid-decorator] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        @classmethod
        def get_tachyon_source_file_name(_: type) -> str:
            return str(schema_ir.schema.module.module_id.get_base_path())

        # pyrefly: ignore[invalid-decorator] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        @classmethod
        def get_tachyon_class_name(_: type) -> str:
            return schema_ir.schema_name

        # pyrefly: ignore[invalid-decorator] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        @classmethod
        def get_tachyon_schema_ir(_: type) -> schema.InstantiatedSchema:
            return schema_ir

        # pyrefly: ignore[invalid-decorator] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        @classmethod
        def get_tachyon_compiler_context(_: type) -> CompilerContext:
            return schema_serdes.compiler_context

        py_class.serialize_tachyon = serialize_tachyon
        py_class.deserialize_tachyon = deserialize_tachyon
        py_class.get_tachyon_constraint = get_tachyon_constraint
        py_class.get_tachyon_metadata_name = get_tachyon_metadata_name
        py_class.get_tachyon_metadata = get_tachyon_metadata
        py_class.get_tachyon_schema_ir = get_tachyon_schema_ir
        py_class.get_tachyon_compiler_context = get_tachyon_compiler_context
        py_class.get_tachyon_module_name = get_tachyon_module_name
        py_class.get_tachyon_source_file_name = get_tachyon_source_file_name
        py_class.get_tachyon_class_name = get_tachyon_class_name

    @classmethod
    def make(
        cls: type[SchemaSerDes[T]], compiler_context: CompilerContext, schema_ir: schema.InstantiatedSchema
    ) -> SchemaSerDes[T]:
        """Construct a SerDes for the schema."""
        layout = tachyon_layout_reg.layout_for_type(compiler_context, schema_ir)
        constraint = tachyon_reg.constraint_for_type(compiler_context, schema_ir)
        if layout is None or constraint is None:
            msg = f"No known Tachyon representation for {schema_ir.value_key()}; did you forget an instantiation? {layout} {constraint}"
            raise ValueError(msg)

        field_serdeses, dataclass_fields = cls._process_fields(compiler_context, schema_ir, layout)

        py_class: Any = dataclasses.make_dataclass(
            schema_ir.schema_name, fields=dataclass_fields, kw_only=True, slots=True, eq=True
        )

        schema_serdes: SchemaSerDes[Any] = SchemaSerDes(
            compiler_context=compiler_context, clk_type=schema_ir, py_class=py_class, field_serdeses=field_serdeses
        )

        cls._add_tachyon_methods(py_class, schema_serdes, schema_ir, constraint)

        return schema_serdes

    def make_serdes(self) -> SerDes[T]:
        """Create a SerDes for this schema."""
        # The context isn't used at this point since we already have the constraint
        constraint = _get_constraint(self.compiler_context, self.clk_type)
        return SerDes(
            type_=self.py_class,
            constraint=constraint,
            serializer=self.serialize,
            deserializer=self.deserialize,
            clk_type=self.clk_type,
        )


# PLR0911 and C901 (too many return statements, too complex) suppressed because we need to handle all these cases
def _convert_init_value_to_python(  # noqa: PLR0911, C901
    compiler_context: CompilerContext,
    schema_ir: schema.InstantiatedSchema,
    init_value: typesys.Value,
    py_type: type[Any],
) -> float | str | bool | enum.Enum | None:
    """Convert a typesys.Value to a Python value of the appropriate type."""
    if isinstance(init_value, schema.ParameterRef):
        if not schema_ir.arguments or init_value.name not in schema_ir.arguments:
            msg = f"Unknown schema parameter {init_value.name} in field initial value"
            raise TypeError(msg)
        init_value = schema_ir.arguments[init_value.name]

    if isinstance(init_value, primitive.DecimalValue):
        if py_type is int:
            return int(init_value.value)
        if py_type is float:
            return float(init_value.value)
        msg = f"Cannot convert DecimalValue to {py_type}"
        raise TypeError(msg)

    if isinstance(init_value, clkenum.ValueRef):
        # For enums, we need to return the enum member
        # Create a default registry for initialization
        enum_serdes = serdes_for_type(compiler_context, init_value.value_def.enum.get_resolved())
        assert isinstance(enum_serdes.type_, type(enum.Enum))
        # pyrefly: ignore[no-matching-overload] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return enum_serdes.type_(init_value.value_def.integer_value)

    if isinstance(init_value, primitive.StringValue) and py_type is str:
        return init_value.value

    if init_value is clkbuiltins.TRUE_VALUE:
        return True
    if init_value is clkbuiltins.FALSE_VALUE:
        return False
    if isinstance(init_value, clkbuiltins.Nullopt):
        return None

    if isinstance(init_value, statement.ImmutableBinding):
        return _convert_init_value_to_python(compiler_context, schema_ir, init_value.get_resolved(), py_type)

    msg = f"Unsupported init_value type: {type(init_value)} for Python type {py_type}"
    raise TypeError(msg)


def _get_default_for_field(
    compiler_context: CompilerContext,
    schema_ir: schema.InstantiatedSchema,
    field_def: schema.InstantiatedFieldDef,
    py_type: type[T],
) -> tuple[bool, T]:
    """Get the default value for a field based on its init_value or type.

    Returns:
        A tuple of (has_default, default_value) where has_default is True if the field has a default value.
    """
    # If there's an explicit init_value, convert it
    if field_def.init_value is not None:
        result = _convert_init_value_to_python(compiler_context, schema_ir, field_def.init_value, py_type)
        assert isinstance(result, py_type)
        return (True, result)

    # No init_value or conversion failed, get default based on type
    return _get_default_for_type(compiler_context, field_def.type_info)


def _get_default_for_primitive_type(type_info: clkbuiltins.PrimitiveType) -> bool | float | None:
    """Get the default value for a primitive type."""
    if type_info == clkbuiltins.BOOL:
        return False
    if type_info in (
        clkbuiltins.BYTE,
        clkbuiltins.INT8,
        clkbuiltins.INT16,
        clkbuiltins.INT32,
        clkbuiltins.INT64,
        clkbuiltins.UINT8,
        clkbuiltins.UINT16,
        clkbuiltins.UINT32,
        clkbuiltins.UINT64,
        clkbuiltins.DURATION,
        clkbuiltins.SYNC_TIME,
    ):
        return 0
    if type_info in (clkbuiltins.FLOAT32, clkbuiltins.FLOAT64):
        return 0.0
    return None


def _get_default_for_enum(compiler_context: CompilerContext, type_info: clkenum.ResolvedEnum) -> enum.Enum:
    """Get the default value for an enum type."""
    enum_serdes = serdes_for_type(compiler_context, type_info)
    default_value_def = type_info.values[type_info.default_field_num]
    result = enum_serdes.type_(default_value_def.integer_value)
    assert isinstance(result, enum.Enum)
    return result


# We suppress PLR0911 here because there are simply a lot of cases that need to
# be handled.
def _get_default_for_container(  # noqa: PLR0911 (see above)
    compiler_context: CompilerContext, type_info: typesys.Instantiation
) -> tuple[bool, float | list[Any] | tensor_builtins.TensorData[Any] | str | None]:
    """Get the default value for a container type.

    Returns:
        A tuple of (has_default, default_value) where has_default is True if the type has a default value.
    """
    if type_info.instantiates is clkbuiltins.FIXED_ARRAY:
        element_type = type_info.arguments["type"]
        assert isinstance(element_type, typesys.TypeVal)
        size_arg = type_info.arguments["size"]
        assert isinstance(size_arg, primitive.DecimalValue)
        size = int(size_arg.value)
        _has_default, element_default = _get_default_for_type(compiler_context, element_type)
        return (True, [element_default] * size)

    if type_info.instantiates is clkbuiltins.BITSET:
        return (True, 0)

    if type_info.instantiates is tensor_builtins.TENSOR:
        element_type = tensor_builtins.get_tensor_element_type(type_info)
        shape = tensor_builtins.get_tensor_shape(type_info)
        strides = tensor_builtins.get_tensor_strides(type_info)
        size = reduce(operator.mul, shape)
        _, element_default = _get_default_for_type(compiler_context, element_type)
        return (True, tensor_builtins.TensorData(data=[element_default] * size, shape=shape, strides=strides))

    if type_info.instantiates is clkbuiltins.VAR_ARRAY:
        return (True, [])

    if type_info.instantiates is clkbuiltins.VAR_STRING:
        return (True, "")

    if type_info.instantiates is clkbuiltins.OPTIONAL:
        return (True, None)

    if type_info.instantiates is clkbuiltins.UUID:
        return (False, None)

    if type_info.instantiates in (clkbuiltins.FIXED_SOA, clkbuiltins.VAR_SOA):
        soa_serdes = serdes_for_type(compiler_context, type_info)
        return (True, soa_serdes.type_())

    msg = f"Unsupported container type {type_info.instantiates}"
    raise TypeError(msg)


def _get_default_for_schema(
    compiler_context: CompilerContext, type_info: schema.InstantiatedSchema
) -> tuple[bool, Tachyon[Any] | None]:
    """Get the default value for a schema type."""
    schema_serdes = serdes_for_type(compiler_context, type_info)
    # Create an instance with all defaults
    try:
        return (True, cast("Tachyon[Any]", schema_serdes.type_()))
    except TypeError:
        # This class has some fields without defaults
        return (False, None)


def _get_default_for_type(  # noqa: PLR0911 (need to handle all of the types)
    compiler_context: CompilerContext, type_info: typesys.TypeVal
) -> tuple[bool, Any]:
    """Generate a default value for a given type.

    Returns:
        A tuple of (has_default, default_value) where has_default is True if the type has a default value.
    """
    # Primitive(ish) types
    if isinstance(type_info, clkbuiltins.PrimitiveType):
        return (True, _get_default_for_primitive_type(type_info))

    if type_info is clkbuiltins.DURATION or type_info is clkbuiltins.SYNC_TIME:
        return (True, 0)

    if isinstance(type_info, typesys.Instantiation) and type_info.instantiates is clkbuiltins.UUID:
        return (True, uuid.UUID(int=0))

    if isinstance(type_info, strongtypes.StrongType):
        if not isinstance(type_info.typespec, clkbuiltins.PrimitiveType):
            msg = f"Cannot get default value for unresolved strong type {type_info}"
            raise RuntimeError(msg)  # noqa: TRY004 (TypeError inappropriate here)
        return (True, _get_default_for_primitive_type(type_info.typespec))

    # Enums - use default value
    if isinstance(type_info, clkenum.ResolvedEnum):
        return (True, _get_default_for_enum(compiler_context, type_info))

    # Container types
    if isinstance(type_info, typesys.Instantiation):
        return _get_default_for_container(compiler_context, type_info)

    # Schema types - recursively create default instance
    if isinstance(type_info, schema.InstantiatedSchema):
        return _get_default_for_schema(compiler_context, type_info)

    msg = f"Unsupported type {type_info}"
    raise TypeError(msg)


def get_schema_dataclass(
    compiler_context: CompilerContext, module: node.Module, schema_name: str
) -> tuple[Any, schema.Schema]:
    """Retrieve a SchemaSerDes for a schema defined in the given module."""
    schema_ir = module.inner_scope.lookup(schema_name)
    if schema_ir is None:
        msg = f"No such name {schema_name} in module"
        raise KeyError(msg)
    if not isinstance(schema_ir, schema.Schema):
        msg = f"Expected a Schema type but got {schema_ir}"
        raise TypeError(msg)
    return serdes_for_type(compiler_context, schema_ir).type_, schema_ir


def get_instantiation_dataclass(
    compiler_context: CompilerContext,
    module: node.Module,
    schema_name: str,
    **args: typesys.Value | float | ScopeLookup,
) -> tuple[Any, schema.Schema | typesys.Instantiation]:
    """Retrieve a SchemaSerDes for a schema instantiated in the given module."""
    schema_ir = module.inner_scope.lookup(schema_name)
    if schema_ir is None:
        msg = f"No such name {schema_name} in module"
        raise KeyError(msg)
    if not isinstance(schema_ir, schema.Schema):
        msg = f"Expected a Schema type but got {schema_ir}"

        raise TypeError(msg)

    def _make_value(val: typesys.Value | float | ScopeLookup) -> typesys.Value:
        if isinstance(val, typesys.Value):
            return val
        if isinstance(val, ScopeLookup):
            entity = val.module.inner_scope.lookup(val.name) if val.module else module.inner_scope.lookup(val.name)

            if entity is None:
                msg = f"Unable to lookup {val.name} in module scope."
                raise RuntimeError(msg)
            if not isinstance(entity, typesys.Value):
                msg = f"Entity {val.name} is not a typesys.Value"
                raise TypeError(msg)
            if val.arguments:
                val_args = {}
                for name, value in val.arguments.items():
                    val_args[name] = _make_value(value)
                assert isinstance(entity, schema.Schema)
                return typesys.Instantiation(type_info=clkbuiltins.TYPE_TYPE, instantiates=entity, arguments=val_args)
            return entity
        type_info = clkbuiltins.INT64 if isinstance(val, int) else clkbuiltins.FLOAT64
        return primitive.DecimalValue(type_info, Decimal(val))

    instantiation: schema.Schema | typesys.Instantiation
    arguments = {name: _make_value(val) for name, val in args.items()} if args else None
    instantiation = (
        typesys.Instantiation(type_info=clkbuiltins.TYPE_TYPE, instantiates=schema_ir, arguments=arguments)
        if arguments
        else schema_ir
    )
    return serdes_for_type(compiler_context, instantiation).type_, instantiation


def get_enum(
    compiler_context: CompilerContext, module: node.Module, enum_name: str
) -> tuple[Any, clkenum.ResolvedEnum]:
    """Retrieve an EnumSerDes for a schema defined in the given module."""
    enum_ir = module.inner_scope.lookup(enum_name)
    if enum_ir is None:
        msg = f"No such name {enum_name} in module"
        raise KeyError(msg)
    if not isinstance(enum_ir, clkenum.ClkEnum):
        msg = f"Expected a ClkEnum type but got {enum_ir}"
        raise TypeError(msg)
    result = serdes_for_type(compiler_context, enum_ir.get_resolved()).type_
    return result, enum_ir.get_resolved()


def get_schema_field_serdes(
    compiler_context: CompilerContext, schema_ir: schema.Schema, field: int | str
) -> tuple[Any, typesys.TypeVal]:
    """Get a Python type and Clockwork type for a field of a schema."""
    if isinstance(field, int):
        fld_def = schema_ir.fields[field]
    else:
        for fld_def in schema_ir.fields.values():
            if fld_def.cur_name == field:
                break
        else:
            msg = f"No such field {field} in {schema_ir.name}"
            raise KeyError(msg)
    assert isinstance(fld_def.type_info, typesys.TypeVal)
    return serdes_for_type(compiler_context, fld_def.type_info).type_, fld_def.type_info


# Module-level SerDes instances for built-in types
# These will be shared across all registry instances to avoid duplication
BOOL_SERDES: Final = SerDes(
    type_=bool,
    constraint=tachyon_reg.BOOL_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<?", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<?", buffer, 0)[0],
    clk_type=clkbuiltins.BOOL,
)

BYTE_SERDES: Final = SerDes(
    type_=int,
    constraint=tachyon_reg.BYTE_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<B", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<B", buffer, 0)[0],
    clk_type=clkbuiltins.BYTE,
)

FLOAT32_SERDES: Final = SerDes(
    type_=float,
    constraint=tachyon_reg.FLOAT32_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<f", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<f", buffer, 0)[0],
    clk_type=clkbuiltins.FLOAT32,
)

FLOAT64_SERDES: Final = SerDes(
    type_=float,
    constraint=tachyon_reg.FLOAT64_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<d", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<d", buffer, 0)[0],
    clk_type=clkbuiltins.FLOAT64,
)

INT64_SERDES: Final = SerDes(
    type_=int,
    constraint=tachyon_reg.INT64_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<q", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<q", buffer, 0)[0],
    clk_type=clkbuiltins.INT64,
)

INT32_SERDES: Final = SerDes(
    type_=int,
    constraint=tachyon_reg.INT32_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<i", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<i", buffer, 0)[0],
    clk_type=clkbuiltins.INT32,
)

INT16_SERDES: Final = SerDes(
    type_=int,
    constraint=tachyon_reg.INT16_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<h", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<h", buffer, 0)[0],
    clk_type=clkbuiltins.INT16,
)

INT8_SERDES: Final = SerDes(
    type_=int,
    constraint=tachyon_reg.INT8_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<b", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<b", buffer, 0)[0],
    clk_type=clkbuiltins.INT8,
)

UINT64_SERDES: Final = SerDes(
    type_=int,
    constraint=tachyon_reg.UINT64_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<Q", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<Q", buffer, 0)[0],
    clk_type=clkbuiltins.UINT64,
)

UINT32_SERDES: Final = SerDes(
    type_=int,
    constraint=tachyon_reg.UINT32_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<I", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<I", buffer, 0)[0],
    clk_type=clkbuiltins.UINT32,
)

UINT16_SERDES: Final = SerDes(
    type_=int,
    constraint=tachyon_reg.UINT16_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<H", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<H", buffer, 0)[0],
    clk_type=clkbuiltins.UINT16,
)

UINT8_SERDES: Final = SerDes(
    type_=int,
    constraint=tachyon_reg.UINT8_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<B", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<B", buffer, 0)[0],
    clk_type=clkbuiltins.UINT8,
)

DURATION_SERDES: Final = SerDes(
    type_=int,
    constraint=tachyon_reg.DURATION_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<q", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<q", buffer, 0)[0],
    clk_type=clkbuiltins.DURATION,
)

SYNC_TIME_SERDES: Final = SerDes(
    type_=int,
    constraint=tachyon_reg.SYNC_TIME_CONSTRAINT,
    serializer=lambda obj, buffer: struct.pack_into("<q", buffer, 0, obj),
    deserializer=lambda buffer: struct.unpack_from("<q", buffer, 0)[0],
    clk_type=clkbuiltins.SYNC_TIME,
)

# Dictionary mapping Clockwork types to their SerDes instances
BUILTIN_SERDES: Final[dict[str, tuple[SerDes[Any], str | None]]] = {
    clkbuiltins.BOOL.value_key(): (BOOL_SERDES, "builtins"),
    clkbuiltins.BYTE.value_key(): (BYTE_SERDES, "builtins"),
    clkbuiltins.FLOAT32.value_key(): (FLOAT32_SERDES, "builtins"),
    clkbuiltins.FLOAT64.value_key(): (FLOAT64_SERDES, "builtins"),
    clkbuiltins.INT64.value_key(): (INT64_SERDES, "builtins"),
    clkbuiltins.INT32.value_key(): (INT32_SERDES, "builtins"),
    clkbuiltins.INT16.value_key(): (INT16_SERDES, "builtins"),
    clkbuiltins.INT8.value_key(): (INT8_SERDES, "builtins"),
    clkbuiltins.UINT64.value_key(): (UINT64_SERDES, "builtins"),
    clkbuiltins.UINT32.value_key(): (UINT32_SERDES, "builtins"),
    clkbuiltins.UINT16.value_key(): (UINT16_SERDES, "builtins"),
    clkbuiltins.UINT8.value_key(): (UINT8_SERDES, "builtins"),
    clkbuiltins.DURATION.value_key(): (DURATION_SERDES, "builtins"),
    clkbuiltins.SYNC_TIME.value_key(): (SYNC_TIME_SERDES, "builtins"),
}


@dataclass
class FieldUpgrade:
    """Information about how to upgrade a single field.

    Attributes:
        old_name: Name of the field in the old schema version
        new_name: Name of the field in the new schema version
        upgrader: Function to transform the field value during upgrade.
                 If None, the field value is copied without transformation.
    """

    old_name: str
    new_name: str
    upgrader: Callable[[Any], Any] | None = None


@dataclass
class SchemaUpgrader:
    """Contains the logic for upgrading instances of a schema from one version to another."""

    new_schema: schema.InstantiatedSchema
    new_serdes: SerDes[Any]
    old_fqn: str
    old_version: int
    field_upgrades: list[FieldUpgrade]

    def upgrade(self, old_instance: Tachyon[Any]) -> Tachyon[Any]:
        """Upgrade a single instance using the pre-computed upgrade plan."""
        new_values = {}
        for upgrade in self.field_upgrades:
            old_value = getattr(old_instance, upgrade.old_name)
            if upgrade.upgrader is not None:
                new_values[upgrade.new_name] = upgrade.upgrader(old_value)
            else:
                new_values[upgrade.new_name] = old_value
        return cast("Tachyon[Any]", self.new_serdes.type_(**new_values))

    def upgrade_is_required(self) -> bool:
        """Check if the upgrade is required."""
        return len(self.field_upgrades) > 0


@dataclass
class UpgradePlan:
    """Contains all information needed to upgrade instances of a schema and its nested schemas.

    The plan traces field evolution solely through field numbers, not by field names.
    Field numbers uniquely identify fields through schema history, and the schema history
    records field transformations using these numbers.
    """

    schema_upgraders: dict[str, SchemaUpgrader | None]  # Keyed by old schema FQN
    py_type: Tachyon[Any] | None  # May be None if no upgrade is required
    old_fqn: str

    def upgrade(self, old_instance: Tachyon[Any]) -> Tachyon[Any]:
        """Upgrade a single instance using the pre-computed upgrade plan."""
        upgrader = self.schema_upgraders[self.old_fqn]
        if upgrader is None or not upgrader.upgrade_is_required():
            return old_instance
        return upgrader.upgrade(old_instance)

    def needs_upgrade(self) -> bool:
        """Check if the upgrade is required."""
        upgrader = self.schema_upgraders[self.old_fqn]
        return upgrader is not None and upgrader.upgrade_is_required()


def _create_type_converter(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Create a converter function between two types if needed."""
    # Try each conversion handler in sequence
    handlers = [
        _handle_primitive_to_primitive_conversion,
        _handle_primitive_to_varstring_conversion,
        _handle_primitive_to_enum_conversion,
        _handle_enum_to_enum_conversion,
        _handle_varstring_to_varstring_conversion,
        _handle_varstring_to_optional_conversion,
        _handle_optional_to_optional_conversion,
        _handle_schema_to_schema_conversion,
        _handle_type_to_container_conversion,
        _handle_array_to_array_conversion,
        _handle_soa_to_soa_conversion,
        _handle_array_to_soa_conversion,
        _handle_soa_to_array_conversion,
        _handle_optional_to_array_conversion,
        _handle_optional_to_soa_conversion,
        _handle_optional_to_varstring_conversion,
        _handle_array_to_optional_conversion,
        _handle_soa_to_optional_conversion,
        _handle_array_to_string_conversion,
        _handle_string_to_array_conversion,
        _handle_uuid_to_uuid_conversion,
        _handle_uuid_to_varstring_conversion,
    ]

    if isinstance(new_type_info, strongtypes.StrongType):
        # Strong types are treated as their underlying type
        new_type_info = new_type_info.get_underlying_type()

    old_type = old_types[old_type_id]
    if isinstance(old_type, tachyon_model.StrongType):
        # Strong types are treated as their underlying type
        old_type_id = old_type.underlying_type_id
        old_type = old_types[old_type_id]

    for handler in handlers:
        handled, converter = handler(
            compiler_context=compiler_context,
            old_type_id=old_type_id,
            old_types=old_types,
            new_type_info=new_type_info,
            upgraders=upgraders,
            error_node=error_node,
        )
        if handled:
            return True, converter
    return False, None


def _handle_uuid_to_uuid_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,  # noqa: ARG001 (need to match handler signature)
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],  # noqa: ARG001 (need to match handler signature)
    error_node: node.CstNode[Any],  # noqa: ARG001 (need to match handler signature)
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion between primitive types (integers, floats, duration, sync_time)."""
    old_type = old_types[old_type_id]
    if (
        isinstance(old_type, tachyon_model.BuiltInType)
        and old_type.fqn == ".Uuid"
        and isinstance(new_type_info, typesys.Instantiation)
        and new_type_info.instantiates is clkbuiltins.UUID
    ):
        return True, None

    return False, None


_UUID_STRING_LENGTH: Final = 36


def _handle_uuid_to_varstring_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,  # noqa: ARG001 (need to match handler signature)
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],  # noqa: ARG001 (need to match handler signature)
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion from UUID to VarString.

    A UUID is formatted as a 36-character dash-separated hex string
    (e.g. "550e8400-e29b-41d4-a716-446655440000"), so the destination
    VarString must have max_size > 36 because VarString reserves one
    byte for the null terminator.
    """
    old_type = old_types[old_type_id]
    if not (
        isinstance(old_type, tachyon_model.BuiltInType)
        and old_type.fqn == ".Uuid"
        and isinstance(new_type_info, typesys.Instantiation)
        and new_type_info.instantiates is clkbuiltins.VAR_STRING
    ):
        return False, None

    max_size = new_type_info.arguments["max_size"]
    if not isinstance(max_size, primitive.DecimalLiteral) or int(max_size.value) <= _UUID_STRING_LENGTH:
        msg = error_node.append_error_line(
            f"VarString max_size must be greater than {_UUID_STRING_LENGTH} to hold a UUID string"
        )
        raise ValueError(msg)

    def uuid_to_string(val: uuid.UUID) -> str:
        return str(val)

    return True, uuid_to_string


def _create_int_to_float_converter() -> Callable[[int], float]:
    """Create a converter function from int to float.

    Returns:
        A function that converts int to float.
    """

    def converter(old_value: int) -> float:
        return float(old_value)

    return converter


def _create_float_to_int_converter() -> Callable[[float], int]:
    """Create a converter function from float to int.

    Returns:
        A function that converts float to int.
    """

    def converter(old_value: float) -> int:
        return int(old_value)

    return converter


# PLR0913 (too many params) is mitigated by kwonly arg.
# PLR0911 (too many returns) is suppressed because we need to handle all these cases.
def _handle_primitive_to_primitive_conversion(  # noqa: PLR0913, PLR0911 (see above)
    *,
    compiler_context: CompilerContext,  # noqa: ARG001 (need to match handler signature)
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],  # noqa: ARG001 (need to match handler signature)
    error_node: node.CstNode[Any],  # noqa: ARG001 (need to match handler signature)
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion between primitive types (integers, floats, duration, sync_time)."""
    old_type = old_types[old_type_id]
    if not isinstance(old_type, tachyon_model.BuiltInType):
        return False, None

    if isinstance(new_type_info, typesys.TypeDef) and old_type.fqn == new_type_info.fqn:
        return True, None

    if old_type.fqn in (
        clkbuiltins.INT8.fqn,
        clkbuiltins.INT16.fqn,
        clkbuiltins.INT32.fqn,
        clkbuiltins.INT64.fqn,
        clkbuiltins.UINT8.fqn,
        clkbuiltins.UINT16.fqn,
        clkbuiltins.UINT32.fqn,
        clkbuiltins.UINT64.fqn,
    ) and isinstance(new_type_info, clkbuiltins.IntegerPrimitiveType):
        return True, None

    if old_type.fqn in (
        clkbuiltins.INT8.fqn,
        clkbuiltins.INT16.fqn,
        clkbuiltins.INT32.fqn,
        clkbuiltins.INT64.fqn,
        clkbuiltins.UINT8.fqn,
        clkbuiltins.UINT16.fqn,
        clkbuiltins.UINT32.fqn,
        clkbuiltins.UINT64.fqn,
    ) and (new_type_info in (clkbuiltins.FLOAT32, clkbuiltins.FLOAT64)):
        return True, _create_int_to_float_converter()

    if old_type.fqn in (clkbuiltins.FLOAT32.fqn, clkbuiltins.FLOAT64.fqn) and (
        new_type_info in (clkbuiltins.FLOAT32, clkbuiltins.FLOAT64)
    ):
        return True, None

    if old_type.fqn in (clkbuiltins.FLOAT32.fqn, clkbuiltins.FLOAT64.fqn) and isinstance(
        new_type_info, clkbuiltins.IntegerPrimitiveType
    ):
        return True, _create_float_to_int_converter()

    # Integer to Duration/SyncTime conversions
    if old_type.fqn in (
        clkbuiltins.INT8.fqn,
        clkbuiltins.INT16.fqn,
        clkbuiltins.INT32.fqn,
        clkbuiltins.INT64.fqn,
        clkbuiltins.UINT8.fqn,
        clkbuiltins.UINT16.fqn,
        clkbuiltins.UINT32.fqn,
        clkbuiltins.UINT64.fqn,
    ) and new_type_info in (clkbuiltins.DURATION, clkbuiltins.SYNC_TIME):
        return True, None

    return False, None


def _handle_primitive_to_varstring_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,  # noqa: ARG001 (need to match handler signature)
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],  # noqa: ARG001 (need to match handler signature)
    error_node: node.CstNode[Any],  # noqa: ARG001 (need to match handler signature)
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion from primitive types to strings."""
    old_type = old_types[old_type_id]
    if not (
        isinstance(old_type, tachyon_model.BuiltInType)
        and old_type.fqn in (clkbuiltins.INT8.fqn, clkbuiltins.UINT8.fqn, clkbuiltins.BYTE.fqn)
        and isinstance(new_type_info, typesys.Instantiation)
        and new_type_info.instantiates is clkbuiltins.VAR_STRING
    ):
        return False, None

    def primitive_converter(val: int) -> str:
        return bytes([val]).decode("utf-8")

    return True, primitive_converter


def _handle_primitive_to_enum_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],  # noqa: ARG001 (need to match handler signature)
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion from primitive integer to enum types."""
    old_type = old_types[old_type_id]
    if old_type.fqn not in (
        clkbuiltins.INT8.fqn,
        clkbuiltins.INT16.fqn,
        clkbuiltins.INT32.fqn,
        clkbuiltins.INT64.fqn,
        clkbuiltins.UINT8.fqn,
        clkbuiltins.UINT16.fqn,
        clkbuiltins.UINT32.fqn,
        clkbuiltins.UINT64.fqn,
    ):
        return False, None

    if not isinstance(new_type_info, clkenum.ResolvedEnum):
        return False, None

    try:
        return True, _create_primitive_to_enum_converter(compiler_context, new_type_info, error_node)
    except ValueError as e:
        msg = error_node.append_error_line(f"Failed to convert {old_type.fqn} to enum: {e}")
        raise ValueError(msg) from e


def _create_primitive_to_enum_converter(
    compiler_context: CompilerContext, new_enum: clkenum.ResolvedEnum, error_node: node.CstNode[Any]
) -> Callable[[int], enum.Enum]:
    """Create a converter function between two versions of the same enum.

    Args:
        compiler_context: Compiler context containing serializers registry
        new_enum: The target enum version
        error_node: CST node used to generate error messages

    Returns:
        A function that converts enum values from old version to new version
    """
    new_enum_serdes = serdes_for_type(compiler_context, new_enum)

    conversion_map: dict[int, enum.Enum] = {}

    for new_value_def in new_enum.values.values():
        new_py_value = new_enum_serdes.type_(new_value_def.integer_value)
        conversion_map[new_value_def.integer_value] = new_py_value

    def converter(old_value: int, error_node: node.CstNode[Any] = error_node) -> enum.Enum:
        if old_value not in conversion_map:
            msg = error_node.append_error_line(f"Invalid enum value {old_value}")
            raise ValueError(msg)
        return conversion_map[old_value]

    return converter


def _handle_enum_to_enum_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],  # noqa: ARG001 (need to match handler signature)
    error_node: node.CstNode[Any],  # noqa: ARG001 (need to match handler signature)
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion between enum types."""
    old_type = old_types[old_type_id]
    if not isinstance(old_type, tachyon_model.ClkEnumType):
        return False, None

    if not isinstance(new_type_info, clkenum.ResolvedEnum):
        return False, None

    if old_type.enum_uuid != new_type_info.uuid:
        return False, None

    try:
        return True, _create_enum_to_enum_converter(compiler_context, old_type, new_type_info)
    except ValueError as e:
        msg = f"Failed to convert enum {old_type.fqn} from version {old_type.version} to {new_type_info.cur_version()}: {e}"
        raise ValueError(msg) from e


def _create_enum_to_enum_converter(
    compiler_context: CompilerContext, old_type: tachyon_model.ClkEnumType, new_enum: clkenum.ResolvedEnum
) -> Callable[[enum.Enum], enum.Enum]:
    """Create a converter function between two versions of the same enum.

    Args:
        compiler_context: Compiler context containing serializers registry
        old_type: The source enum schema
        new_enum: The target enum definition

    Returns:
        A function that converts enum values from old version to new version
    """
    new_enum.validate_version(old_type.version)
    new_enum_serdes = serdes_for_type(compiler_context, new_enum)

    conversion_map: dict[int, enum.Enum] = {}
    removed_values: set[int] = set()

    for old_value in old_type.values:
        final_value_num, _ = _trace_enum_value_forward(new_enum.history, old_value.num)
        if final_value_num is None:
            removed_values.add(old_value.value)
        else:
            if final_value_num not in new_enum.values:
                msg = new_enum.append_error_line(
                    f"Value #{final_value_num} not found in enum {new_enum.name} nor is it removed"
                )
                raise ValueError(msg)

            new_value_def = new_enum.values[final_value_num]
            new_py_value = new_enum_serdes.type_(new_value_def.integer_value)
            conversion_map[old_value.value] = new_py_value

    def converter(old_value: enum.Enum) -> enum.Enum:
        if old_value.value in removed_values:
            msg = (
                f"Cannot upgrade enum value {old_value} (value={old_value.value}) as it was removed in a later version"
            )
            raise ValueError(msg)
        if old_value.value not in conversion_map:
            msg = f"Invalid enum value {old_value} (value={old_value.value}) in old data"
            raise ValueError(msg)
        return conversion_map[old_value.value]

    return converter


def _trace_enum_value_forward(
    enum_history: clkenum.EnumHistory,
    field_num: int,
) -> tuple[int | None, list[int]]:
    """Trace an enum value's evolution forward through enum history.

    Args:
        enum_history: Enum history containing value transformation information
        field_num: Starting field number to trace

    Returns:
        Tuple of (final_field_number, path) where:
        - final_field_number is the current field number this value evolved into, or None if removed
        - path is the list of intermediate field numbers in the evolution chain (including the start)
    """
    path = [field_num]
    current = field_num

    while current in enum_history.legacy_became:
        current = enum_history.legacy_became[current]
        path.append(current)

    if current in enum_history.removed:
        return (None, path)

    return (current, path)


def _handle_type_to_container_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion from any type T to a container of compatible type U.

    This is a general handler for converting:
    - T -> Array<U>
    - T -> Optional<U>
    where T and U are compatible types that can be converted.
    """
    if not isinstance(new_type_info, typesys.Instantiation):
        return False, None

    if new_type_info.instantiates not in (clkbuiltins.VAR_ARRAY, clkbuiltins.FIXED_ARRAY, clkbuiltins.OPTIONAL):
        return False, None

    inner_type = new_type_info.arguments.get("type")
    if not isinstance(inner_type, typesys.TypeVal):
        msg = error_node.append_error_line(f"Invalid container type arguments: {new_type_info.value_key()}")
        raise TypeError(msg)

    can_convert, element_converter = _create_type_converter(
        compiler_context=compiler_context,
        old_type_id=old_type_id,
        old_types=old_types,
        new_type_info=inner_type,
        upgraders=upgraders,
        error_node=error_node,
    )
    if not can_convert:
        return False, None

    if new_type_info.instantiates in (clkbuiltins.VAR_ARRAY, clkbuiltins.FIXED_ARRAY):

        def to_array(val: object) -> list[object]:
            if element_converter is not None:
                val = element_converter(val)
            return [val]

        return True, to_array

    if new_type_info.instantiates is clkbuiltins.OPTIONAL:
        return True, element_converter

    return False, None


def _handle_schema_to_schema_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion between schema types."""
    del error_node  # Unused, but needs to have the same signature as the rest of the handlers.

    old_type = old_types[old_type_id]
    if not (isinstance(old_type, tachyon_model.SchemaType) and isinstance(new_type_info, schema.InstantiatedSchema)):
        return False, None
    process_schema(
        compiler_context=compiler_context,
        schema_ir=new_type_info,
        type_id=old_type_id,
        old_types=old_types,
        upgraders=upgraders,
    )
    upgrader = upgraders[old_type.fqn]
    if upgrader is not None:
        return True, upgrader.upgrade
    return True, None


def _handle_array_to_array_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion between array types (VarArray or FixedArray)."""
    old_type = old_types[old_type_id]
    if not (
        isinstance(old_type, tachyon_model.BuiltInType)
        and old_type.fqn in (clkbuiltins.VAR_ARRAY.fqn, clkbuiltins.FIXED_ARRAY.fqn)
        and isinstance(new_type_info, typesys.Instantiation)
        and new_type_info.instantiates in (clkbuiltins.VAR_ARRAY, clkbuiltins.FIXED_ARRAY)
    ):
        return False, None

    # Get element types for both arrays
    if len(old_type.arguments) < 1 or not isinstance(old_type.arguments[0], int):
        msg = error_node.append_error_line(f"Invalid array type arguments: {old_type}")
        raise ValueError(msg)

    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    old_element_type_id = int(old_type.arguments[0])

    new_element_type = new_type_info.arguments["type"]
    if not isinstance(new_element_type, typesys.TypeVal):
        msg = error_node.append_error_line(f"Invalid array type arguments: {new_type_info}")
        raise TypeError(msg)

    if old_type.fqn == clkbuiltins.FIXED_ARRAY.fqn and new_type_info.instantiates is clkbuiltins.FIXED_ARRAY:
        old_array_size = int(old_type.arguments[1])
        new_size_arg = new_type_info.arguments["size"]
        assert isinstance(new_size_arg, primitive.DecimalLiteral)
        new_array_size = int(new_size_arg.value)
        if old_array_size != new_array_size:
            return False, None

    can_convert, element_converter = _create_type_converter(
        compiler_context=compiler_context,
        old_type_id=old_element_type_id,
        old_types=old_types,
        new_type_info=new_element_type,
        upgraders=upgraders,
        error_node=error_node,
    )
    if not can_convert:
        return False, None

    if element_converter is not None:
        converter = element_converter  # Helps mypy figure out that it's not None

        def array_converter(val: list[Any]) -> list[Any]:
            return [converter(item) for item in val]

        return True, array_converter

    return True, None


def _handle_optional_to_array_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion from Optional[T] to array types."""
    old_type = old_types[old_type_id]
    if not (
        isinstance(old_type, tachyon_model.BuiltInType)
        and old_type.fqn == clkbuiltins.OPTIONAL.fqn
        and (
            isinstance(new_type_info, typesys.Instantiation)
            and new_type_info.instantiates in (clkbuiltins.VAR_ARRAY, clkbuiltins.FIXED_ARRAY)
        )
    ):
        return False, None

    if len(old_type.arguments) != 1 or not isinstance(old_type.arguments[0], int):
        msg = error_node.append_error_line(f"Invalid Optional type arguments: {old_type}")
        raise ValueError(msg)

    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    inner_old_type_id = int(old_type.arguments[0])

    new_element_type = new_type_info.arguments["type"]
    if not isinstance(new_element_type, typesys.TypeVal):
        msg = error_node.append_error_line(f"Invalid array type arguments: {new_type_info}")
        raise TypeError(msg)

    can_convert, element_converter = _create_type_converter(
        compiler_context=compiler_context,
        old_type_id=inner_old_type_id,
        old_types=old_types,
        new_type_info=new_element_type,
        upgraders=upgraders,
        error_node=error_node,
    )
    if not can_convert:
        return False, None

    def optional_to_array(val: T | None) -> list[T]:
        if val is None:
            return []  # None becomes an empty list

        converted = val if element_converter is None else element_converter(val)
        return [converted]  # Single value becomes a one-element list

    return True, optional_to_array


def _handle_optional_to_varstring_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,  # noqa: ARG001 (need to match handler signature)
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],  # noqa: ARG001 (need to match handler signature)
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion from Optional to VarString."""
    old_type = old_types[old_type_id]
    if not (
        isinstance(old_type, tachyon_model.BuiltInType)
        and old_type.fqn == clkbuiltins.OPTIONAL.fqn
        and isinstance(new_type_info, typesys.Instantiation)
        and new_type_info.instantiates is clkbuiltins.VAR_STRING
    ):
        return False, None

    # Get element types for the source arrays
    if len(old_type.arguments) < 1 or not isinstance(old_type.arguments[0], int):
        msg = error_node.append_error_line(f"Invalid optional type arguments: {old_type}")
        raise ValueError(msg)

    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    old_element_type_id = int(old_type.arguments[0])
    old_element_type = old_types[old_element_type_id]
    if not isinstance(old_element_type, tachyon_model.BuiltInType):
        return False, None

    if old_element_type.fqn not in (clkbuiltins.INT8.fqn, clkbuiltins.UINT8.fqn, clkbuiltins.BYTE.fqn):
        return False, None

    def optional_converter(val: int | None) -> str:
        if val is None:
            return ""
        return bytes([val]).decode("utf-8")

    return True, optional_converter


def _handle_array_to_optional_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion from array types to Optional[T]."""
    old_type = old_types[old_type_id]
    if not (
        isinstance(old_type, tachyon_model.BuiltInType)
        and old_type.fqn in (clkbuiltins.VAR_ARRAY.fqn, clkbuiltins.FIXED_ARRAY.fqn)
        and isinstance(new_type_info, typesys.Instantiation)
        and new_type_info.instantiates is clkbuiltins.OPTIONAL
    ):
        return False, None

    if len(old_type.arguments) < 1 or not isinstance(old_type.arguments[0], int):
        msg = error_node.append_error_line(f"Invalid array type arguments: {old_type}")
        raise ValueError(msg)

    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    old_element_type_id = int(old_type.arguments[0])

    new_inner_type = new_type_info.arguments["type"]
    if not isinstance(new_inner_type, typesys.TypeVal):
        msg = error_node.append_error_line(f"Invalid Optional type arguments: {new_type_info}")
        raise TypeError(msg)

    can_convert, element_converter = _create_type_converter(
        compiler_context=compiler_context,
        old_type_id=old_element_type_id,
        old_types=old_types,
        new_type_info=new_inner_type,
        upgraders=upgraders,
        error_node=error_node,
    )
    if not can_convert:
        return False, None

    def array_to_optional(val: list[T]) -> T | None:
        if not val:
            return None  # Empty list becomes None

        if len(val) > 1:
            msg = error_node.append_error_line(f"Cannot convert multi-element list to Optional: {val}")
            raise ValueError(msg)

        if element_converter is None:
            return val[0]
        return cast("T", element_converter(val[0]))

    return True, array_to_optional


def _handle_array_to_string_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,  # noqa: ARG001 (need to match handler signature)
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],  # noqa: ARG001  (need to match handler signature)
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion from byte array to string."""
    old_type = old_types[old_type_id]
    if not (
        isinstance(new_type_info, typesys.Instantiation)
        and new_type_info.instantiates is clkbuiltins.VAR_STRING
        and isinstance(old_type, tachyon_model.BuiltInType)
        and (old_type.fqn in (clkbuiltins.VAR_ARRAY.fqn, clkbuiltins.FIXED_ARRAY.fqn))
    ):
        return False, None

    if len(old_type.arguments) < 1 or not isinstance(old_type.arguments[0], int):
        msg = error_node.append_error_line(f"Invalid array type arguments: {old_type}")
        raise ValueError(msg)

    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    element_type_id = int(old_type.arguments[0])
    element_type = old_types[element_type_id]

    if isinstance(element_type, tachyon_model.BuiltInType) and element_type.fqn in (
        clkbuiltins.BYTE.fqn,
        clkbuiltins.INT8.fqn,
        clkbuiltins.UINT8.fqn,
    ):

        def bytes_to_string(val: list[int]) -> str:
            if not val:
                return ""
            # Convert list of bytes to string using UTF-8
            return bytes(val).decode("utf-8")

        return True, bytes_to_string

    return False, None


def _handle_string_to_array_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,  # noqa: ARG001 (need to match handler signature)
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],  # noqa: ARG001 (need to match handler signature)
    error_node: node.CstNode[Any],  # noqa: ARG001 (need to match handler signature)
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion from string to byte array."""
    old_type = old_types[old_type_id]
    if not (
        isinstance(old_type, tachyon_model.BuiltInType)
        and old_type.fqn == clkbuiltins.VAR_STRING.fqn
        and isinstance(new_type_info, typesys.Instantiation)
        and (
            new_type_info.instantiates is clkbuiltins.VAR_ARRAY or new_type_info.instantiates is clkbuiltins.FIXED_ARRAY
        )
    ):
        return False, None

    element_type_arg = new_type_info.arguments["type"]
    if isinstance(element_type_arg, clkbuiltins.PrimitiveType) and element_type_arg in [
        clkbuiltins.BYTE,
        clkbuiltins.UINT8,
        clkbuiltins.INT8,
    ]:

        def string_to_bytes(val: str) -> list[int]:
            if not val:
                return []
            # Convert string to list of bytes using UTF-8
            return list(val.encode("utf-8"))

        return True, string_to_bytes

    return False, None


def _build_soa_field_converters(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    old_element_type: tachyon_model.SchemaType,
    new_element_schema: schema.InstantiatedSchema,
    old_types: Sequence[tachyon_model.ClkType],
    compiler_context: CompilerContext,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> list[tuple[str, str, Callable[[Any], Any] | None]]:
    """Build field converters for SoA-to-SoA conversion.

    Returns:
        List of (old_name, new_name, converter) tuples.
    """
    field_converters: list[tuple[str, str, Callable[[Any], Any] | None]] = []

    for old_field in old_element_type.fields:
        final_field_num, _ = _trace_field_forward(new_element_schema.history, old_field.num)

        if final_field_num is None:
            continue

        if final_field_num not in new_element_schema.fields:
            msg = error_node.append_error_line(
                f"Field #{final_field_num} not found in schema {new_element_schema.schema_name}"
            )
            raise ValueError(msg)

        new_field = new_element_schema.fields[final_field_num]

        can_convert, field_converter = _create_type_converter(
            compiler_context=compiler_context,
            old_type_id=old_field.type_id,
            old_types=old_types,
            new_type_info=new_field.type_info,
            upgraders=upgraders,
            error_node=error_node,
        )

        if not can_convert:
            msg = error_node.append_error_line(
                f"Cannot convert field {old_field.name} from type_id {old_field.type_id} to {new_field.type_info}"
            )
            raise ValueError(msg)

        field_converters.append((old_field.name, new_field.cur_name, field_converter))

    return field_converters


def _get_soa_new_field_defaults(
    new_element_schema: schema.InstantiatedSchema,
    field_converters: list[tuple[str, str, Callable[[Any], Any] | None]],
    compiler_context: CompilerContext,
) -> dict[str, Any]:
    """Get default values for newly added fields in SoA conversion.

    Returns:
        Dictionary mapping field names to their default values.
    """
    new_field_defaults: dict[str, Any] = {}

    for new_field in new_element_schema.fields.values():
        if not any(new_name == new_field.cur_name for _, new_name, _ in field_converters):
            new_field_serdes = serdes_for_type(compiler_context, new_field.type_info)
            py_type = new_field_serdes.type_

            has_default, default_val = _get_default_for_field(compiler_context, new_element_schema, new_field, py_type)

            if not has_default:
                msg = f"New field {new_field.cur_name} has no default value"
                raise ValueError(msg)

            new_field_defaults[new_field.cur_name] = default_val

    return new_field_defaults


def _create_soa_to_soa_converter(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    old_element_type: tachyon_model.SchemaType,
    new_element_schema: schema.InstantiatedSchema,
    new_type_info: typesys.Instantiation,
    old_types: Sequence[tachyon_model.ClkType],
    compiler_context: CompilerContext,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> Callable[[Any], Any] | None:
    """Create a converter for SoA→SoA that applies field-wise conversions.

    Input: SoA dataclass with field arrays (old schema fields)
    Output: SoA dataclass with field arrays (new schema fields)

    Handles field renames, removals, and type changes.
    """
    field_converters = _build_soa_field_converters(
        old_element_type=old_element_type,
        new_element_schema=new_element_schema,
        old_types=old_types,
        compiler_context=compiler_context,
        upgraders=upgraders,
        error_node=error_node,
    )

    new_soa_serdes = serdes_for_type(compiler_context, new_type_info)
    new_soa_class = new_soa_serdes.type_

    new_field_defaults = _get_soa_new_field_defaults(new_element_schema, field_converters, compiler_context)

    def soa_to_soa_converter(old_soa: Tachyon[Any]) -> Tachyon[Any]:
        """Convert SoA to SoA with field-wise type conversions and field mapping."""
        new_field_arrays = {}

        array_length = 0
        if field_converters:
            first_old_name = field_converters[0][0]
            array_length = len(getattr(old_soa, first_old_name))

        for old_name, new_name, field_conv in field_converters:
            old_array = getattr(old_soa, old_name)

            if field_conv is not None:
                new_field_arrays[new_name] = [field_conv(elem) for elem in old_array]
            else:
                new_field_arrays[new_name] = list(old_array)

        for new_field_name, default_val in new_field_defaults.items():
            new_field_arrays[new_field_name] = [default_val] * array_length

        # pyrefly: ignore[no-any-return-implicit] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return new_soa_class(**new_field_arrays)

    return soa_to_soa_converter


def _create_aos_to_soa_converter(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    new_element_schema: schema.InstantiatedSchema,
    new_type_info: typesys.Instantiation,
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    compiler_context: CompilerContext,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> Callable[[Any], Any]:
    """Create a converter for AoS→SoA (transpose from list of structs to struct of lists).

    Input: list[OldSchema] - array of old schema instances
    Output: SoA dataclass with field arrays for new schema

    Strategy:
    1. Convert each old instance to new instance using element converter
    2. Extract each field from the new instance into the corresponding field array
    3. Construct the SoA dataclass with the field arrays
    """
    # Get converter for old element → new element
    old_element_type = old_types[old_type_id]
    can_convert, element_converter = _create_type_converter(
        compiler_context=compiler_context,
        old_type_id=old_type_id,
        old_types=old_types,
        new_type_info=new_element_schema,
        upgraders=upgraders,
        error_node=error_node,
    )

    if not can_convert:
        msg = error_node.append_error_line(f"Cannot convert {old_element_type} to {new_element_schema}")
        raise ValueError(msg)

    # Get the new SoA type to construct
    new_soa_serdes = serdes_for_type(compiler_context, new_type_info)
    new_soa_class = new_soa_serdes.type_

    def aos_to_soa_converter(old_list: list[Tachyon[Any]]) -> Tachyon[Any]:
        """Transpose list of old structs to SoA with new field arrays."""
        field_arrays: dict[str, list[Any]] = {field.cur_name: [] for field in new_element_schema.fields.values()}

        for old_instance in old_list:
            new_instance = element_converter(old_instance) if element_converter else old_instance

            for field_name, field_array in field_arrays.items():
                field_array.append(getattr(new_instance, field_name))

        # pyrefly: ignore[no-any-return-implicit] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return new_soa_class(**field_arrays)

    return aos_to_soa_converter


def _create_soa_to_aos_converter(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    old_element_type: tachyon_model.SchemaType,
    new_element_schema: schema.InstantiatedSchema,
    old_types: Sequence[tachyon_model.ClkType],
    compiler_context: CompilerContext,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> Callable[[Any], Any]:
    """Create a converter for SoA→AoS (un-transpose struct of arrays to array of structs).

    Input: SoA dataclass with field arrays (old schema fields)
    Output: list[NewSchema] (array of new schema instances)

    Handles field renames, removals, and type changes.
    """
    field_converters: list[tuple[str, str, Callable[[Any], Any] | None]] = []  # [(old_name, new_name, converter)]

    for old_field in old_element_type.fields:
        final_field_num, _ = _trace_field_forward(new_element_schema.history, old_field.num)

        if final_field_num is None:
            continue

        if final_field_num not in new_element_schema.fields:
            msg = error_node.append_error_line(
                f"Field #{final_field_num} not found in schema {new_element_schema.schema_name}"
            )
            raise ValueError(msg)

        new_field = new_element_schema.fields[final_field_num]

        can_convert, field_converter = _create_type_converter(
            compiler_context=compiler_context,
            old_type_id=old_field.type_id,
            old_types=old_types,
            new_type_info=new_field.type_info,
            upgraders=upgraders,
            error_node=error_node,
        )

        if not can_convert:
            msg = error_node.append_error_line(
                f"Cannot convert field {old_field.name} from type_id {old_field.type_id} to {new_field.type_info}"
            )
            raise ValueError(msg)

        field_converters.append((old_field.name, new_field.cur_name, field_converter))

    new_aos_serdes = serdes_for_type(compiler_context, new_element_schema)
    new_aos_class = new_aos_serdes.type_

    def soa_to_aos_converter(soa: Tachyon[Any]) -> list[Tachyon[Any]]:
        """Un-transpose struct of arrays to array of structs."""
        if not field_converters:
            return []

        first_old_name = field_converters[0][0]
        field_array = getattr(soa, first_old_name)
        array_length = len(field_array)

        result = []
        for i in range(array_length):
            field_values = {}

            for old_name, new_name, field_conv in field_converters:
                val = getattr(soa, old_name)[i]
                field_values[new_name] = field_conv(val) if field_conv is not None else val

            # Newly added fields will use their dataclass defaults
            result.append(new_aos_class(**field_values))

        return result

    return soa_to_aos_converter


def _extract_soa_info(
    old_type: tachyon_model.ClkType,
    old_types: Sequence[tachyon_model.ClkType],
    error_node: node.CstNode[Any],
) -> tuple[tachyon_model.SchemaType, bool, int] | None:
    """Extract SoA information from a type and validate it.

    Returns:
        Tuple of (element_schema, is_fixed, size) if old_type is a SoaType, None otherwise

    Raises:
        TypeError: If the SoA element type is not a SchemaType
    """
    if not isinstance(old_type, tachyon_model.SoaType):
        return None

    old_element_type = old_types[old_type.schema_type_id]
    if not isinstance(old_element_type, tachyon_model.SchemaType):
        msg = error_node.append_error_line(f"SoA element must be a schema, got: {old_element_type}")
        raise TypeError(msg)

    is_fixed = old_type.size_field_offset is None
    return old_element_type, is_fixed, old_type.container_size


def _handle_soa_to_soa_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion between SoA types (VarSoa/FixedSoa)."""
    old_type = old_types[old_type_id]

    soa_info = _extract_soa_info(old_type, old_types, error_node)
    if soa_info is None:
        return False, None

    old_element_type, old_is_fixed, old_size = soa_info

    if not (
        isinstance(new_type_info, typesys.Instantiation)
        and new_type_info.instantiates in (clkbuiltins.VAR_SOA, clkbuiltins.FIXED_SOA)
    ):
        return False, None

    new_element_type = new_type_info.arguments["type"]
    if not isinstance(new_element_type, schema.InstantiatedSchema):
        msg = error_node.append_error_line(f"SoA element must be a schema, got: {new_element_type}")
        raise TypeError(msg)

    # FixedSoa cannot change size
    new_is_fixed = new_type_info.instantiates is clkbuiltins.FIXED_SOA
    if old_is_fixed and new_is_fixed:
        assert old_size is not None
        new_size_arg = new_type_info.arguments["size"]
        assert isinstance(new_size_arg, primitive.DecimalLiteral)
        new_size = int(new_size_arg.value)
        if old_size != new_size:
            return False, None

    converter = _create_soa_to_soa_converter(
        old_element_type=old_element_type,
        new_element_schema=new_element_type,
        new_type_info=new_type_info,
        old_types=old_types,
        compiler_context=compiler_context,
        upgraders=upgraders,
        error_node=error_node,
    )

    return True, converter


def _handle_array_to_soa_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion from array types (VarArray/FixedArray) to SoA types (VarSoa/FixedSoa)."""
    old_type = old_types[old_type_id]
    if not (
        isinstance(old_type, tachyon_model.BuiltInType)
        and old_type.fqn in (clkbuiltins.VAR_ARRAY.fqn, clkbuiltins.FIXED_ARRAY.fqn)
        and isinstance(new_type_info, typesys.Instantiation)
        and new_type_info.instantiates in (clkbuiltins.VAR_SOA, clkbuiltins.FIXED_SOA)
    ):
        return False, None

    if len(old_type.arguments) < 1 or not isinstance(old_type.arguments[0], int):
        msg = error_node.append_error_line(f"Invalid array type arguments: {old_type}")
        raise ValueError(msg)

    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    old_element_type_id = int(old_type.arguments[0])
    old_element_type = old_types[old_element_type_id]

    if not isinstance(old_element_type, tachyon_model.SchemaType):
        msg = error_node.append_error_line(f"Array→SoA element must be a schema, got: {old_element_type}")
        raise TypeError(msg)

    new_element_type = new_type_info.arguments["type"]
    if not isinstance(new_element_type, schema.InstantiatedSchema):
        msg = error_node.append_error_line(f"Array→SoA element must be a schema, got: {new_element_type}")
        raise TypeError(msg)

    # FixedArray/Soa cannot change size
    old_is_fixed = old_type.fqn == clkbuiltins.FIXED_ARRAY.fqn
    new_is_fixed = new_type_info.instantiates is clkbuiltins.FIXED_SOA
    if old_is_fixed and new_is_fixed:
        old_size = int(old_type.arguments[1])
        new_size_arg = new_type_info.arguments["size"]
        assert isinstance(new_size_arg, primitive.DecimalLiteral)
        new_size = int(new_size_arg.value)
        if old_size != new_size:
            return False, None

    converter = _create_aos_to_soa_converter(
        new_element_schema=new_element_type,
        new_type_info=new_type_info,
        old_type_id=old_element_type_id,
        old_types=old_types,
        compiler_context=compiler_context,
        upgraders=upgraders,
        error_node=error_node,
    )

    return True, converter


def _handle_soa_to_array_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion from SoA types (VarSoa/FixedSoa) to array types (VarArray/FixedArray)."""
    old_type = old_types[old_type_id]

    soa_info = _extract_soa_info(old_type, old_types, error_node)
    if soa_info is None:
        return False, None

    old_element_type, old_is_fixed, old_size = soa_info

    if not (
        isinstance(new_type_info, typesys.Instantiation)
        and new_type_info.instantiates in (clkbuiltins.VAR_ARRAY, clkbuiltins.FIXED_ARRAY)
    ):
        return False, None

    new_element_type = new_type_info.arguments["type"]
    if not isinstance(new_element_type, schema.InstantiatedSchema):
        msg = error_node.append_error_line(f"SoA→Array element must be a schema, got: {new_element_type}")
        raise TypeError(msg)

    # FixedArray/Soa cannot change size
    new_is_fixed = new_type_info.instantiates is clkbuiltins.FIXED_ARRAY
    if old_is_fixed and new_is_fixed:
        assert old_size is not None
        new_size_arg = new_type_info.arguments["size"]
        assert isinstance(new_size_arg, primitive.DecimalLiteral)
        new_size = int(new_size_arg.value)
        if old_size != new_size:
            return False, None

    converter = _create_soa_to_aos_converter(
        old_element_type=old_element_type,
        new_element_schema=new_element_type,
        old_types=old_types,
        compiler_context=compiler_context,
        upgraders=upgraders,
        error_node=error_node,
    )

    return True, converter


def _handle_optional_to_soa_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion from Optional[Schema] to SoA types (VarSoa/FixedSoa)."""
    old_type = old_types[old_type_id]
    if not (
        isinstance(old_type, tachyon_model.BuiltInType)
        and old_type.fqn == clkbuiltins.OPTIONAL.fqn
        and isinstance(new_type_info, typesys.Instantiation)
        and new_type_info.instantiates in (clkbuiltins.VAR_SOA, clkbuiltins.FIXED_SOA)
    ):
        return False, None

    if len(old_type.arguments) != 1 or not isinstance(old_type.arguments[0], int):
        msg = error_node.append_error_line(f"Invalid Optional type arguments: {old_type}")
        raise ValueError(msg)

    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    inner_old_type_id = int(old_type.arguments[0])
    inner_old_type = old_types[inner_old_type_id]

    if not isinstance(inner_old_type, tachyon_model.SchemaType):
        msg = error_node.append_error_line(f"Optional→SoA element must be a schema, got: {inner_old_type}")
        raise TypeError(msg)

    new_element_type = new_type_info.arguments["type"]
    if not isinstance(new_element_type, schema.InstantiatedSchema):
        msg = error_node.append_error_line(f"Optional→SoA element must be a schema, got: {new_element_type}")
        raise TypeError(msg)

    aos_to_soa = _create_aos_to_soa_converter(
        new_element_schema=new_element_type,
        new_type_info=new_type_info,
        old_type_id=inner_old_type_id,
        old_types=old_types,
        compiler_context=compiler_context,
        upgraders=upgraders,
        error_node=error_node,
    )

    def optional_to_soa(val: Any | None) -> Any:  # noqa: ANN401
        """Convert Optional[Schema] to SoA."""
        if val is None:
            # None → empty SoA
            aos_list: list[Any] = []
        else:
            # Single value → single-element SoA
            aos_list = [val]
        return aos_to_soa(aos_list)

    return True, optional_to_soa


def _handle_soa_to_optional_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion from SoA types (VarSoa/FixedSoa) to Optional[Schema]."""
    old_type = old_types[old_type_id]

    soa_info = _extract_soa_info(old_type, old_types, error_node)
    if soa_info is None:
        return False, None

    old_element_type, _old_is_fixed, _old_size = soa_info

    if not (isinstance(new_type_info, typesys.Instantiation) and new_type_info.instantiates is clkbuiltins.OPTIONAL):
        return False, None

    new_inner_type = new_type_info.arguments["type"]
    if not isinstance(new_inner_type, schema.InstantiatedSchema):
        msg = error_node.append_error_line(f"SoA→Optional element must be a schema, got: {new_inner_type}")
        raise TypeError(msg)

    soa_to_aos = _create_soa_to_aos_converter(
        old_element_type=old_element_type,
        new_element_schema=new_inner_type,
        old_types=old_types,
        compiler_context=compiler_context,
        upgraders=upgraders,
        error_node=error_node,
    )

    def soa_to_optional(soa_val: Any) -> Any | None:  # noqa: ANN401
        """Convert SoA to Optional[Schema]."""
        aos_list = soa_to_aos(soa_val)

        if not aos_list:
            return None  # Empty SoA → None

        if len(aos_list) > 1:
            msg = f"Cannot convert multi-element SoA to Optional: SoA has {len(aos_list)} elements"
            raise ValueError(msg)

        # pyrefly: ignore[no-any-return-implicit] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return aos_list[0]  # Single element → value

    return True, soa_to_optional


def _handle_varstring_to_varstring_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,  # noqa: ARG001 (need to match handler signature)
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],  # noqa: ARG001 (need to match handler signature)
    error_node: node.CstNode[Any],  # noqa: ARG001 (need to match handler signature)
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion between VarString types (potentially with different max sizes)."""
    old_type = old_types[old_type_id]
    return (
        isinstance(old_type, tachyon_model.BuiltInType)
        and old_type.fqn == clkbuiltins.VAR_STRING.fqn
        and isinstance(new_type_info, typesys.Instantiation)
        and new_type_info.instantiates is clkbuiltins.VAR_STRING
    ), None


def _handle_varstring_to_optional_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,  # noqa: ARG001 (need to match handler signature)
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],  # noqa: ARG001 (need to match handler signature)
    error_node: node.CstNode[Any],  # noqa: ARG001 (need to match handler signature)
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion from string to byte array."""
    old_type = old_types[old_type_id]
    if not (
        isinstance(old_type, tachyon_model.BuiltInType)
        and old_type.fqn == clkbuiltins.VAR_STRING.fqn
        and isinstance(new_type_info, typesys.Instantiation)
        and new_type_info.instantiates is clkbuiltins.OPTIONAL
    ):
        return False, None

    element_type_arg = new_type_info.arguments["type"]
    if isinstance(element_type_arg, clkbuiltins.PrimitiveType) and element_type_arg in [
        clkbuiltins.BYTE,
        clkbuiltins.UINT8,
        clkbuiltins.INT8,
    ]:

        def string_to_optional(val: str) -> int | None:
            if not val:
                return None
            if len(val) > 1:
                msg = f"Source string size ({len(val)} is greater than 1"
                raise ValueError(msg)
            # Convert string to list of bytes using UTF-8
            return ord(val[0])

        return True, string_to_optional

    return False, None


def _handle_optional_to_optional_conversion(  # noqa: PLR0913 (too many params mitigated by kwonly args)
    *,
    compiler_context: CompilerContext,
    old_type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    new_type_info: typesys.TypeVal,
    upgraders: dict[str, SchemaUpgrader | None],
    error_node: node.CstNode[Any],
) -> tuple[bool, Callable[[Any], Any] | None]:
    """Handle conversion between Optional<T> and Optional<U> where T and U are compatible."""
    old_type = old_types[old_type_id]
    if not (
        isinstance(old_type, tachyon_model.BuiltInType)
        and old_type.fqn == clkbuiltins.OPTIONAL.fqn
        and isinstance(new_type_info, typesys.Instantiation)
        and new_type_info.instantiates is clkbuiltins.OPTIONAL
    ):
        return False, None

    # Extract inner type from old optional
    if len(old_type.arguments) != 1 or not isinstance(old_type.arguments[0], int):
        msg = error_node.append_error_line(f"Invalid Optional type arguments: {old_type}")
        raise ValueError(msg)
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    old_inner_type_id = int(old_type.arguments[0])

    # Extract inner type from new optional
    new_inner_type = new_type_info.arguments.get("type")
    if not isinstance(new_inner_type, typesys.TypeVal):
        msg = error_node.append_error_line(f"Invalid Optional type arguments: {new_type_info}")
        raise TypeError(msg)

    can_convert, inner_converter = _create_type_converter(
        compiler_context=compiler_context,
        old_type_id=old_inner_type_id,
        old_types=old_types,
        new_type_info=new_inner_type,
        upgraders=upgraders,
        error_node=error_node,
    )
    if not can_convert:
        return False, None

    if inner_converter is not None:

        def optional_converter(val: object | None) -> object | None:
            if val is None:
                return None
            return cast("object", inner_converter(val))

        return True, optional_converter

    return True, None


def _trace_field_forward(
    schema_history: schema.SchemaHistory,
    field_num: int,
) -> tuple[int | None, list[int]]:
    """Trace a field's evolution forward through schema history.

    Args:
        schema_history: Schema history containing field transformation information
        field_num: Starting field number to trace

    Returns:
        Tuple of (final_field_number, path) where:
        - final_field_number is the current field number this field evolved into, or None if removed
        - path is the list of intermediate field numbers in the evolution chain (including the start)
    """
    path = [field_num]
    current = field_num

    while current in schema_history.legacy_became:
        current = schema_history.legacy_became[current]
        path.append(current)

    if current in schema_history.removed:
        return (None, path)

    return (current, path)


# PLR0913 (too many args) suppressed because we need all this. Mitigating confusion with kwonly args.
def process_schema_field(
    *,
    compiler_context: CompilerContext,
    schema_ir: schema.InstantiatedSchema,
    old_field: tachyon_model.SchemaField,
    old_types: Sequence[tachyon_model.ClkType],
    upgraders: dict[str, SchemaUpgrader | None],
) -> FieldUpgrade | None:
    """Process a single field for schema upgrade.

    Args:
        compiler_context: The compiler context
        schema_ir: The current schema
        old_field: Field information from the old schema
        old_types: All types from the old metadata
        old_metadata: The complete metadata from the old schema
        upgraders: The current set of schema upgraders

    Returns:
        A FieldUpgrade object if the field should be upgraded, None if it was removed
    """
    # First trace the field forward through schema history to find its current field number
    final_field_num, _ = _trace_field_forward(schema_ir.history, old_field.num)

    # If the field was removed, skip it
    if final_field_num is None:
        return None

    if final_field_num not in schema_ir.fields:
        msg = schema_ir.schema.append_error_line(
            f"Field #{final_field_num} not found in schema {schema_ir.schema_name} nor is it removed"
        )
        raise ValueError(msg)

    new_field = schema_ir.fields[final_field_num]

    old_name, new_name, upgrader = _create_field_upgrader(
        compiler_context=compiler_context,
        old_field=old_field,
        final_field_num=final_field_num,
        new_field=new_field,
        old_types=old_types,
        upgraders=upgraders,
    )

    return FieldUpgrade(
        old_name=old_name,
        new_name=new_name,
        upgrader=upgrader,
    )


def process_schema(
    compiler_context: CompilerContext,
    schema_ir: schema.InstantiatedSchema,
    type_id: int,
    old_types: Sequence[tachyon_model.ClkType],
    upgraders: dict[str, SchemaUpgrader | None],
) -> SchemaUpgrader | None:
    """Recursively process a schema and its nested schemas.

    Args:
        compiler_context: The compiler context
        schema_ir: The schema to process
        type_id: The type ID in the old metadata
        old_types: Types from the old version's metadata
        upgraders: The current set of schema upgraders
    """
    old_type = old_types[type_id]
    if not isinstance(old_type, tachyon_model.SchemaType):
        msg = schema_ir.schema.append_error_line(f"Expected SchemaType metadata, got {type(old_type)}")
        raise TypeError(msg)

    try:
        return upgraders[old_type.fqn]
    except KeyError:
        pass

    if schema_ir.schema_uuid != old_type.schema_uuid:
        msg = schema_ir.schema.append_error_line(
            f"UUID mismatch for schema {old_type.fqn}: {old_type.schema_uuid} != {schema_ir.schema_uuid}"
        )
        raise ValueError(msg)

    new_serdes = serdes_for_type(compiler_context, schema_ir)
    if old_type.version == schema_ir.cur_version():
        metadata = new_serdes.type_.get_tachyon_metadata()
        new_metadata = metadata.types[metadata.outer_type_id]
        if old_type.get_hash(old_types) == new_metadata.get_hash(metadata.types):
            upgraders[old_type.fqn] = None
            return None

    if old_type.version > schema_ir.history.version:
        msg = schema_ir.schema.append_error_line(
            f"Version {old_type.version} of {old_type.fqn} greater than current_version {schema_ir.history.version}"
        )
        raise ValueError(msg)

    field_upgrades = []
    for old_field in old_type.fields:
        field_upgrade = process_schema_field(
            compiler_context=compiler_context,
            schema_ir=schema_ir,
            old_field=old_field,
            old_types=old_types,
            upgraders=upgraders,
        )
        if field_upgrade is not None:
            field_upgrades.append(field_upgrade)

    # Create schema upgrader
    result = SchemaUpgrader(
        new_schema=schema_ir,
        new_serdes=new_serdes,
        old_fqn=old_type.fqn,
        old_version=old_type.version,
        field_upgrades=field_upgrades,
    )
    upgraders[old_type.fqn] = result
    return result


def create_upgrade_plan(
    compiler_context: CompilerContext,
    schema_ir: schema.InstantiatedSchema,
    old_metadata: tachyon_model.TachyonMetadata,
) -> UpgradePlan:
    """Create a plan for upgrading instances from old versions to current versions."""
    upgraders: dict[str, SchemaUpgrader | None] = {}

    # Start recursive processing from the root schema
    outer_upgrader = process_schema(
        compiler_context, schema_ir, old_metadata.outer_type_id, old_metadata.types, upgraders
    )
    old_fqn = old_metadata.types[old_metadata.outer_type_id].fqn
    py_type = None if outer_upgrader is None else cast("Tachyon[Any]", outer_upgrader.new_serdes.type_)
    return UpgradePlan(
        schema_upgraders=upgraders,
        py_type=py_type,
        old_fqn=old_fqn,
    )


def _create_field_upgrader(  # noqa: PLR0913 (too many params mitigated by kw_only args)
    *,
    compiler_context: CompilerContext,
    old_field: tachyon_model.SchemaField,
    final_field_num: int,
    new_field: schema.InstantiatedFieldDef,
    old_types: Sequence[tachyon_model.ClkType],
    upgraders: dict[str, SchemaUpgrader | None],
) -> tuple[str, str, Callable[[Any], Any] | None]:
    """Create an upgrader function for a field if needed.

    Args:
        compiler_context: The compiler context providing necessary resources
        old_field: Field information from metadata for the old schema
        final_field_num: The current field number this field evolved into
        new_field: The current field definition
        old_types: All types from metadata
        upgraders: The current set of schema upgraders

    Returns:
        Tuple of (old_field_name, new_field_name, upgrader_function)
        where upgrader_function is None if direct copy is sufficient
    """
    # If the field was removed (should not happen, already filtered in process_schema)
    if final_field_num is None:  # pyright: ignore[reportUnnecessaryComparison] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        return old_field.name, "", None

    # Get type information
    old_type_id = old_field.type_id
    old_type = old_types[old_type_id]
    new_type_info = new_field.type_info

    # Create a type converter if needed
    can_convert, converter = _create_type_converter(
        compiler_context=compiler_context,
        old_type_id=old_type_id,
        old_types=old_types,
        new_type_info=new_type_info,
        upgraders=upgraders,
        error_node=new_field,
    )
    if not can_convert:
        msg = new_field.append_error_line(f"Cannot convert {old_type} to {new_type_info.value_key()}")
        raise ValueError(msg)
    return old_field.name, new_field.cur_name, converter


def upgrade_schema(
    compiler_context: CompilerContext, schema_ir: schema.InstantiatedSchema, old_instance: Tachyon[Any]
) -> Tachyon[Any]:
    """Upgrade an instance from an old version to the current version.

    Use this for a one-off upgrade. If you are upgrading many instances of the
    same schema, use create_upgrade_plan instead to avoid re-computing the
    upgrade for each instance.

    Args:
        compiler_context: The compiler context providing necessary resources and configurations for the upgrade process
        schema_ir: The current version of the schema
        old_instance: The instance to upgrade

    Returns:
        The upgraded instance
    """
    old_metadata = old_instance.get_tachyon_metadata()
    if old_metadata is None:
        msg = f"No metadata available for old_instance: {old_instance}"
        raise ValueError(msg)

    plan = create_upgrade_plan(compiler_context, schema_ir, old_metadata)
    return plan.upgrade(old_instance)
