# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Functions for handling data compatibility and upgrade."""

from __future__ import annotations

import pathlib
from typing import TYPE_CHECKING, Any, TypeVar

from clockwork.dsl.ir.compiler import compile_source_file
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import ModuleID
from clockwork.serialization.metadata import tachyon as tachyon_metadata
from clockwork.serialization.py.common import validate_tachyon_types_compatibility
from clockwork.serialization.py.protocol import Tachyon
from clockwork.serialization.py.tachyon_dyn import create_upgrade_plan, get_schema_dataclass, upgrade_schema
from clockwork.serialization.py.tachyon_dyn_from_metadata import py_type_from_metadata

if TYPE_CHECKING:
    from collections.abc import Callable

    from clockwork.dsl.ir.schema import InstantiatedSchema

T = TypeVar("T", bound=Tachyon[Any])


# Suppressing PLR0913 (too many args) because this can't really be split. This function is only called from C++.
def create_upgrader(  # noqa: PLR0913 (see above)
    current_module_name: str,
    current_source_file_name: str,
    current_class_name: str,
    current_metadata_view: memoryview,
    incoming_metadata_view: memoryview,
    incoming_metadata_name: str,
) -> tuple[bool, Callable[[memoryview, memoryview], None] | None]:
    """Create a binary message upgrade function that handles schema version compatibility.

    This function is called from the C++ tachyon upgrader when the python_required flag is
    set in the current schema metadata.

    This function creates a deserializer that takes a memoryview containing serialized data
    matching the incoming_metadata format, and returns an instance of the expected class.
    If the incoming data is from an older schema version, it automatically upgrades
    the data to the current version.

    Args:
        current_module_name: The module name for the current schema
        current_source_file_name: The source file name for the current schema
        current_class_name: The class name for the current schema
        current_metadata_view: The serialized metadata protobuf describing the format of the current schema
        incoming_metadata_view: The serialized metadata protobuf describing the format of the incoming data
        incoming_metadata_name: The type name recorded for the incoming metadata

    Returns:
        (needs_upgrade, upgrader) where needs_upgrade is a boolean indicating if an upgrade is required

    Raises:
        ValueError: If the schemas are incompatible or can't be upgraded
    """
    incoming_metadata = tachyon_metadata.get_metadata_from_protobuf(incoming_metadata_view.tobytes())
    current_metadata = tachyon_metadata.get_metadata_from_protobuf(current_metadata_view.tobytes())
    needs_upgrade = validate_tachyon_types_compatibility(expected=current_metadata, incoming=incoming_metadata)

    if not needs_upgrade:
        return (False, None)

    _module = compile_source_file(
        ModuleID.from_path(current_module_name, pathlib.Path(current_source_file_name)),
        FilesystemImporter(compile_fn=compile_source_file),
    )
    native_wrapper_class, _native_wrapper_class = get_schema_dataclass(_module.context, _module, current_class_name)

    schema_ir: InstantiatedSchema = native_wrapper_class.get_tachyon_schema_ir()

    incoming_class, _ = py_type_from_metadata(_module.context, incoming_metadata_name, incoming_metadata)

    upgrader = create_upgrade_plan(_module.context, schema_ir, incoming_metadata)

    def deserialize_and_upgrade(input_buffer: memoryview, output_buffer: memoryview) -> None:
        old_instance = incoming_class.deserialize_tachyon(input_buffer)
        native_upgraded_instance = upgrade_schema(_module.context, schema_ir, old_instance)
        native_upgraded_instance.serialize_tachyon(output_buffer)

    return upgrader.needs_upgrade(), deserialize_and_upgrade
