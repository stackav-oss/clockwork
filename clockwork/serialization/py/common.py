# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Functions for handling data compatibility and upgrade."""

from __future__ import annotations

from clockwork.serialization.metadata import tachyon_model


def validate_tachyon_types_compatibility(
    *,
    expected: tachyon_model.TachyonMetadata,
    incoming: tachyon_model.TachyonMetadata,
) -> bool:
    """Validate compatibility of tachyon metadata between log and message type.

    Args:
        expected: Expected TachyonMetadata to validate against
        incoming: TachyonMetadata from the incoming data

    Returns:
        True if an upgrade is required, False otherwise

    Raises:
        TypeError: If the outer type is not a schema
        ValueError: If the metadata does not match the expected
    """
    expected.check_for_unexpected_schema_changes(incoming)

    expected_outer_type = expected.types[expected.outer_type_id]
    incoming_outer_type = incoming.types[incoming.outer_type_id]

    assert isinstance(incoming_outer_type, tachyon_model.SchemaType)  # Validated by check_for_unexpected_schema_changes
    assert isinstance(expected_outer_type, tachyon_model.SchemaType)  # Validated by check_for_unexpected_schema_changes

    if incoming_outer_type.version < expected_outer_type.version:
        return True

    return not incoming.is_wire_compatible(expected)
