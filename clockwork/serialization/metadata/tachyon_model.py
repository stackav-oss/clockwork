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
    from uuid import UUID


ClkType: TypeAlias = "BuiltInType | SchemaType | ClkEnumType | TagType | StrongType"

BUILT_IN_UUID_ADDED_IN_VERSION: Final = 2


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
    """

    outer_type_id: int
    types: Sequence[ClkType]
    version: int = 2

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
        if self.version == other.version or min(self.version, other.version) >= BUILT_IN_UUID_ADDED_IN_VERSION:
            return self.types[self.outer_type_id].get_hash(self.types) == other.types[other.outer_type_id].get_hash(
                other.types
            )

        # Custom comparison passing version context to handle BuiltInType.uuid conditionally
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
    """

    fqn: str = field(compare=False)
    size: int
    alignment: int
    schema_uuid: UUID
    version: int
    fields: Sequence[SchemaField]
    hash: bytes | None = field(default=None, compare=False)

    def get_hash(self, types: Sequence[ClkType], force_recompute: bool = False) -> bytes:
        """Return the hash of this schema, calculating it if needed."""
        if self.hash is None or force_recompute:
            result = md5(self.schema_uuid.bytes)  # noqa: S324  (md5 not used for security)
            result.update(str(self.version).encode("utf-8"))
            for fld in self.fields:
                result.update(fld.compute_hash(types, force_recompute))
            self.hash = result.digest()
        return self.hash


@dataclass(slots=True)
class SchemaField:
    """Metadata for one schema field.

    Attributes:
        offset: Byte offset of this field within the schema representation
        num: Field number
        name: Field name
        type_id: ID (index) of the type of this field
    """

    offset: int
    num: int
    name: str
    type_id: int

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

    def get_hash(self, types: Sequence[ClkType], force_recompute: bool = False) -> bytes:  # noqa: ARG002
        """Return the hash of this object, calculating it if needed."""
        if self.hash is None or force_recompute:
            result = md5(self.enum_uuid.bytes)  # noqa: S324  (md5 not used for security)
            result.update(str(int(self.options)).encode("utf-8"))
            result.update(str(self.version).encode("utf-8"))
            for val in self.values:
                result.update(val.compute_hash())
            self.hash = result.digest()
        return self.hash


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
