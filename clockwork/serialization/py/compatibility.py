# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Functions for handling data compatibility and upgrade."""

from __future__ import annotations

from typing import TYPE_CHECKING, Any, TypeVar, cast

from clockwork.serialization.cpp.nb_tachyon_upgrader import TachyonCppUpgrader
from clockwork.serialization.metadata import tachyon as tachyon_metadata
from clockwork.serialization.metadata import tachyon_model
from clockwork.serialization.py.common import validate_tachyon_types_compatibility
from clockwork.serialization.py.protocol import Tachyon
from clockwork.serialization.py.tachyon_dyn import create_upgrade_plan, upgrade_schema
from clockwork.serialization.py.tachyon_dyn_from_metadata import py_type_from_metadata

if TYPE_CHECKING:
    from collections.abc import Callable

    from clockwork.dsl.compiler_context import CompilerContext
    from clockwork.dsl.ir.schema import InstantiatedSchema

T = TypeVar("T", bound=Tachyon[Any])


def create_deserializer_with_cpp_upgrader(
    expected_class: type[Tachyon[T]],
    incoming_metadata: tachyon_model.TachyonMetadata,
) -> tuple[bool, Callable[[memoryview], Tachyon[T]]]:
    """Create a deserializer function for that uses the C++ tachyon upgrader to handle schema version compatibility.

    This function creates a deserializer that takes a memoryview containing serialized data
    matching the incoming_metadata format, and returns an instance of the expected class.
    If the incoming data is from an older schema version, it automatically upgrades
    the data to the current version.

    Args:
        expected_class: The target class that should be produced (must be tachyon_dyn compatible)
        incoming_metadata: The metadata describing the format of the incoming data
        incoming_metadata_name: The type name recorded for the incoming metadata

    Returns:
        (needs_upgrade, deserializer) where needs_upgrade is a boolean indicating if an upgrade is required

    Raises:
        ValueError: If the schemas are incompatible or can't be upgraded
    """
    # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    expected_metadata = expected_class.get_tachyon_metadata()
    assert expected_metadata is not None  # Validated in create_deserializer

    # fmt: off
    upgrader = TachyonCppUpgrader(
        # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        expected_class.get_tachyon_class_name(),
        tachyon_metadata.to_protobuf(expected_metadata).SerializeToString(),
        tachyon_metadata.to_protobuf(incoming_metadata).SerializeToString(),
    )
    # fmt: on

    if not upgrader.upgrade_required:
        return False, expected_class.deserialize_tachyon

    def deserialize_and_upgrade(buffer: memoryview) -> Tachyon[T]:
        # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        upgraded_buffer = bytearray(expected_class.get_tachyon_constraint().size)
        upgrader.upgrade(buffer, memoryview(upgraded_buffer))
        # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        upgraded_instance = expected_class.deserialize_tachyon(memoryview(upgraded_buffer))
        return cast("Tachyon[T]", upgraded_instance)

    return upgrader.upgrade_required, deserialize_and_upgrade


def create_deserializer(
    compiler_context: CompilerContext,
    expected_class: type[Tachyon[T]],
    incoming_metadata: tachyon_model.TachyonMetadata,
    incoming_metadata_name: str,
) -> tuple[bool, Callable[[memoryview], Tachyon[T]]]:
    """Create a deserializer function that handles schema version compatibility.

    This function creates a deserializer that takes a memoryview containing serialized data
    matching the incoming_metadata format, and returns an instance of the expected class.
    If the incoming data is from an older schema version, it automatically upgrades
    the data to the current version.

    Note: To support upgrade, the expected class must be a pure-Python tachyon class with
    access to the Schema IR or a nanobind class, not a class generated from metadata.

    Args:
        compiler_context: The compiler context for serialization operations
        expected_class: The target class that should be produced (must be tachyon_dyn compatible)
        incoming_metadata: The metadata describing the format of the incoming data
        incoming_metadata_name: The type name recorded for the incoming metadata

    Returns:
        (needs_upgrade, deserializer) where needs_upgrade is a boolean indicating if an upgrade is required

    Raises:
        TypeError: If expected_class doesn't have required Tachyon methods or the metadata is not for a schema
        ValueError: If the schemas are incompatible or can't be upgraded
    """
    # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    expected_metadata = expected_class.get_tachyon_metadata()
    if expected_metadata is None:
        msg = f"No metadata available for expected class: {expected_class.__name__}"
        raise ValueError(msg)

    if not expected_metadata.python_required or not hasattr(expected_class, "get_tachyon_schema_ir"):
        if hasattr(expected_class, "get_tachyon_source_file_name"):
            return create_deserializer_with_cpp_upgrader(expected_class, incoming_metadata)

        msg = (
            f"Expected class {expected_class.__name__} is missing get_tachyon_schema_ir method\n"
            "Cannot upgrade incoming data to current version"
        )
        raise TypeError(msg)

    needs_upgrade = validate_tachyon_types_compatibility(expected=expected_metadata, incoming=incoming_metadata)

    if not needs_upgrade:
        return False, expected_class.deserialize_tachyon

    schema_ir: InstantiatedSchema = cast("Any", expected_class).get_tachyon_schema_ir()

    incoming_class, _ = py_type_from_metadata(compiler_context, incoming_metadata_name, incoming_metadata)

    upgrader = create_upgrade_plan(compiler_context, schema_ir, incoming_metadata)

    def deserialize_and_upgrade(buffer: memoryview) -> Tachyon[T]:
        old_instance = incoming_class.deserialize_tachyon(buffer)
        upgraded_instance = upgrade_schema(compiler_context, schema_ir, old_instance)
        return cast("Tachyon[T]", upgraded_instance)

    return upgrader.needs_upgrade(), deserialize_and_upgrade
