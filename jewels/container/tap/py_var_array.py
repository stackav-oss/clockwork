# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python class that represents a VarArray with the Tachyon layout."""

from __future__ import annotations

from typing import TYPE_CHECKING, Final, Generic, TypeVar

from jewels.container.tap import value_serdes
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Iterable, Iterator

    from jewels.memory.py_bytes import MutableBytes

ValueType = TypeVar("ValueType")

_SIZE_T_SIZE_BYTES: Final = 8
"""Size in bytes for a size_t."""

_SIZE_T_ALIGNMENT_BYTES: Final = 8
"""Alignment in bytes for a size_t."""


def var_array_between_padding(value_size: int, capacity: int) -> int:
    """Size of padding between array of elements and the size."""
    data_size = value_size * capacity
    remainder = data_size % _SIZE_T_ALIGNMENT_BYTES
    if remainder == 0:
        return 0
    return _SIZE_T_SIZE_BYTES - remainder


def var_array_trailing_padding(value_alignment: int) -> int:
    """Size of padding after the size."""
    if value_alignment > _SIZE_T_ALIGNMENT_BYTES:
        return value_alignment - _SIZE_T_ALIGNMENT_BYTES
    return 0


def var_array_size_offset(value_size_bytes: int, capacity: int) -> int:
    """Offset in bytes to the size value."""
    between_padding = var_array_between_padding(value_size_bytes, capacity)
    return value_size_bytes * capacity + between_padding


def var_array_size_bytes(value_size_bytes: int, value_alignment_bytes: int, capacity: int) -> int:
    """Size of a VarArray buffer in bytes."""
    return (
        var_array_size_offset(value_size_bytes, capacity)
        + _SIZE_T_SIZE_BYTES
        + var_array_trailing_padding(value_alignment_bytes)
    )


class VarArray(Generic[ValueType]):  # noqa: PLW1641 Intentionally leaving out __hash__ because this is a mutable type.
    """A python interface for the clockwork VarArray type obeying the Tachyon layout."""

    def __init__(self, capacity: int, serdes: value_serdes.ValueSerDes[ValueType]) -> None:
        """Initialize.

        Args:
            buf: The underlying byte buffer.
            capacity: The maximum size.
            serdes: Handles serializing and deserializing the values.
        """
        self._serdes = serdes
        self._capacity = capacity
        self._size_offset = var_array_size_offset(serdes.size_bytes(), capacity)
        expected_bytes = var_array_size_bytes(
            value_size_bytes=self._serdes.size_bytes(),
            value_alignment_bytes=self._serdes.alignment(),
            capacity=self._capacity,
        )
        self._buf = bytearray(expected_bytes)

    def _validate_buffer(self) -> None:
        """Validate the size of the buffer and the current size value."""
        expected_bytes = var_array_size_bytes(
            value_size_bytes=self._serdes.size_bytes(),
            value_alignment_bytes=self._serdes.alignment(),
            capacity=self._capacity,
        )

        if len(self._buf) != expected_bytes:
            msg = f"For a capacity of {self._capacity} and value constraints (size: {self._serdes.size_bytes()}, alignment: {self._serdes.alignment()}), the expected buffer size is {expected_bytes} bytes, but received {len(self._buf)} bytes."
            raise ValueError(msg)

        if len(self) > self._capacity:
            msg = f"Cannot construct a VarArray with a preset size ({len(self)}) larger than the capacity ({self._capacity})."
            raise ValueError(msg)

    @classmethod
    def from_buffer(
        cls: type[VarArray[ValueType]],
        buf: MutableBytes,
        capacity: int,
        serdes: value_serdes.ValueSerDes[ValueType],
    ) -> VarArray[ValueType]:
        """Initialize.

        Args:
            buf: The underlying byte buffer.
            capacity: The maximum size.
            serdes: Handles serializing and deserializing the values.
        """
        # pyrefly: ignore[bad-argument-count, bad-specialization] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        obj = cls.__new__(cls)
        obj._serdes = serdes  # noqa: SLF001 required to initialize the class without calling __init__
        obj._capacity = capacity  # noqa: SLF001 required to initialize the class without calling __init__
        obj._size_offset = var_array_size_offset(serdes.size_bytes(), capacity)  # noqa: SLF001 required to initialize the class without calling __init__
        # pyrefly: ignore[bad-assignment] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        obj._buf = buf  # noqa: SLF001 required to initialize the class without calling __init__

        obj._validate_buffer()  # noqa: SLF001 this should only ever be called by class factory functions
        return obj

    def _write_size(self, size: int) -> None:
        """Write new size to the buffer."""
        value_serdes.UInt64SerDes.serialize(
            size, memoryview(self._buf)[self._size_offset : (self._size_offset + _SIZE_T_SIZE_BYTES)]
        )

    def _value_memory_view(self, index: int) -> memoryview:
        """Get the memory view for the value at the index."""
        start = index * self._serdes.size_bytes()
        end = (index + 1) * self._serdes.size_bytes()
        return memoryview(self._buf)[start:end]

    def __getitem__(self, index: int) -> ValueType:
        """Deserialize a value at an index."""
        current_size = len(self)
        if index < 0:
            index += current_size

        if index < 0 or index >= current_size:
            msg = f"Tried to get element {index} of VarArray with size {current_size}."
            raise IndexError(msg)
        return self._serdes.deserialize(self._value_memory_view(index))

    def __setitem__(self, index: int, value: ValueType) -> None:
        """Serialize a value to an index."""
        current_size = len(self)
        if index < 0:
            index += current_size

        if index < 0 or index >= current_size:
            msg = f"Tried to set element {index} of VarArray with size {current_size}."
            raise IndexError(msg)
        return self._serdes.serialize(value, self._value_memory_view(index))

    def append(self, value: ValueType) -> None:
        """Append a new value."""
        current_size = len(self)
        if current_size < self._capacity:
            self._write_size(current_size + 1)
            self[current_size] = value
            return
        msg = f"VarArray has insufficient capacity ({self._capacity}) to add an extra element."
        raise ValueError(msg)

    def extend(self, iterable: Iterable[ValueType]) -> None:
        """Append all values from an iterable."""
        for value in iterable:
            self.append(value)

    @property
    def capacity(self) -> int:
        """Get the capacity of the array."""
        return self._capacity

    def __len__(self) -> int:
        """Current length of the array."""
        return value_serdes.UInt64SerDes.deserialize(
            memoryview(self._buf)[self._size_offset : (self._size_offset + _SIZE_T_SIZE_BYTES)]
        )

    def __iter__(self) -> Iterator[ValueType]:
        """An iterable over the range of elements."""
        for index in range(len(self)):
            yield self[index]

    @override
    def __eq__(self, other: object) -> bool:
        """Check for equality of two VarArray s.

        VarArrays are considered equal if they have the same number of
        elements and all elements are equal.  That means that
        VarArrays with different element types can be equal, as long
        as the python representation of their elements compare
        equal. For instance, VarArray[UInt8] and VarArray[Int32] could
        be equal because UInt8 and Int32 are both represented as
        python ints.
        """
        if not isinstance(other, VarArray):
            return False

        if len(self) != len(other):
            return False
        return all(lhs == rhs for lhs, rhs in zip(self, other, strict=False))

    def __copy__(self) -> VarArray[ValueType]:
        """Copy the array."""
        return VarArray.from_buffer(bytearray(self._buf), self._capacity, self._serdes)

    def pop(self) -> ValueType:
        """Pop an element and return it if not empty.

        The bytes for the removed element are zero'd.  This is
        necessary to guarantee that the buffer remains unchanged after
        appending an element and immediately popping that element.
        This is required for consistency of serialized data (e.g., in
        logs).
        """
        current_size = len(self)
        if current_size == 0:
            msg = "Cannot pop from an empty VarArray."
            raise IndexError(msg)
        value = self[-1]
        self._value_memory_view(current_size - 1)[:] = bytes(self._serdes.size_bytes())
        self._write_size(current_size - 1)
        return value

    def clear(self) -> None:
        """Remove all elements.

        The bytes for the removed elements are zero'd.  This is
        necessary to guarantee that the buffer remains unchanged after
        appending an element and immediately popping that element.
        This is required for consistency of serialized data (e.g., in
        logs).
        """
        current_size = len(self)
        if current_size == 0:
            return
        end = current_size * self._serdes.size_bytes()
        self._buf[:end] = bytes(end)
        self._write_size(0)


class VarArraySerDes(value_serdes.ValueSerDes[VarArray[ValueType]]):
    """An implementation when the value type is a VarArray."""

    def __init__(self, capacity: int, value_serdes: value_serdes.ValueSerDes[ValueType]) -> None:
        """Initialize."""
        self._capacity = capacity
        self._value_serdes = value_serdes
        self._size_bytes = var_array_size_bytes(
            self._value_serdes.size_bytes(), self._value_serdes.alignment(), self._capacity
        )
        self._alignment = max(self._value_serdes.alignment(), _SIZE_T_ALIGNMENT_BYTES)

    @override
    def serialize(self, value: VarArray[ValueType], buf: MutableBytes) -> None:
        """Serialize the value to a byte buffer."""
        buf[:] = value._buf  # pyright: ignore[reportPrivateUsage] # noqa: SLF001  Part of the same library.

    @override
    def deserialize(self, buf: MutableBytes) -> VarArray[ValueType]:
        """Deserialize the value from bytes."""
        return VarArray.from_buffer(buf, self._capacity, self._value_serdes)

    @override
    def size_bytes(self) -> int:
        """Get the size in bytes."""
        return self._size_bytes

    @override
    def alignment(self) -> int:
        """Get the alignment in bytes."""
        return self._alignment
