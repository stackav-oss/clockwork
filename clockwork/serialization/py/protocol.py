# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Type protocol for Tachyon-serializable classes."""

from __future__ import annotations

from abc import abstractmethod
from typing import TYPE_CHECKING, Any, Protocol, TypeVar

if TYPE_CHECKING:
    from pathlib import Path

    from clockwork.dsl.serialization import tachyon_reg
    from clockwork.serialization.metadata import tachyon_model

T_co = TypeVar("T_co", covariant=True)


class Tachyon(Protocol[T_co]):
    """Mixin to add Tachyon serializers."""

    @abstractmethod
    def serialize_tachyon(self, buffer: memoryview) -> None:
        """Serialize as Tachyon."""

    @classmethod
    @abstractmethod
    def deserialize_tachyon(cls: type[T_co], buffer: memoryview) -> T_co:  # pyright: ignore[reportGeneralTypeIssues] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        """Deserialize from Tachyon."""

    @classmethod
    @abstractmethod
    def get_tachyon_constraint(cls: type[T_co]) -> tachyon_reg.FieldConstraint:  # pyright: ignore[reportGeneralTypeIssues] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        """Deserialize from Tachyon."""

    @classmethod
    @abstractmethod
    def get_tachyon_metadata_name(cls: type[T_co]) -> str | None:  # pyright: ignore[reportGeneralTypeIssues] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        """Get name tachyon metadata type name for the class."""

    @classmethod
    @abstractmethod
    def get_tachyon_metadata(cls: type[T_co]) -> tachyon_model.TachyonMetadata | None:  # pyright: ignore[reportGeneralTypeIssues] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        """Get the tachyon metadata for the class."""

    @classmethod
    @abstractmethod
    def get_tachyon_module_name(cls: type[T_co]) -> str:  # pyright: ignore[reportGeneralTypeIssues] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        """Get the tachyon source module name for the class."""

    @classmethod
    @abstractmethod
    def get_tachyon_source_file_name(cls: type[T_co]) -> str:  # pyright: ignore[reportGeneralTypeIssues] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        """Get the tachyon source file name for the class."""

    @classmethod
    @abstractmethod
    def get_tachyon_class_name(cls: type[T_co]) -> str:  # pyright: ignore[reportGeneralTypeIssues] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        """Get the tachyon class name for the class."""


def write_tachyon_to_file(obj: Tachyon[Any], filename: Path, mode: str = "wb") -> None:
    """Write a Tachyon object to a file."""
    buffer = bytearray(obj.get_tachyon_constraint().size)
    obj.serialize_tachyon(memoryview(buffer))
    with filename.open(mode) as outf:
        outf.write(buffer)


TachyClass = TypeVar("TachyClass", bound=Tachyon[Any])


def read_tachyon_from_file(tachyon_class: type[TachyClass], filename: Path) -> TachyClass:
    """Read a Tachyon object from a file."""
    with filename.open("rb") as inf:
        buffer = inf.read()
    if (bufsz := len(buffer)) != (tachysz := tachyon_class.get_tachyon_constraint().size):
        msg = f"Unexpected Tachyon file size: {bufsz} != {tachysz} in file {filename}"
        raise ValueError(msg)
    result = tachyon_class.deserialize_tachyon(memoryview(buffer))
    assert isinstance(result, tachyon_class)
    return result
