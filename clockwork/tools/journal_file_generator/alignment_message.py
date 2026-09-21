# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Structural parsing for generated alignment result messages."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import cast

from clockwork.journal import journal_pb2


def alignment_result_from_message(message: object, aligner_name: str) -> journal_pb2.AlignmentResult | None:
    """Convert a deserialized generated alignment message into a journal proto."""
    field_values = _message_field_values(message)
    aligned_inputs = tuple(
        aligned_input
        for input_name in _alignment_input_names(tuple(field_values))
        if (aligned_input := _aligned_input_from_fields(input_name, field_values)) is not None
    )
    if not aligned_inputs:
        return None
    return journal_pb2.AlignmentResult(aligner_name=aligner_name, aligned_inputs=list(aligned_inputs))


def _message_field_values(message: object) -> Mapping[str, object]:
    if isinstance(message, Mapping):
        return {key: value for key, value in cast("Mapping[object, object]", message).items() if isinstance(key, str)}
    return {field_name: cast("object", getattr(message, field_name)) for field_name in _message_field_names(message)}


def _message_field_names(message: object) -> tuple[str, ...]:
    dataclass_fields = getattr(message, "__dataclass_fields__", None)
    if isinstance(dataclass_fields, Mapping):
        return tuple(key for key in dataclass_fields if isinstance(key, str))

    slots = getattr(type(message), "__slots__", ())
    if isinstance(slots, str):
        return (slots,)
    if isinstance(slots, Sequence):
        return tuple(slot for slot in slots if isinstance(slot, str) and hasattr(message, slot))
    return tuple(vars(message))


def _alignment_input_names(field_names: Sequence[str]) -> tuple[str, ...]:
    input_names: list[str] = []
    for field_name in field_names:
        input_name = _alignment_input_name_from_sequence_field(field_name)
        if input_name != "" and input_name not in input_names:
            input_names.append(input_name)
    return tuple(input_names)


def _alignment_input_name_from_sequence_field(field_name: str) -> str:
    if field_name.endswith("_begin_seq"):
        return field_name.removesuffix("_begin_seq")
    if (
        field_name.endswith("_seq")
        and not field_name.endswith("_end_seq")
        and not field_name.endswith("_first_new_seq")
    ):
        return field_name.removesuffix("_seq")
    return ""


def _aligned_input_from_fields(input_name: str, field_values: Mapping[str, object]) -> journal_pb2.AlignedInput | None:
    present = _field_bool(field_values, f"has_{input_name}", default=True)
    begin_sequence_number = _field_uint(field_values, f"{input_name}_begin_seq")
    end_sequence_number = _field_uint(field_values, f"{input_name}_end_seq")
    if begin_sequence_number is not None and end_sequence_number is not None:
        return journal_pb2.AlignedInput(
            input_name=input_name,
            present=present,
            batch_begin_sequence_number=begin_sequence_number if present else 0,
            batch_end_sequence_number=end_sequence_number if present else 0,
            is_batch=True,
        )

    selected_sequence_number = _field_uint(field_values, f"{input_name}_seq")
    if selected_sequence_number is None:
        return None
    return journal_pb2.AlignedInput(
        input_name=input_name,
        selected_sequence_number=selected_sequence_number if present else 0,
        present=present,
    )


def _field_uint(field_values: Mapping[str, object], field_name: str) -> int | None:
    value = field_values.get(field_name)
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value if value >= 0 else None
    if isinstance(value, float) and value >= 0 and value.is_integer():
        return int(value)
    return None


def _field_bool(field_values: Mapping[str, object], field_name: str, *, default: bool) -> bool:
    value = field_values.get(field_name)
    if isinstance(value, bool):
        return value
    if isinstance(value, int):
        return bool(value)
    return default
