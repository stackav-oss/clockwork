# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Serialization and deserialization abstractions for clockwork types."""

from __future__ import annotations

import struct
from abc import ABC, abstractmethod
from typing import TYPE_CHECKING, Final, Generic, TypeVar

from typing_extensions import override

if TYPE_CHECKING:
    from jewels.memory.py_bytes import MutableBytes

ValueType = TypeVar("ValueType")


class ValueSerDes(ABC, Generic[ValueType]):
    """An abstract class for serializing / deserializing a value to / from bytes."""

    @abstractmethod
    def serialize(self, value: ValueType, buf: MutableBytes) -> None:
        """Serialize a value to a byte buffer."""

    @abstractmethod
    def deserialize(self, buf: MutableBytes) -> ValueType:
        """Deserialize a value from a byte buffer."""

    @abstractmethod
    def size_bytes(self) -> int:
        """Size of the value in bytes."""

    @abstractmethod
    def alignment(self) -> int:
        """Alignment of the value in bytes."""


class PrimitiveSerDes(ValueSerDes[ValueType], Generic[ValueType]):
    """An implementation for numeric types and booleans."""

    def __init__(self, format_spec: str, size_bytes: int) -> None:
        """Initialize the class.

        Args:
            format_spec: Format for the struct module.
            size_bytes: Size of the value in bytes.
        """
        self._format_spec = format_spec
        self._size_bytes = size_bytes

    @override
    def serialize(self, value: ValueType, buf: MutableBytes) -> None:
        """Serialize the value to a byte buffer."""
        return struct.pack_into(self._format_spec, buf, 0, value)

    @override
    def deserialize(self, buf: MutableBytes) -> ValueType:
        """Deserialize the value from bytes."""
        (value,) = struct.unpack(self._format_spec, buf)
        # pyrefly: ignore[no-any-return-explicit] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return value

    @override
    def size_bytes(self) -> int:
        """Get the size in bytes."""
        return self._size_bytes

    @override
    def alignment(self) -> int:
        """Get the alignment in bytes."""
        # Alignment for primitives is the same as the size.
        return self._size_bytes

    def format_spec(self) -> str:
        """Format specifier for struct module."""
        return self._format_spec


BoolSerDes: Final[PrimitiveSerDes[bool]] = PrimitiveSerDes("<?", 1)
Float32SerDes: Final[PrimitiveSerDes[float]] = PrimitiveSerDes("<f", 4)
Float64SerDes: Final[PrimitiveSerDes[float]] = PrimitiveSerDes("<d", 8)
Int8SerDes: Final[PrimitiveSerDes[int]] = PrimitiveSerDes("<b", 1)
Int16SerDes: Final[PrimitiveSerDes[int]] = PrimitiveSerDes("<h", 2)
Int32SerDes: Final[PrimitiveSerDes[int]] = PrimitiveSerDes("<l", 4)
Int64SerDes: Final[PrimitiveSerDes[int]] = PrimitiveSerDes("<q", 8)
UInt8SerDes: Final[PrimitiveSerDes[int]] = PrimitiveSerDes("<B", 1)
UInt16SerDes: Final[PrimitiveSerDes[int]] = PrimitiveSerDes("<H", 2)
UInt32SerDes: Final[PrimitiveSerDes[int]] = PrimitiveSerDes("<L", 4)
UInt64SerDes: Final[PrimitiveSerDes[int]] = PrimitiveSerDes("<Q", 8)
