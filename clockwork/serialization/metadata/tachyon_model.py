# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Data model for metadata for Tachyon representations.

This module contains a dataclass-based data model for describing Tachyon
representations of Clockwork schemas.  This is not the serialized form of the
metadata, but most code dealing with Tachyon metadata should be written against
this data model rather than against the underlying serialized form.
"""

from __future__ import annotations

import enum
from dataclasses import dataclass, field
from hashlib import md5
from typing import TYPE_CHECKING, Final, TypeAlias

if TYPE_CHECKING:
    from collections.abc import Sequence
    from decimal import Decimal
    from uuid import UUID


ClkType: TypeAlias = "BuiltInType | SchemaType | ClkEnumType | TagType | StrongType"

InitialValue: TypeAlias = "SignedInitialValue | UnsignedInitialValue | FloatInitialValue | BoolInitialValue"

BUILT_IN_UUID_ADDED_IN_VERSION: Final = 2
ENUM_UNDERLYING_TYPE_ADDED_IN_VERSION: Final = 3


@dataclass(slots=True)
class TachyonMetadata:
    """Metadata for a collection of types that describe a Tachyon representation.

    Types are stored in a flat sequence.  Each type can then be identified by a
    single integer, which is an index into this sequence.

    Note that generic types, whether built-in or schema types, will have a
    separate entry in the metadata for each instantiation that's used in this
    representation.

    Attributes:
        outer_type_id: Identifier (index) of the top-level type in this representation.
        types: Metadata for all types used within this representation.
        version: Metadata version
        python_required: Set to True for force schema upgrade in python
    """

    outer_type_id: int
    types: Sequence[ClkType]
    version: int = ENUM_UNDERLYING_TYPE_ADDED_IN_VERSION
    python_required: bool = False

    def _is_legacy_wire_compatible(self, other: TachyonMetadata) -> bool:
        """Wire compatibilty check for metadata from before the schema UUID was added to the protobuf."""
        if self.outer_type_id != other.outer_type_id or len(self.types) != len(other.types):
            return False

        for self_type, other_type in zip(self.types, other.types, strict=True):
            if isinstance(self_type, BuiltInType):
                if not isinstance(other_type, BuiltInType):
                    return False
                if not self_type.is_wire_compatible(other_type, ignore_uuid=True):
                    return False
            elif self_type != other_type:
                return False

        return True

    def is_wire_compatible(self, other: TachyonMetadata) -> bool:
        """Check if two metadata instances are wire-compatible.

        Wire compatibility means they can serialize/deserialize the same layout
        even if the metadata versions (that is, the version of TachyonMetadata
        itself, not the version of contained schemas) are different. This will
        also return true if the fully-qualified names of some entities have
        changed but their physical representation remains the same.

        Args:
            other: Another TachyonMetadata instance to check compatibility with

        Returns:
            True if the metadata are wire-compatible, False otherwise
        """
        # If versions indicate comparable hashes, just compare hashes
        if self.version == other.version:
            return self.types[self.outer_type_id].get_hash(self.types) == other.types[other.outer_type_id].get_hash(
                other.types
            )

        if min(self.version, other.version) >= BUILT_IN_UUID_ADDED_IN_VERSION:
            return self.types[self.outer_type_id].get_hash(self.types, force_recompute=True) == other.types[
                other.outer_type_id
            ].get_hash(other.types, force_recompute=True)

        return self._is_legacy_wire_compatible(other)

    def check_for_unexpected_schema_changes(self, incoming: TachyonMetadata) -> None:
        """Check for unexpected schema changes that are not reflected in the schema history.

        Args:
            incoming: Tachyon metadata this schema will be upgraded from.

        Raises:
            TypeError or ValueError if unexpected schema changes are found.
        """
        incoming_outer_type = incoming.types[incoming.outer_type_id]
        self_outer_type = self.types[self.outer_type_id]

        if not isinstance(self_outer_type, SchemaType):
            msg = f"Expected outer type to be a schema for {self_outer_type.fqn}"
            raise TypeError(msg)

        self_outer_type.check_for_unexpected_schema_changes(self.types, incoming_outer_type, incoming.types)


@dataclass(slots=True)
class BuiltInType:
    """Metadata for a built-in Clockwork type.

    The internal layout of built-in types are not precisely described, though
    the size and alignment are described, so parsers can skip over unrecognized
    types.

    Arguments can be for either type parameters or non-type (value) parameters.
    To make sense of these arguments, parsers must know already the expected
    parameters.  If the parser recognizes the built-in type, then it will also
    understand the parameters; if it doesn't recognize the type, then it can
    only skip over the type anyway.

    Arguments for type parameters will be a type ID (index).  Arguments for
    non-type parameters will be the canonical string representation of the
    value.  Even integer values are encoded as a string; this allows values with
    type int to be unambiguously interpreted as type IDs rather than non-type
    values.

    Attributes:
        fqn: The fully-qualified name of this type, not including any arguments
        size: Size of this type on the wire, in bytes
        alignment: Alignment of this type in bytes (informational; not needed to parse)
        arguments: Arguments that this type is instantiated with (or empty if non-generic)
        hash: An opaque, unique identifier for this type
    """

    fqn: str = field(compare=False)
    uuid: UUID
    size: int
    alignment: int
    arguments: Sequence[int | str]
    hash: bytes | None = field(default=None, compare=False)

    def is_wire_compatible(self, other: BuiltInType, ignore_uuid: bool) -> bool:
        """Check if two built-in types are wire-compatible.

        This is a way of comparing BuiltInType instances that handles metadata
        changes over time, specifically the fact that older metadata does not
        have UUIDs for built-in types.

        Args:
            other: Another BuiltInType to check compatibility with
            ignore_uuid: If True, UUID fields will be excluded from comparison,
                         which is needed when comparing types from metadata with
                         version < BUILT_IN_UUID_ADDED_IN_VERSION

        Returns:
            True if the types are wire-compatible, False otherwise
        """
        if ignore_uuid:
            return self.size == other.size and self.alignment == other.alignment and self.arguments == other.arguments
        return self == other

    def get_hash(self, types: Sequence[ClkType], force_recompute: bool = False) -> bytes:
        """Return the hash of this schema, calculating it if needed."""
        if self.hash is None or force_recompute:
            result = md5(self.uuid.bytes)  # noqa: S324  (md5 not used for security)
            # UUID tag type does not participate in hashing (and can cause recursion)
            if self.fqn != ".Uuid":
                for arg in self.arguments:
                    if isinstance(arg, int):
                        result.update(types[arg].get_hash(types, force_recompute))
                    else:
                        result.update(arg.encode("utf-8"))
            self.hash = result.digest()
        return self.hash

    def check_for_unexpected_schema_changes(
        self, self_types: Sequence[ClkType], incoming: ClkType, incoming_types: Sequence[ClkType], name: str = ""
    ) -> None:
        """Check for unexpected schema changes.

        Validate that the fqn and string arguments all match and there are no unexpected changes in the
        type arguments.

        Args:
            self_types: Self tachyon metadata types.
            incoming: Schema type being upgraded to this type.
            incoming_types: Incoming tachyon metadata types.
            name: Name to use in exception strings.

        Raises:
            TypeError or ValueError if unexpected schema changes are found.
        """
        if not name:
            name = self.fqn

        if not isinstance(incoming, BuiltInType):
            msg = f"Unsupported field type change for {name}"
            raise TypeError(msg)

        if self.fqn != incoming.fqn:
            msg = f"Unsupported field type change for {name}: {self.fqn} -> {incoming.fqn}"
            raise ValueError(msg)

        if len(self.arguments) != len(incoming.arguments):
            msg = f"Incompatible arguments for {name}"
            raise ValueError(msg)

        for self_arg, incoming_arg in zip(self.arguments, incoming.arguments, strict=True):
            if isinstance(self_arg, str):
                if not isinstance(incoming_arg, str) or self_arg != incoming_arg:
                    msg = f"Unsupported parameter change for {name}: {self_arg} -> {incoming_arg}"
                    raise ValueError(msg)
            else:
                if not isinstance(incoming_arg, int):
                    msg = f"Unsupported parameter change for {name}: {self_arg} -> {incoming_arg}"
                    raise TypeError(msg)
                self_types[self_arg].check_for_unexpected_schema_changes(
                    self_types, incoming_types[incoming_arg], incoming_types, name
                )


@dataclass(slots=True)
class SchemaType:
    """Metadata for schema types.

    Attributes:
        fqn: The fully-qualified name of this type, not including any arguments
        size: Size of this type on the wire, in bytes
        alignment: Alignment of this type in bytes (informational; not needed to parse)
        schema_uuid: The UUID of the schema; unlike hash, this is the same for different instantiations and versions
        version: The version number of the schema
        fields: Fields in this schema
        hash: An opaque, unique identifier for this type.
        removed: Set of field numbers that have been removed
        became: Map from field number to field number in current schema
        versions: Set of historical schema versions
    """

    fqn: str = field(compare=False)
    size: int
    alignment: int
    schema_uuid: UUID
    version: int
    fields: Sequence[SchemaField]
    hash: bytes | None = field(default=None, compare=False)
    removed: set[int] = field(default_factory=set, compare=False)
    became: dict[int, int] = field(default_factory=dict, compare=False)
    versions: set[int] = field(default_factory=set, compare=False)
    init_value: InitialValue | None = field(default=None)

    def get_hash(self, types: Sequence[ClkType], force_recompute: bool = False) -> bytes:
        """Return the hash of this schema, calculating it if needed."""
        if self.hash is None or force_recompute:
            result = md5(self.schema_uuid.bytes)  # noqa: S324  (md5 not used for security)
            result.update(str(self.version).encode("utf-8"))
            for fld in self.fields:
                result.update(fld.compute_hash(types, force_recompute))
            self.hash = result.digest()
        return self.hash

    def _validate_modified_field(
        self, self_fields: dict[int, SchemaField], incoming_field: SchemaField, name: str
    ) -> None:
        """Check modified fields against the history.

        Args:
            self_fields: Fields in this type by field number.
            incoming_field: Incoming modified schema field.
            name: Name to use in exception strings.

        Raises:
            ValueError if unexpected schema changes are found.
        """
        self_field_num = incoming_field.num
        while self_field_num in self.became:
            self_field_num = self.became[self_field_num]
        if self_field_num not in self_fields and self_field_num not in self.removed:
            msg = f"Field number {self_field_num} ({incoming_field.name}) removed from {name} without updating history"
            raise ValueError(msg)

    def check_for_unexpected_schema_changes(
        self, self_types: Sequence[ClkType], incoming: ClkType, incoming_types: Sequence[ClkType], name: str = ""
    ) -> None:
        """Check for unexpected schema changes.

        If two fields have the same field number then their names should match and there should be no unexpected
        schema changes to the field types.

        Args:
            self_types: Self tachyon metadata types.
            incoming: Schema type being upgraded to this type.
            incoming_types: Incoming tachyon metadata types.
            name: Name to use in exception strings.

        Raises:
            TypeError or ValueError if unexpected schema changes are found.
        """
        if not name:
            name = self.fqn

        if not isinstance(incoming, SchemaType):
            msg = f"Expected incoming type to be a schema for {name}"
            raise TypeError(msg)

        if self.schema_uuid != incoming.schema_uuid:
            msg = f"Tachyon metadata UUID mismatch for {name}: {self.schema_uuid} != {incoming.schema_uuid}"
            raise ValueError(msg)

        if self.version < incoming.version:
            msg = f"Incoming version for {name} is beyond latest version: {incoming.version} > {self.version}"
            raise ValueError(msg)

        if self.version == incoming.version and len(self.fields) != len(incoming.fields):
            msg = f"Unsupported schema change for {name}: num fields went from {len(incoming.fields)} to {len(self.fields)}"

        self_fields = {field.num: field for field in self.fields}

        for incoming_field in incoming.fields:
            incoming_field_name = name + f".{incoming_field.name}"
            if incoming_field.num in self_fields:
                self_field = self_fields[incoming_field.num]

                if self_field.name != incoming_field.name:
                    msg = f"Unsupported field name change for {incoming_field_name}: new name is {self_field.name}"
                    raise ValueError(msg)

                self_types[self_field.type_id].check_for_unexpected_schema_changes(
                    self_types, incoming_types[incoming_field.type_id], incoming_types, incoming_field_name
                )
            elif self.version == incoming.version:
                msg = f"Unsupported field removal: {incoming_field.name} removed from {name}"
                raise ValueError(msg)
            else:
                self._validate_modified_field(self_fields, incoming_field, name)


@dataclass(slots=True)
class SchemaField:
    """Metadata for one schema field.

    Attributes:
        offset: Byte offset of this field within the schema representation
        num: Field number
        name: Field name
        type_id: ID (index) of the type of this field
        init_value: Initial field value
    """

    offset: int
    num: int
    name: str
    type_id: int
    init_value: InitialValue | None = field(default=None)

    def compute_hash(self, types: Sequence[ClkType], force_recompute: bool = False) -> bytes:
        """Compute hash of this field."""
        result = md5(str(self.num).encode("utf-8"))  # noqa: S324  (md5 not used for security)
        result.update(self.name.encode("utf-8"))
        result.update(types[self.type_id].get_hash(types, force_recompute))
        return result.digest()


@dataclass(slots=True)
class ClkEnumType:
    """Metadata for a Clockwork enum.

    Attributes:
        fqn: The fully-qualified name of this type, not including any arguments
        underlying_type_id: Type ID (index) of underlying integer type
        options: Options set for this enum
        enum_uuid: The UUID of the enum
        version: The version number of the enum
        values: Values of the enum
        hash: An opaque, unique identifier for this type.
        removed: Set of enum value numbers that have been removed
        became: Map from enum value number to current enum value number
        versions: Set of historical enum versions
    """

    class Options(enum.IntFlag):
        """Options for enum usage."""

        none = 0
        flags = 1

    fqn: str = field(compare=False)
    options: Options
    underlying_type_id: int
    enum_uuid: UUID
    version: int
    values: Sequence[EnumValue]
    hash: bytes | None = field(default=None, compare=False)
    removed: set[int] = field(default_factory=set, compare=False)
    became: dict[int, int] = field(default_factory=dict, compare=False)
    versions: set[int] = field(default_factory=set, compare=False)

    def get_hash(self, types: Sequence[ClkType], force_recompute: bool = False) -> bytes:
        """Return the hash of this object, calculating it if needed."""
        if self.hash is None or force_recompute:
            result = md5(self.enum_uuid.bytes)  # noqa: S324  (md5 not used for security)
            result.update(types[self.underlying_type_id].get_hash(types, force_recompute))
            result.update(str(int(self.options)).encode("utf-8"))
            result.update(str(self.version).encode("utf-8"))
            for val in self.values:
                result.update(val.compute_hash())
            self.hash = result.digest()
        return self.hash

    def _validate_modified_value(self, self_values: dict[int, EnumValue], incoming_value: EnumValue, name: str) -> None:
        """Check value changes against the history.

        Args:
            self_values: Values in this type by value number.
            incoming_value: Incoming modified enum value.
            name: Name to use in exception strings.

        Raises:
            ValueError if unexpected schema changes are found.
        """
        self_value_num = incoming_value.num
        while self_value_num in self.became:
            self_value_num = self.became[self_value_num]
        if self_value_num not in self_values and self_value_num not in self.removed:
            msg = f"Value number {self_value_num} ({incoming_value.name}) removed from {name} without updating history"
            raise ValueError(msg)

    def check_for_unexpected_schema_changes(
        self, _self_types: Sequence[ClkType], incoming: ClkType, _incoming_types: Sequence[ClkType], name: str = ""
    ) -> None:
        """Check for unexpected schema changes.

        Args:
            _self_types: Self tachyon metadata types.
            incoming: Schema type being upgraded to this type.
            _incoming_types: Incoming tachyon metadata types.
            name: Name to use in exception strings.

        Raises:
            TypeError or ValueError if unexpected schema changes are found.
        """
        if not name:
            name = self.fqn
        if not isinstance(incoming, ClkEnumType):
            msg = f"Unsupported type change for {name}"
            raise TypeError(msg)

        if self.version < incoming.version:
            msg = f"Incoming version for {name} is beyond latest version: {self.version} < {incoming.version}"
            raise ValueError(msg)

        if self.version == incoming.version and len(self.values) != len(incoming.values):
            msg = f"Unsupported schema change for {name}: num values went from {len(incoming.values)} to {len(self.values)}"

        self_values = {value.num: value for value in self.values}

        for incoming_value in incoming.values:
            incoming_value_name = name + f".{incoming_value.name}"
            if incoming_value.num in self_values:
                self_value = self_values[incoming_value.num]

                if self_value.name != incoming_value.name:
                    msg = f"Unsupported value name change for {incoming_value_name}: new name is {self_value.name}"
                    raise ValueError(msg)
                if self.version == incoming.version and self_value.value != incoming_value.value:
                    msg = f"Unsupported enum value change for {incoming_value_name} from {incoming_value.value} to {self_value.value}"
                    raise ValueError(msg)
            elif self.version == incoming.version:
                msg = f"Unsupported value removal: {incoming_value.name} removed from {name}"
                raise ValueError(msg)
            else:
                self._validate_modified_value(self_values, incoming_value, name)


@dataclass(slots=True)
class EnumValue:
    """One value of an enum.

    Attributes:
        num: Value number (*not* the underlying value)
        value: Underlying value
        name: Value name
    """

    num: int
    value: int
    name: str

    def compute_hash(self) -> bytes:
        """Compute hash of this value."""
        result = md5(str(self.num).encode("utf-8"))  # noqa: S324 (md5 not used for security)
        result.update(self.name.encode("utf-8"))
        result.update(str(self.value).encode("utf-8"))
        return result.digest()


# All tags have the same hash since they have no unique identifiers other than
# fqn and tags are interchangeable with respect to serialization.
TAG_HASH: Final = md5(b"<TAG>").digest()  # noqa: S324  (md5 not used for security)


@dataclass(slots=True)
class TagType:
    """Metadata for a Clockwork strong type tag.

    Attributes:
        fqn: The fully-qualified name of this type, not including any arguments
        hash: An opaque, unique identifier for this type.
    """

    fqn: str = field(compare=False)
    hash: bytes | None = field(default=None, compare=False)

    def get_hash(self, types: Sequence[ClkType], force_recompute: bool = False) -> bytes:  # noqa: ARG002
        """Return the hash of this object, calculating it if needed."""
        if self.hash is None or force_recompute:
            self.hash = TAG_HASH
        return self.hash

    def check_for_unexpected_schema_changes(
        self, _self_types: Sequence[ClkType], incoming: ClkType, _incoming_types: Sequence[ClkType], name: str = ""
    ) -> None:
        """Check for unexpected schema changes.

        This just checks that both types are tag types.

        Args:
            _self_types: Self tachyon metadata types.
            incoming: Schema type being upgraded to this type.
            _incoming_types: Incoming tachyon metadata types.
            name: Name to use in exception strings.

        Raises:
            TypeError or ValueError if unexpected schema changes are found.
        """
        if not name:
            name = self.fqn
        if not isinstance(incoming, TagType):
            msg = f"Unsupported type change for {name}"
            raise TypeError(msg)


@dataclass(slots=True)
class StrongType:
    """Metadata for a Clockwork strong type.

    Attributes:
        fqn: The fully-qualified name of this type, not including any arguments
        underlying_type_id: The underlying type
        hash: An opaque, unique identifier for this type.
    """

    fqn: str = field(compare=False)
    underlying_type_id: int
    hash: bytes | None = field(default=None, compare=False)

    def get_hash(self, types: Sequence[ClkType], force_recompute: bool = False) -> bytes:
        """Return the hash of this object, calculating it if needed."""
        if self.hash is None or force_recompute:
            # Use only the hash of the underlying type; for serialization strong
            # types are interchangeable with their underlying types.
            self.hash = types[self.underlying_type_id].get_hash(types, force_recompute)
        return self.hash

    def check_for_unexpected_schema_changes(
        self, self_types: Sequence[ClkType], incoming: ClkType, incoming_types: Sequence[ClkType], name: str = ""
    ) -> None:
        """Check for unexpected schema changes.

        The type fqn should not change without changing the history, and the underlying type should not
        have any unexpected schema changes.

        Args:
            self_types: Self tachyon metadata types.
            incoming: Schema type being upgraded to this type.
            incoming_types: Incoming tachyon metadata types.
            name: Name to use in exception strings.

        Raises:
            TypeError or ValueError if unexpected schema changes are found.
        """
        if not name:
            name = self.fqn
        if not isinstance(incoming, StrongType):
            msg = f"Unsupported type change for {name}"
            raise TypeError(msg)

        self_types[self.underlying_type_id].check_for_unexpected_schema_changes(
            self_types, incoming_types[incoming.underlying_type_id], incoming_types, name
        )


@dataclass(slots=True)
class SignedInitialValue:
    """Signed integer initial value for a schema field.

    Attributes:
        value: Initial value
    """

    value: int


@dataclass(slots=True)
class UnsignedInitialValue:
    """Unsigned integer initial value for a schema field.

    Attributes:
        value: Initial value
    """

    value: int


@dataclass(slots=True)
class FloatInitialValue:
    """Floating point initial value for a schema field.

    Attributes:
        value: Initial value
    """

    value: Decimal


@dataclass(slots=True)
class BoolInitialValue:
    """Bool initial value for a schema field.

    Attributes:
        value: Initial value
    """

    value: bool
