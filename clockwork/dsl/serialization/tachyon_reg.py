# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Tachyon Type Field Constraint (size/alignment) Registry."""

from __future__ import annotations

import math
from collections.abc import Callable
from dataclasses import dataclass
from typing import Final, TypeAlias

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.ir import clkbuiltins, clkenum, primitive, schema, typesys
from typing_extensions import override


@dataclass(frozen=True, eq=True, slots=True)
class FieldConstraint:
    """Constraint on a field layout.

    Attributes:
        size: The size of the field in bytes
        alignment: The minimum alignment in bytes
    """

    size: int
    alignment: int

    def array_stride(self) -> int:
        """Determine the stride of an array of elements with this constraint."""
        return math.ceil(self.size / self.alignment) * self.alignment


@dataclass(frozen=True, eq=True, slots=True)
class FieldLayoutInfo:
    """Layout information for a single field in an SoA structure.

    Attributes:
        field_num: The field number from the schema.
        size: Size in bytes of one element of this field.
        alignment: Alignment requirement for this field.
        offset: Byte offset of this field array in the SoA layout.
    """

    field_num: int
    size: int
    alignment: int
    offset: int


@dataclass(frozen=True, eq=True, slots=True)
class SoaLayoutInfo:
    """Complete layout information for an SoA structure.

    Attributes:
        field_layouts: List of field layout information in memory order.
                      For VarSoa, includes a size field with field_num=SIZE_FIELD_NUM.
        total_size: Total size of the SoA structure in bytes (including size field if present).
        max_alignment: Maximum alignment requirement across all fields.
    """

    field_layouts: tuple[FieldLayoutInfo, ...]
    total_size: int
    max_alignment: int


TypeKey: TypeAlias = str

CppFactoryType = Callable[[CompilerContext | None, typesys.Instantiation], FieldConstraint | None]

# Module-level constraint constants for built-in types
BOOL_CONSTRAINT: Final = FieldConstraint(size=1, alignment=1)
BYTE_CONSTRAINT: Final = FieldConstraint(size=1, alignment=1)
INT8_CONSTRAINT: Final = FieldConstraint(size=1, alignment=1)
UINT8_CONSTRAINT: Final = FieldConstraint(size=1, alignment=1)
INT16_CONSTRAINT: Final = FieldConstraint(size=2, alignment=2)
UINT16_CONSTRAINT: Final = FieldConstraint(size=2, alignment=2)
INT32_CONSTRAINT: Final = FieldConstraint(size=4, alignment=4)
UINT32_CONSTRAINT: Final = FieldConstraint(size=4, alignment=4)
INT64_CONSTRAINT: Final = FieldConstraint(size=8, alignment=8)
UINT64_CONSTRAINT: Final = FieldConstraint(size=8, alignment=8)
FLOAT32_CONSTRAINT: Final = FieldConstraint(size=4, alignment=4)
FLOAT64_CONSTRAINT: Final = FieldConstraint(size=8, alignment=8)
DURATION_CONSTRAINT: Final = FieldConstraint(size=8, alignment=8)
SYNC_TIME_CONSTRAINT: Final = FieldConstraint(size=8, alignment=8)
UUID_CONSTRAINT: Final = FieldConstraint(size=16, alignment=8)

# Dictionary mapping primitive type to its constraint
PRIMITIVE_CONSTRAINTS: Final[dict[str, tuple[str | None, FieldConstraint]]] = {
    clkbuiltins.BOOL.value_key(): ("builtins", BOOL_CONSTRAINT),
    clkbuiltins.BYTE.value_key(): ("builtins", BYTE_CONSTRAINT),
    clkbuiltins.INT8.value_key(): ("builtins", INT8_CONSTRAINT),
    clkbuiltins.UINT8.value_key(): ("builtins", UINT8_CONSTRAINT),
    clkbuiltins.INT16.value_key(): ("builtins", INT16_CONSTRAINT),
    clkbuiltins.UINT16.value_key(): ("builtins", UINT16_CONSTRAINT),
    clkbuiltins.INT32.value_key(): ("builtins", INT32_CONSTRAINT),
    clkbuiltins.UINT32.value_key(): ("builtins", UINT32_CONSTRAINT),
    clkbuiltins.INT64.value_key(): ("builtins", INT64_CONSTRAINT),
    clkbuiltins.UINT64.value_key(): ("builtins", UINT64_CONSTRAINT),
    clkbuiltins.FLOAT32.value_key(): ("builtins", FLOAT32_CONSTRAINT),
    clkbuiltins.FLOAT64.value_key(): ("builtins", FLOAT64_CONSTRAINT),
    clkbuiltins.DURATION.value_key(): ("builtins", DURATION_CONSTRAINT),
    clkbuiltins.SYNC_TIME.value_key(): ("builtins", SYNC_TIME_CONSTRAINT),
}

SIZE_FIELD_SIZE: Final = 8
SIZE_FIELD_ALIGNMENT: Final = 8

# Sentinel value for size field in SoA layouts (not a real schema field)
SIZE_FIELD_NUM: Final = -999


class TachyonRegistry(Context):
    """Compiler Context for Tachyon field constraints."""

    def __init__(self, name: str | None) -> None:
        """Create a new, empty Tachyon registry."""
        self.name = name
        self.type_registry: dict[TypeKey, tuple[str | None, FieldConstraint]] = {}
        self.generic_type_registry: dict[int, CppFactoryType] = {}

    @override
    def import_from(self, other: TachyonRegistry) -> None:
        """Combine this context with items from another.

        Raises:
            RuntimeError: If a type already exists with a different constraint.
        """
        for key, (name, constraint) in other.type_registry.items():
            if key in self.type_registry and self.type_registry[key][1] != constraint:
                msg = f"Type {key} has conflicting constraints: {self.type_registry[key][1]} vs {constraint}\nWhen merging from {name} into {self.name}"
                raise RuntimeError(msg)
            self.type_registry[key] = (name, constraint)

        for generic_key, factory in other.generic_type_registry.items():
            if generic_key in self.generic_type_registry and self.generic_type_registry[generic_key] is not factory:
                msg = f"Generic factory {generic_key} has conflicting registrations"
                raise RuntimeError(msg)
            self.generic_type_registry[generic_key] = factory


class TachyonRegistryKey(ContextKey[TachyonRegistry]):
    """Compiler context key for Tachyon field constraints registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> TachyonRegistry:
        """Create a default instance of the registry with built-in types."""
        registry = TachyonRegistry(compiler_context.name)

        # Register primitive types using the module-level constants
        registry.type_registry.update(PRIMITIVE_CONSTRAINTS)

        # Register UUID with its factory
        register_generic_type(None, clkbuiltins.UUID, _uuid_factory, _registry=registry)

        # Register array types with their factories
        register_generic_type(None, clkbuiltins.FIXED_ARRAY, _fixed_array_factory, _registry=registry)
        register_generic_type(None, clkbuiltins.OPTIONAL, _optional_factory, _registry=registry)
        register_generic_type(None, clkbuiltins.VAR_ARRAY, _var_array_factory, _registry=registry)
        register_generic_type(None, clkbuiltins.VAR_STRING, _var_string_factory, _registry=registry)

        # Register SoA types with their factories
        register_generic_type(None, clkbuiltins.FIXED_SOA, _fixed_soa_factory, _registry=registry)
        register_generic_type(None, clkbuiltins.VAR_SOA, _var_soa_factory, _registry=registry)

        return registry


TACHYON_REGISTRY_KEY: Final = TachyonRegistryKey("TachyonRegistry")

# Maximum values for unsigned integer types (used for SoA size field selection)
UINT8_MAX: Final = 0xFF
UINT16_MAX: Final = 0xFFFF
UINT32_MAX: Final = 0xFFFFFFFF
UINT64_MAX: Final = 0xFFFFFFFFFFFFFFFF


def align_offset(offset: int, alignment: int) -> int:
    """Round up offset to the next alignment boundary.

    Args:
        offset: The current offset.
        alignment: The alignment requirement.

    Returns:
        The aligned offset.
    """
    if alignment <= 0:
        return offset
    return offset + (alignment - (offset % alignment)) % alignment


def get_compact_size_type_info(
    max_size: int,
) -> tuple[clkbuiltins.IntegerPrimitiveBuiltinSerializable, FieldConstraint]:
    """Get the compact size type and constraint for a given max_size.

    Selects the smallest unsigned integer type that can hold max_size values.

    Args:
        max_size: The maximum number of elements the container can hold.

    Returns:
        Tuple of (type_val, field_constraint)
        - type_val: clkbuiltins type (UINT8, UINT16, UINT32, or UINT64)
        - field_constraint: The FieldConstraint for that type

    Raises:
        ValueError: If max_size exceeds UINT64_MAX
    """
    if max_size <= UINT8_MAX:
        return (clkbuiltins.UINT8, UINT8_CONSTRAINT)
    if max_size <= UINT16_MAX:
        return (clkbuiltins.UINT16, UINT16_CONSTRAINT)
    if max_size <= UINT32_MAX:
        return (clkbuiltins.UINT32, UINT32_CONSTRAINT)
    if max_size > UINT64_MAX:
        msg = f"max_size {max_size} exceeds maximum representable size {UINT64_MAX}"
        raise ValueError(msg)
    return (clkbuiltins.UINT64, UINT64_CONSTRAINT)


def compute_soa_layout(
    compiler_context: CompilerContext,
    schema_ir: schema.InstantiatedSchema,
    max_size: int,
    *,
    include_size_field: bool = False,
) -> SoaLayoutInfo:
    """Compute SoA memory layout for a schema.

    SoA layout differs from traditional Array-of-Structs (AoS) in that fields are
    laid out as separate contiguous arrays, eliminating inter-element padding.
    Each field array is sized to hold 'max_size' elements with proper alignment. Fields are sorted by alignment (descending), size (descending), field_num (ascending), which minimizes padding. The size field (for variable-sized arrays) is sized to hold the max size and comes last.

    Args:
        compiler_context: The compiler context.
        schema_ir: The instantiated schema to compute layout for.
        max_size: The maximum number of elements in the SoA.
        include_size_field: If True, append a size field to the layout (for VarSoa).

    Returns:
        SoaLayoutInfo containing field layouts, total size, and max alignment.
        For VarSoa (include_size_field=True), field_layouts includes a size field
        with field_num=SIZE_FIELD_NUM.

    Raises:
        ValueError: If any field has an unknown constraint.
    """
    if max_size == 0:
        return SoaLayoutInfo(field_layouts=(), total_size=0, max_alignment=1)

    field_constraints_list: list[tuple[int, FieldConstraint]] = []
    for field in schema_ir.fields.values():
        field_constraint = constraint_for_type(compiler_context, field.type_info)
        if field_constraint is None:
            msg = f"Cannot determine constraint for field '{field.cur_name}' of type {field.type_info}"
            raise ValueError(msg)
        field_constraints_list.append((field.num, field_constraint))

    def _field_constraint_sort_key(x: tuple[int, FieldConstraint]) -> tuple[int, int, int]:
        field_num, field_constraint = x
        return (-field_constraint.alignment, -field_constraint.size, field_num)

    field_constraints_list.sort(key=_field_constraint_sort_key)

    field_layouts: list[FieldLayoutInfo] = []
    total_offset = 0
    max_alignment = 1

    for field_num, field_constraint in field_constraints_list:
        max_alignment = max(max_alignment, field_constraint.alignment)

        total_offset = align_offset(total_offset, field_constraint.alignment)

        field_layouts.append(
            FieldLayoutInfo(
                field_num=field_num,
                size=field_constraint.size,
                alignment=field_constraint.alignment,
                offset=total_offset,
            )
        )

        field_array_size = max_size * field_constraint.size
        total_offset += field_array_size

    # Add size field if requested (for VarSoa)
    if include_size_field:
        _size_type, size_field_constraint = get_compact_size_type_info(max_size)
        max_alignment = max(max_alignment, size_field_constraint.alignment)
        total_offset = align_offset(total_offset, size_field_constraint.alignment)

        field_layouts.append(
            FieldLayoutInfo(
                field_num=SIZE_FIELD_NUM,
                size=size_field_constraint.size,
                alignment=size_field_constraint.alignment,
                offset=total_offset,
            )
        )

        total_offset += size_field_constraint.size

    # Align the total size to the maximum alignment requirement
    # This is necessary because the struct has alignas(max_alignment)
    total_size = align_offset(total_offset, max_alignment)

    return SoaLayoutInfo(field_layouts=tuple(field_layouts), total_size=total_size, max_alignment=max_alignment)


def constraint_for_type(
    compiler_context: CompilerContext | None, typ: typesys.TypeVal, *, _registry: TachyonRegistry | None = None
) -> FieldConstraint | None:
    """Determine size and alignment for a type.

    Args:
        compiler_context: Compiler context containing the registry.
        typ: The type to look up.
        _registry: Internal use only - registry to use instead of context.

    Returns:
        The size and alignment (as a FieldConstraint) if found, else None
    """
    if _registry is None:
        if compiler_context is None:
            msg = "Either context or _registry must be provided"
            raise ValueError(msg)
        registry = compiler_context[TACHYON_REGISTRY_KEY]
    else:
        registry = _registry

    try:
        return registry.type_registry[typ.value_key()][1]
    except KeyError:
        pass
    if isinstance(typ, typesys.Instantiation):
        try:
            factory = registry.generic_type_registry[id(typ.instantiates)]
        except KeyError:
            return None
        constraint = factory(compiler_context, typ)
        if constraint is not None:
            registry.type_registry[typ.value_key()] = (registry.name, constraint)
        return constraint
    if isinstance(typ, clkenum.ResolvedEnum):
        return _enum_factory(compiler_context, typ, _registry=registry)
    return None


def register_type(
    compiler_context: CompilerContext | None,
    typ: typesys.TypeVal,
    constraint: FieldConstraint,
    *,
    _registry: TachyonRegistry | None = None,
) -> None:
    """Register size and alignment constraint for a non-generic type.

    Args:
        compiler_context: Compiler context containing the registry.
        typ: The type to register; must be fully instantiated.
        constraint: The constraint for the type.
        _registry: Internal use only - registry to use instead of context.

    Raises:
        RuntimeError if the type is not fully instantiated or is already registered with different constraint
    """
    if _registry is None:
        if compiler_context is None:
            msg = "Either compiler_context or _registry must be provided"
            raise ValueError(msg)
        registry = compiler_context[TACHYON_REGISTRY_KEY]
    else:
        registry = _registry

    if typ.generic_parameters():
        msg = f"Attempt to register generic type {typ}"
        raise RuntimeError(msg)
    existing = constraint_for_type(compiler_context, typ, _registry=registry)
    if existing:
        if constraint != existing:
            msg = (
                f"Attempt to register constraint {constraint} "
                f"which does not match previous registration {existing} "
                f"for type {typ}"
            )
            raise RuntimeError(msg)
        return
    registry.type_registry[typ.value_key()] = (registry.name, constraint)


def register_generic_type(
    compiler_context: CompilerContext | None,
    typ: typesys.TypeVal,
    factory: CppFactoryType,
    *,
    _registry: TachyonRegistry | None = None,
) -> None:
    """Register a constraint factory for a generic type.

    Args:
        compiler_context: Compiler context containing the registry.
        typ: The generic type to register
        factory: A factory function that generates a constraint for an instantiation.
        _registry: Internal use only - registry to use instead of context.

    Raises:
        RuntimeError if the type is not a generic type or is already registered.
    """
    if _registry is None:
        if compiler_context is None:
            msg = "Either compiler_context or _registry must be provided"
            raise ValueError(msg)
        registry = compiler_context[TACHYON_REGISTRY_KEY]
    else:
        registry = _registry

    if not typ.generic_parameters():
        msg = f"Attempt to register a factory for non-generic type {typ}"
        raise RuntimeError(msg)
    if id(typ) in registry.generic_type_registry:
        msg = f"Attempt to register already-registered generic type {typ}"
        raise RuntimeError(msg)
    registry.generic_type_registry[id(typ)] = factory


# ARG001 suppressed because it's needed for the factory signature even though unused in this factory
def _uuid_factory(compiler_context: CompilerContext | None, typ: typesys.Instantiation) -> FieldConstraint | None:  # noqa: ARG001
    """Helper function for Uuid."""
    if typ.instantiates is not clkbuiltins.UUID:
        msg = f"Expected Uuid but got {typ.instantiates}"
        raise RuntimeError(msg)
    # We align UUIDs to 8 bytes so they can be treated as two int64's, e.g. for fast hash computation.
    return UUID_CONSTRAINT


def _enum_factory(
    compiler_context: CompilerContext | None,
    clk_enum: clkenum.ResolvedEnum,
    *,
    _registry: TachyonRegistry | None = None,
) -> FieldConstraint:
    """Get the constraint for an enum's underlying type.

    Args:
        compiler_context: Compiler context containing the registry.
        clk_enum: The enum type to get the constraint for.
        _registry: Internal use only - registry to use instead of context.

    Returns:
        The constraint for the enum's underlying type.

    Raises:
        RuntimeError: If the constraint for the underlying type cannot be determined.
    """
    constraint = constraint_for_type(compiler_context, clk_enum.underlying_type, _registry=_registry)
    if constraint is None:
        # This should never happen as the underlying type is a builtin type.
        msg = f"Unable to determine constraint for enum: {clk_enum}"
        raise RuntimeError(msg)
    return constraint


def _array_factory(
    compiler_context: CompilerContext | None, element_type: typesys.Value, size: typesys.Value
) -> FieldConstraint | None:
    """Helper function to determine constraints for array-like types."""
    if not isinstance(element_type, typesys.TypeVal):
        msg = f"Bad value type for type parameter: {element_type}"
        raise RuntimeError(msg)  # noqa: TRY004
    if not isinstance(size, primitive.DecimalValue):
        msg = f"Bad value type for size parameter: {size}"
        raise RuntimeError(msg)  # noqa: TRY004
    element_constraint = constraint_for_type(compiler_context, element_type)
    if element_constraint is None:
        return None
    stride = element_constraint.array_stride()
    size_int = primitive.unsigned_decimal_to_int(size)
    return FieldConstraint(size=size_int * stride, alignment=element_constraint.alignment)


def _fixed_array_factory(
    compiler_context: CompilerContext | None, typ: typesys.Instantiation
) -> FieldConstraint | None:
    """Constraint factory for FixedArray."""
    if typ.instantiates is not clkbuiltins.FIXED_ARRAY:
        msg = f"Expected FixedArray but got {typ.instantiates}"
        raise RuntimeError(msg)
    element_type = typ.arguments["type"]
    size = typ.arguments["size"]
    return _array_factory(compiler_context, element_type, size)


def _add_size_field(constraint: FieldConstraint | None) -> FieldConstraint | None:
    """Helper to add allowance for a size field after an array."""
    if constraint is None:
        return None
    array_plus_padding = math.ceil(constraint.size / SIZE_FIELD_ALIGNMENT) * SIZE_FIELD_ALIGNMENT
    return FieldConstraint(
        size=(array_plus_padding + SIZE_FIELD_SIZE),
        alignment=max(SIZE_FIELD_ALIGNMENT, constraint.alignment),
    )


def _var_array_factory(compiler_context: CompilerContext | None, typ: typesys.Instantiation) -> FieldConstraint | None:
    """Constraint factory for VarArray."""
    if typ.instantiates is not clkbuiltins.VAR_ARRAY:
        msg = f"Expected VarArray but got {typ.instantiates}"
        raise RuntimeError(msg)
    element_type = typ.arguments["type"]
    size = typ.arguments["max_size"]
    return _add_size_field(_array_factory(compiler_context, element_type, size))


def _var_string_factory(compiler_context: CompilerContext | None, typ: typesys.Instantiation) -> FieldConstraint | None:
    """Constraint factory for VarString."""
    if typ.instantiates is not clkbuiltins.VAR_STRING:
        msg = f"Expected VarString but got {typ.instantiates}"
        raise RuntimeError(msg)
    size = typ.arguments["max_size"]
    return _add_size_field(_array_factory(compiler_context, clkbuiltins.BYTE, size))


def _add_bool_field(constraint: FieldConstraint | None) -> FieldConstraint | None:
    """Helper to add allowance for a bool field after Optional."""
    if constraint is None:
        return None
    bool_field_alignment: Final = 1
    bool_field_size: Final = 1

    # Only works because bool alignment is 1.  This should never change.
    trailing_padding = constraint.alignment - bool_field_alignment

    return FieldConstraint(
        size=constraint.size + bool_field_size + trailing_padding,
        alignment=constraint.alignment,
    )


def _optional_factory(compiler_context: CompilerContext | None, typ: typesys.Instantiation) -> FieldConstraint | None:
    """Constraint factory for Optional."""
    if typ.instantiates is not clkbuiltins.OPTIONAL:
        msg = f"Expected Optional but got {typ.instantiates}"
        raise RuntimeError(msg)
    value_type = typ.arguments["type"]
    if not isinstance(value_type, typesys.TypeVal):
        msg = f"Bad value type for type parameter: {value_type}"
        raise TypeError(msg)
    return _add_bool_field(constraint_for_type(compiler_context, value_type))


def _soa_factory_impl(
    compiler_context: CompilerContext | None,
    element_type: typesys.Value,
    size: typesys.Value,
    *,
    include_size_field: bool = False,
) -> FieldConstraint | None:
    """Helper function to determine constraints for SoA types.

    SoA layout differs from arrays in that fields are laid out as separate contiguous arrays,
    eliminating inter-element padding. Each field array is sized to hold 'size' elements with
    proper alignment. The total size is the sum of all field arrays, each padded to maintain
    its alignment requirement.

    Args:
        compiler_context: The compiler context.
        element_type: The schema type for SoA elements.
        size: The maximum number of elements (as a DecimalValue).
        include_size_field: If True, include a size field (for VarSoa).

    Returns:
        FieldConstraint for the SoA type, or None on error.
    """
    if not isinstance(size, primitive.DecimalValue):
        msg = f"Bad value type for size parameter: {size}"
        raise RuntimeError(msg)  # noqa: TRY004 # This is an internal compiler error, not a user error.

    if not isinstance(element_type, schema.InstantiatedSchema):
        msg = f"SoA element type must be a schema type, got: {element_type.value_key()} = {type(element_type)}"
        raise TypeError(msg)
    schema_ir = element_type

    size_int = primitive.unsigned_decimal_to_int(size)

    if compiler_context is None:
        msg = "CompilerContext required for SoA layout computation"
        raise ValueError(msg)

    layout_info = compute_soa_layout(compiler_context, schema_ir, size_int, include_size_field=include_size_field)

    return FieldConstraint(size=layout_info.total_size, alignment=layout_info.max_alignment)


def _fixed_soa_factory(compiler_context: CompilerContext | None, typ: typesys.Instantiation) -> FieldConstraint | None:
    """Constraint factory for FixedSoa."""
    if typ.instantiates is not clkbuiltins.FIXED_SOA:
        msg = f"Expected FixedSoa but got {typ.instantiates}"
        raise RuntimeError(msg)
    element_type = typ.arguments["type"]
    size = typ.arguments["size"]
    return _soa_factory_impl(compiler_context, element_type, size, include_size_field=False)


def _var_soa_factory(compiler_context: CompilerContext | None, typ: typesys.Instantiation) -> FieldConstraint | None:
    """Constraint factory for VarSoa."""
    if typ.instantiates is not clkbuiltins.VAR_SOA:
        msg = f"Expected VarSoa but got {typ.instantiates}"
        raise RuntimeError(msg)
    element_type = typ.arguments["type"]
    max_size = typ.arguments["max_size"]
    return _soa_factory_impl(compiler_context, element_type, max_size, include_size_field=True)
