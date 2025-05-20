# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Type registry for clockwork to protocol buffer type mappings."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Final

from clockwork.dsl.ir import clkbuiltins, module_id, typesys


@dataclass
class ProtobufType:
    """Protocol buffer type representation."""

    type_name: str

    def render(self) -> str:
        """Render the type."""
        return f"{self.type_name}"


@dataclass
class PrimitiveProtobufType(ProtobufType):
    """Represents a primitive protobuf type."""

    def render(self) -> str:
        """Render as a string."""
        return f"optional {super().render()}"


@dataclass
class DefinedProtobufType(ProtobufType):
    """Represents a non-primitive protobuf type."""

    module_id: module_id.ModuleID | None
    import_location: str | None
    package_name: str
    validate_fields: bool

    def render(self) -> str:
        """Render as string."""
        if self.package_name:
            return f"{self.package_name}.{super().render()}"
        return f"{super().render()}"


@dataclass
class EnumProtobufType(DefinedProtobufType):
    """Represents a non-primitive protobuf type."""

    def render(self) -> str:
        """Render as a string."""
        return f"optional {super().render()}"


@dataclass
class ArrayProtobufType(DefinedProtobufType):
    """Special Protobuf type to represent the builtin clockwork VarArray and FixedArray."""

    def render(self) -> str:
        """Render the var array."""
        return f"repeated {super().render()}"


INDENT = "   "


@dataclass
class OptionalProtobufType(DefinedProtobufType):
    """Special Protobuf type to represent the builtin clockwork Optional container."""

    def render(self) -> str:
        """Render the optional."""
        return f"optional {super().render()}"


_PROTOBUF_TYPE_REGISTRY: Final[dict[str, ProtobufType]] = {}

_FORBIDDEN_NESTED_TYPES: Final[set[str]] = set()


def register_forbidden_nested_type(clk_type: typesys.Value) -> None:
    """Register a type as forbidden to be nested in an optional or VarArray."""
    _FORBIDDEN_NESTED_TYPES.add(clk_type.value_key())


def in_forbidden_nested_types(clk_type: typesys.Value) -> bool:
    """Check if a type is forbidden to be nested in an optional of VarArray."""
    return clk_type.value_key() in _FORBIDDEN_NESTED_TYPES


register_forbidden_nested_type(clkbuiltins.OPTIONAL)
register_forbidden_nested_type(clkbuiltins.VAR_ARRAY)
register_forbidden_nested_type(clkbuiltins.FIXED_ARRAY)


def register_protobuf_type(clk_type: typesys.Value, protobuf_type: ProtobufType) -> None:
    """Registers the Protobuf type information for a given Clockwork type.

    Args:
        clk_type: The Clockwork type that needs to be mapped to a protobuf type.
        protobuf_type: The corresponding protobuf type descriptor.

    Raises:
        ValueError: If the Clockwork type is already registered with a different protobuf type.
    """
    if not isinstance(clk_type, typesys.TypeVal):
        msg = f"Cannot register Protobuf type corresponding to {clk_type}"
        raise TypeError(msg)
    try:
        existing_type = _PROTOBUF_TYPE_REGISTRY[clk_type.value_key()]
        msg = f"Type {clk_type} already registered as {existing_type}"
        raise ValueError(msg)
    except KeyError:
        pass
    _PROTOBUF_TYPE_REGISTRY[clk_type.value_key()] = protobuf_type


register_protobuf_type(clkbuiltins.BOOL, PrimitiveProtobufType(type_name="bool"))
register_protobuf_type(clkbuiltins.FLOAT32, PrimitiveProtobufType(type_name="float"))
register_protobuf_type(clkbuiltins.FLOAT64, PrimitiveProtobufType(type_name="double"))
register_protobuf_type(clkbuiltins.INT8, PrimitiveProtobufType(type_name="sint32"))
register_protobuf_type(clkbuiltins.INT16, PrimitiveProtobufType(type_name="sint32"))
register_protobuf_type(clkbuiltins.INT32, PrimitiveProtobufType(type_name="sint32"))
register_protobuf_type(clkbuiltins.INT64, PrimitiveProtobufType(type_name="sint64"))
register_protobuf_type(clkbuiltins.UINT8, PrimitiveProtobufType(type_name="uint32"))
register_protobuf_type(clkbuiltins.UINT16, PrimitiveProtobufType(type_name="uint32"))
register_protobuf_type(clkbuiltins.UINT32, PrimitiveProtobufType(type_name="uint32"))
register_protobuf_type(clkbuiltins.UINT64, PrimitiveProtobufType(type_name="uint64"))
register_protobuf_type(clkbuiltins.BYTE, PrimitiveProtobufType(type_name="string"))
register_protobuf_type(
    clkbuiltins.DURATION,
    DefinedProtobufType(
        type_name="Duration",
        module_id=module_id.ModuleID(repo="protobuf", name="google::protobuf::duration"),
        import_location="google/protobuf/duration.proto",
        package_name="google.protobuf",
        validate_fields=False,
    ),
)
register_protobuf_type(
    clkbuiltins.SYNC_TIME,
    DefinedProtobufType(
        type_name="Timestamp",
        module_id=module_id.ModuleID(repo="protobuf", name="google::protobuf::timestamp"),
        import_location="google/protobuf/timestamp.proto",
        package_name="google.protobuf",
        validate_fields=False,
    ),
)


def get_protobuf_type(clk_type: typesys.Value) -> ProtobufType:
    """Gets the Protobuf type information for a given Clockwork type value.

    Args:
        clk_type: The Clockwork type for which the Protobuf type info is required.

    Returns:
        The corresponding Protobuf type descriptor.

    Raises:
        TypeError: If the Clockwork type cannot be mapped to a Protobuf type.
    """
    if isinstance(clk_type, typesys.Instantiation) and (
        (clk_type.instantiates == clkbuiltins.VAR_ARRAY) | (clk_type.instantiates == clkbuiltins.FIXED_ARRAY)
    ):
        if clk_type.arguments["type"] == clkbuiltins.BYTE:
            return PrimitiveProtobufType(type_name="string")

        nested_type = clk_type.arguments["type"]
        value_type = get_protobuf_type(nested_type)
        if isinstance(nested_type, typesys.Instantiation) and in_forbidden_nested_types(nested_type.instantiates):
            msg = f"Cannot resolve the contained type in a VarArray. Note that protobuf generation does not support nested Arrays. Contained type was: {clk_type.arguments['type']}"
            raise TypeError(msg)
        module_id = value_type.module_id if isinstance(value_type, DefinedProtobufType) else None
        import_location = value_type.import_location if isinstance(value_type, DefinedProtobufType) else None
        package_name = value_type.package_name if isinstance(value_type, DefinedProtobufType) else ""
        return ArrayProtobufType(
            type_name=value_type.type_name,
            module_id=module_id,
            import_location=import_location,
            package_name=package_name,
            validate_fields=False,
        )
    if isinstance(clk_type, typesys.Instantiation) and clk_type.instantiates == clkbuiltins.OPTIONAL:
        nested_type = clk_type.arguments["type"]
        value_type = get_protobuf_type(nested_type)
        if isinstance(nested_type, typesys.Instantiation) and in_forbidden_nested_types(nested_type.instantiates):
            msg = f"Cannot resolve the contained type in an Optional note that protobuf generation does not support optional VarArrays. Contained type was: {clk_type.arguments['type']}"
            raise TypeError(msg)
        module_id = value_type.module_id if isinstance(value_type, DefinedProtobufType) else None
        import_location = value_type.import_location if isinstance(value_type, DefinedProtobufType) else None
        package_name = value_type.package_name if isinstance(value_type, DefinedProtobufType) else ""
        return OptionalProtobufType(
            type_name=value_type.type_name,
            module_id=module_id,
            import_location=import_location,
            package_name=package_name,
            validate_fields=False,
        )

    if isinstance(clk_type, typesys.Instantiation) and (
        (clk_type.instantiates == clkbuiltins.UUID) | (clk_type.instantiates == clkbuiltins.VAR_STRING)
    ):
        return PrimitiveProtobufType(type_name="string")
    return get_protobuf_type_from_registry(clk_type)


def get_protobuf_type_from_registry(clk_type: typesys.Value) -> ProtobufType:
    """Gets the Protobuf type information for a given Clockwork type value.

    Args:
        clk_type: The Clockwork type for which the Protobuf type info is required.

    Returns:
        The corresponding Protobuf type descriptor.

    Raises:
        TypeError: If the Clockwork type cannot be mapped to a Protobuf type.
    """
    if not isinstance(clk_type, typesys.TypeVal):
        msg = f"Cannot construct Protobuf type corresponding to {clk_type}"
        raise TypeError(msg)
    try:
        return _PROTOBUF_TYPE_REGISTRY[clk_type.value_key()]
    except KeyError:
        msg = (
            f"No Protobuf type registered for Clockwork type {clk_type}.\n"
            "If this is a user defined type, it might be missing from a `proto_target`."
        )
        raise TypeError(msg) from None
