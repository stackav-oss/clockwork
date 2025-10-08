# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Clockwork Builtins.

This module is named clkbuiltins instead of just builtins to avoid conflict with python's builtins module.
"""

from __future__ import annotations

import uuid
from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Final, cast

from clockwork.dsl import compiler_context
from clockwork.dsl.ir import node, typesys
from clockwork.dsl.ir.module_id import ModuleID
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Callable


CLOCKWORK_NAMESPACE_UUID: Final = uuid.UUID("2a13e50a-6e47-43bd-bc7f-e4ca2be5efba")


@dataclass
class SerializableBuiltin(typesys.TypeDef):
    """A Builtin type that is serializable.

    This adds a UUID to be used as a unique ID in serialization metadata. This should only be used for builtin types,
    in the BUILTINS module, not as a general way to add a UUID.
    """

    uuid: uuid.UUID = field(init=False)  # Set in post_init instead of constructor

    def __post_init__(self) -> None:
        """Set the UUID for this type."""
        super_post_init = getattr(super(), "__post_init__", None)
        if super_post_init:
            super_post_init()
        if not hasattr(self, "uuid"):
            self.uuid = uuid.uuid5(CLOCKWORK_NAMESPACE_UUID, self.name)


@dataclass
class BuiltinSerializableTypeDef(SerializableBuiltin, typesys.TypeDef):
    """A TypeDef that is serializable."""


@dataclass
class BuiltinSerializableGenericTypeDef(SerializableBuiltin, typesys.GenericTypeDef):
    """A GenericTypeDef that is serializable."""


@dataclass
class PrimitiveType(typesys.TypeDef):
    """Base class for primitive types.

    A primitive type is a type with scalar values of a pre-defined range and precision, with no constraints or
    invariants beyond that.  They can be represented by a fixed number of bits with a fixed layout, and typically are in
    most representations.  Integers and floating-point numbers (of all bit widths) are primitives.  Enums, however, are
    not, since they have additional constraints on their values, though their underlying representation can be a
    primitive integer.
    """

    bit_width: int


@dataclass
class PrimitiveBuiltinSerializable(PrimitiveType, SerializableBuiltin):
    """A primitive type that is serializable."""


@dataclass
class IntegerPrimitiveType(PrimitiveType):
    """Primitive integer type."""

    signed: bool

    @override
    def satisfies(self, constraint: typesys.NumericType) -> bool:
        """Check if this type satisfies a constraint.

        At present, we have a very simple constraint system that is special-cased for integer and floating point
        numbers.
        """
        if self.signed:
            return constraint in (
                typesys.NumericType.INTEGER,
                typesys.NumericType.SIGNED_INTEGER,
                typesys.NumericType.NONE,
            )
        return constraint in (typesys.NumericType.INTEGER, typesys.NumericType.NONE)


@dataclass
class IntegerPrimitiveBuiltinSerializable(IntegerPrimitiveType, SerializableBuiltin):
    """A primitive integer type that is serializable."""


@dataclass
class FloatingPointPrimitiveType(PrimitiveType):
    """Floating point primitive type."""

    @override
    def satisfies(self, constraint: typesys.NumericType) -> bool:
        """Check if this type satisfies a constraint.

        At present, we have a very simple constraint system that is special-cased for integer and floating point
        numbers.
        """
        return constraint in (typesys.NumericType.FLOAT, typesys.NumericType.NONE)


@dataclass
class FloatingPointPrimitiveBuiltinSerializable(FloatingPointPrimitiveType, SerializableBuiltin):
    """A floating point primitive type that is serializable."""


BUILTINS_SCOPE: Final = node.Scope(parent=None, uniq_path="", module_id_for_errors=None)
BUILTINS_MODULE: Final = node.Module(
    doc=None,
    module_id=ModuleID("", ""),
    inner_scope=BUILTINS_SCOPE,
    terminals=None,
    cst_node=None,
    unresolved_imports=[],
    context=compiler_context.CompilerContext(),
)


TYPE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="Type", type_info=cast("typesys.TypeVal", None))
TYPE_TYPE.type_info = TYPE_TYPE

INFERRED_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="Inferred", type_info=TYPE_TYPE)

INT64: Final = IntegerPrimitiveBuiltinSerializable(
    scope=BUILTINS_SCOPE, name="Int64", bit_width=64, signed=True, type_info=TYPE_TYPE
)
INT32: Final = IntegerPrimitiveBuiltinSerializable(
    scope=BUILTINS_SCOPE, name="Int32", bit_width=32, signed=True, type_info=TYPE_TYPE
)
INT16: Final = IntegerPrimitiveBuiltinSerializable(
    scope=BUILTINS_SCOPE, name="Int16", bit_width=16, signed=True, type_info=TYPE_TYPE
)
INT8: Final = IntegerPrimitiveBuiltinSerializable(
    scope=BUILTINS_SCOPE, name="Int8", bit_width=8, signed=True, type_info=TYPE_TYPE
)

UINT64: Final = IntegerPrimitiveBuiltinSerializable(
    scope=BUILTINS_SCOPE,
    name="UInt64",
    bit_width=64,
    signed=False,
    type_info=TYPE_TYPE,
)
UINT32: Final = IntegerPrimitiveBuiltinSerializable(
    scope=BUILTINS_SCOPE,
    name="UInt32",
    bit_width=32,
    signed=False,
    type_info=TYPE_TYPE,
)
UINT16: Final = IntegerPrimitiveBuiltinSerializable(
    scope=BUILTINS_SCOPE,
    name="UInt16",
    bit_width=16,
    signed=False,
    type_info=TYPE_TYPE,
)
UINT8: Final = IntegerPrimitiveBuiltinSerializable(
    scope=BUILTINS_SCOPE, name="UInt8", bit_width=8, signed=False, type_info=TYPE_TYPE
)

BOOL: Final = PrimitiveBuiltinSerializable(scope=BUILTINS_SCOPE, name="Bool", bit_width=8, type_info=TYPE_TYPE)

BYTE: Final = PrimitiveBuiltinSerializable(scope=BUILTINS_SCOPE, name="Byte", bit_width=8, type_info=TYPE_TYPE)

FLOAT32: Final = FloatingPointPrimitiveBuiltinSerializable(
    scope=BUILTINS_SCOPE, name="Float32", bit_width=32, type_info=TYPE_TYPE
)
FLOAT64: Final = FloatingPointPrimitiveBuiltinSerializable(
    scope=BUILTINS_SCOPE, name="Float64", bit_width=64, type_info=TYPE_TYPE
)

DURATION: Final = BuiltinSerializableTypeDef(scope=BUILTINS_SCOPE, name="Duration", type_info=TYPE_TYPE)
SYNC_TIME: Final = BuiltinSerializableTypeDef(scope=BUILTINS_SCOPE, name="SyncTime", type_info=TYPE_TYPE)

BYTES: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="Bytes", type_info=TYPE_TYPE)
BITS: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="Bits", type_info=TYPE_TYPE)

FIXED_ARRAY: Final = BuiltinSerializableGenericTypeDef(
    scope=BUILTINS_SCOPE,
    name="FixedArray",
    parameters=(
        typesys.Parameter(name="type", type_bound=TYPE_TYPE, default=None),
        typesys.Parameter(name="size", type_bound=UINT64, default=None),
    ),
    type_info=TYPE_TYPE,
)

VAR_ARRAY: Final = BuiltinSerializableGenericTypeDef(
    scope=BUILTINS_SCOPE,
    name="VarArray",
    parameters=(
        typesys.Parameter(name="type", type_bound=TYPE_TYPE, default=None),
        typesys.Parameter(name="max_size", type_bound=UINT64, default=None),
    ),
    type_info=TYPE_TYPE,
)


VAR_STRING: Final = BuiltinSerializableGenericTypeDef(
    scope=BUILTINS_SCOPE,
    name="VarString",
    parameters=(typesys.Parameter(name="max_size", type_bound=UINT64, default=None),),
    type_info=TYPE_TYPE,
)

# NB: This is not supported for serialization in Tachyon.  It's used internally in the DSL for policy classes.
STRING: Final = typesys.TypeDef(
    scope=BUILTINS_SCOPE,
    name="String",
    type_info=TYPE_TYPE,
)

IPV4_ADDRESS: Final = typesys.TypeDef(
    scope=BUILTINS_SCOPE,
    name="IPv4Address",
    type_info=TYPE_TYPE,
)

OPTIONAL: Final = BuiltinSerializableGenericTypeDef(
    scope=BUILTINS_SCOPE,
    name="Optional",
    parameters=(typesys.Parameter(name="type", type_bound=TYPE_TYPE, default=None),),
    type_info=TYPE_TYPE,
)


@dataclass
class MagicValue(typesys.NamedValue):
    """A value that instantiates a new entity through a factory function when used.

    This is used to have special sentinel values that appear to be a single
    thing in the DSL source but in fact become a new entity each time they are
    used.  This is useful for things like nullopt, which must be type-flexible,
    with each instance taking on a different type through type inference.
    """

    factory: Callable[[MagicValue], typesys.Value]


class Nullopt(typesys.ObjectIdentityValue):
    """Represents instances of the nullopt magic value."""


NULLOPT_VALUE: Final = MagicValue(
    scope=BUILTINS_SCOPE, name="nullopt", type_info=TYPE_TYPE, factory=lambda _: Nullopt(type_info=INFERRED_TYPE)
)

POD: Final = typesys.GenericTypeDef(
    scope=BUILTINS_SCOPE,
    name="Pod",
    parameters=(typesys.Parameter(name="schema", type_bound=TYPE_TYPE, default=None),),
    type_info=TYPE_TYPE,
)


TAP: Final = typesys.GenericTypeDef(
    scope=BUILTINS_SCOPE,
    name="Tap",
    parameters=(typesys.Parameter(name="representation", type_bound=TYPE_TYPE, default=None),),
    type_info=TYPE_TYPE,
)


TAP_INIT: Final = typesys.GenericTypeDef(
    scope=BUILTINS_SCOPE,
    name="TapInit",
    parameters=(typesys.Parameter(name="representation", type_bound=TYPE_TYPE, default=None),),
    type_info=TYPE_TYPE,
)


PROTOBUF: Final = typesys.GenericTypeDef(
    scope=BUILTINS_SCOPE,
    name="Protobuf",
    parameters=(typesys.Parameter(name="schema", type_bound=TYPE_TYPE, default=None),),
    type_info=TYPE_TYPE,
)

TACHYON: Final = typesys.GenericTypeDef(
    scope=BUILTINS_SCOPE,
    name="Tachyon",
    parameters=(typesys.Parameter(name="schema", type_bound=TYPE_TYPE, default=None),),
    type_info=TYPE_TYPE,
)


TAPPY: Final = typesys.GenericTypeDef(
    scope=BUILTINS_SCOPE,
    name="Tappy",
    parameters=(typesys.Parameter(name="schema", type_bound=TYPE_TYPE, default=None),),
    type_info=TYPE_TYPE,
)

PROTOBUF_TO_TAP: Final = typesys.GenericTypeDef(
    scope=BUILTINS_SCOPE,
    name="ProtobufToTap",
    parameters=(
        typesys.Parameter(name="source", type_bound=TYPE_TYPE, default=None),
        typesys.Parameter(name="destination", type_bound=TYPE_TYPE, default=None),
    ),
    type_info=TYPE_TYPE,
)

TAP_TO_PROTOBUF: Final = typesys.GenericTypeDef(
    scope=BUILTINS_SCOPE,
    name="TapToProtobuf",
    parameters=(
        typesys.Parameter(name="source", type_bound=TYPE_TYPE, default=None),
        typesys.Parameter(name="destination", type_bound=TYPE_TYPE, default=None),
    ),
    type_info=TYPE_TYPE,
)

UUID: Final = BuiltinSerializableGenericTypeDef(
    scope=BUILTINS_SCOPE,
    name="Uuid",
    parameters=(typesys.Parameter(name="tag", type_bound=TYPE_TYPE, default=None),),
    type_info=TYPE_TYPE,
)

FALSE_VALUE: Final = typesys.NamedValue(scope=BUILTINS_SCOPE, name="false", type_info=BOOL)
TRUE_VALUE: Final = typesys.NamedValue(scope=BUILTINS_SCOPE, name="true", type_info=BOOL)

#
# Internal Types
#
# These types are not directly exposed to users within the DSL (i.e., you can't
# declare a schema field or parameter of these types), but are used within the
# compiler as the types of DSL constructs.
#
# Keep this list alphabetically sorted to reduce merge conflicts.
AUDIO_SOURCE_INSTANCE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="AudioSource", type_info=TYPE_TYPE)
BOX_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="Box", type_info=TYPE_TYPE)
CHANNEL_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="Channel", type_info=TYPE_TYPE)
COG_CONDITION_INSTANCE_TYPE: Final = typesys.TypeDef(
    scope=BUILTINS_SCOPE, name="ConditionInstance", type_info=TYPE_TYPE
)
COG_CONDITION_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="Condition", type_info=TYPE_TYPE)
COG_RESOURCE_INSTANCE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="ResourceInstance", type_info=TYPE_TYPE)
COG_RESOURCE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="Resource", type_info=TYPE_TYPE)
COG_CONFIG_INSTANCE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="ConfigInstance", type_info=TYPE_TYPE)
COG_CONFIG_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="Config", type_info=TYPE_TYPE)
COG_STATE_INSTANCE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="StateInstance", type_info=TYPE_TYPE)
COG_STATE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="State", type_info=TYPE_TYPE)
COG_INPUT_INSTANCE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="InputInstance", type_info=TYPE_TYPE)
COG_INPUT_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="Input", type_info=TYPE_TYPE)
COG_INSTANCE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="CogInstance", type_info=TYPE_TYPE)
COG_OUTPUT_INSTANCE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="OutputInstance", type_info=TYPE_TYPE)
COG_METRICS_OUTPUT_INSTANCE_TYPE: Final = typesys.TypeDef(
    scope=BUILTINS_SCOPE, name="MetricsOutputInstance", type_info=TYPE_TYPE
)
COG_OUTPUT_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="Output", type_info=TYPE_TYPE)
COG_METRICS_OUTPUT_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="MetricsOutput", type_info=TYPE_TYPE)
COG_DIAGNOSTICS_INSTANCE_TYPE: Final = typesys.TypeDef(
    scope=BUILTINS_SCOPE, name="CogDiagnosticsInstance", type_info=TYPE_TYPE
)
CONFIG_INSTANCE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="ConfigInstance", type_info=TYPE_TYPE)
CPU_DOMAIN_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="CpuDomain", type_info=TYPE_TYPE)
DIAGNOSTICS_INSTANCE_TYPE: Final = typesys.TypeDef(
    scope=BUILTINS_SCOPE, name="DiagnosticsInstance", type_info=TYPE_TYPE
)
ENUM_TAG_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="EnumTag", type_info=TYPE_TYPE)
ETHERNET_LAN_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="EthernetLan", type_info=TYPE_TYPE)
EXECUTABLE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="Executable", type_info=TYPE_TYPE)
MEMORY_RESOURCE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="MemoryResource", type_info=TYPE_TYPE)
POLICY_INSTANCE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="PolicyInstance", type_info=TYPE_TYPE)
POLICY_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="Policy", type_info=TYPE_TYPE)
PROCESS_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="Process", type_info=TYPE_TYPE)
SCHEMA_TAG_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="SchemaTag", type_info=TYPE_TYPE)
SYSTEM_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="System", type_info=TYPE_TYPE)
STATE_INSTANCE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="StateInstance", type_info=TYPE_TYPE)
REPRESENTATION_TAG_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="RepresentationTag", type_info=TYPE_TYPE)
UDP_SOCKET_INSTANCE_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="UdpSocketInstance", type_info=TYPE_TYPE)
UDP_SOCKET_ENDPOINT_TYPE: Final = typesys.TypeDef(scope=BUILTINS_SCOPE, name="UdpSocketEndpoint", type_info=TYPE_TYPE)
UDP_SOCKET_ENDPOINT_INSTANCE_TYPE: Final = typesys.TypeDef(
    scope=BUILTINS_SCOPE, name="UdpSocketEndpointInstance", type_info=TYPE_TYPE
)

for typ in (
    BITS,
    BOOL,
    BYTE,
    BYTES,
    CHANNEL_TYPE,
    COG_INSTANCE_TYPE,
    DURATION,
    FALSE_VALUE,
    FIXED_ARRAY,
    FLOAT32,
    FLOAT64,
    INT16,
    INT32,
    INT64,
    INT8,
    OPTIONAL,
    NULLOPT_VALUE,
    POD,
    PROTOBUF,
    STRING,
    SYNC_TIME,
    TACHYON,
    TAP,
    TAPPY,
    TAP_TO_PROTOBUF,
    PROTOBUF_TO_TAP,
    TRUE_VALUE,
    TYPE_TYPE,
    UINT16,
    UINT32,
    UINT64,
    UINT8,
    UUID,
    VAR_ARRAY,
    VAR_STRING,
    SCHEMA_TAG_TYPE,
    REPRESENTATION_TAG_TYPE,
):
    BUILTINS_SCOPE.define(typ.name, typ, None)
