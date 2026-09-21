# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Decode signal presence from dynamically typed report-group messages."""

from __future__ import annotations

import dataclasses
from dataclasses import dataclass
from typing import Any, Final, final

_LEGACY_ASSUME_PRESENT: Final[int] = 0
_COUNT_FIELD: Final[int] = 1
_PRESENCE_BIT: Final[int] = 2
_SIGNAL_PRESENCE_FIELD: Final[str] = "signal_presence"


@final
@dataclass(frozen=True, slots=True)
class SignalValiditySpec:
    """Validity metadata for one signal in a report group."""

    field_prefix: str
    """Prefix shared by every generated field for the signal."""

    source: int
    """Numeric SignalValiditySource enum value."""

    index: int
    """Count-field number or presence-bit index, depending on ``source``."""


@final
@dataclass(frozen=True, slots=True)
class ReportGroupDecodeContext:
    """Signal validity information required to decode one report group."""

    signals: tuple[SignalValiditySpec, ...]
    """Validity specifications sorted by descending field-prefix length."""

    def __post_init__(self) -> None:
        """Canonicalize signal ordering for longest-prefix ownership matching."""
        object.__setattr__(
            self, "signals", tuple(sorted(self.signals, key=lambda spec: len(spec.field_prefix), reverse=True))
        )


def make_report_group_decode_context(
    signal_metadata: Any,  # noqa: ANN401 # Dynamically generated SignalMetadataConfig type.
    report_group_metadata: Any,  # noqa: ANN401 # Dynamically generated ReportGroupMetadata type.
) -> ReportGroupDecodeContext:
    """Build a decode context from signal and report-group metadata.

    Args:
        signal_metadata: Dynamically generated signal metadata configuration.
        report_group_metadata: Report-group metadata within that configuration.

    Returns:
        Canonically ordered validity specifications for the report group.

    Raises:
        ValueError: If a report-group signal references an invalid signal index.
    """
    specs: list[SignalValiditySpec] = []
    for report_group_signal in report_group_metadata.signals:
        signal_index = int(report_group_signal.signal_index)
        if signal_index < 0 or signal_index >= len(signal_metadata.signals):
            msg = f"Signal index {signal_index} is outside signal metadata"
            raise ValueError(msg)
        signal = signal_metadata.signals[signal_index]
        alias_value = getattr(report_group_signal, "alias", None)
        field_prefix = str(alias_value) if alias_value else str(signal.name)
        source_value = getattr(report_group_signal, "validity_source", _LEGACY_ASSUME_PRESENT)
        source = int(getattr(source_value, "value", source_value))
        specs.append(
            SignalValiditySpec(
                field_prefix=field_prefix,
                source=source,
                index=int(getattr(report_group_signal, "validity_index", 0)),
            )
        )
    return ReportGroupDecodeContext(signals=tuple(specs))


def make_report_group_decode_contexts(
    signal_metadata: Any,  # noqa: ANN401 # Dynamically generated SignalMetadataConfig type.
) -> dict[str, ReportGroupDecodeContext]:
    """Build report-group decode contexts indexed by channel name.

    Args:
        signal_metadata: Dynamically generated signal metadata configuration.

    Returns:
        Decode context for every report-group channel in the configuration.

    Raises:
        ValueError: If a channel references unavailable report-group metadata.
    """
    cogs_by_path = {str(cog.cog_path): cog for cog in signal_metadata.cogs}
    contexts: dict[str, ReportGroupDecodeContext] = {}
    for channel in signal_metadata.report_group_channels:
        cog = cogs_by_path.get(str(channel.cog_path))
        report_group_index = int(channel.report_group_index)
        if cog is None or report_group_index < 0 or report_group_index >= len(cog.report_groups):
            msg = f"Report-group metadata is unavailable for channel {channel.channel_name}"
            raise ValueError(msg)
        contexts[str(channel.channel_name)] = make_report_group_decode_context(
            signal_metadata,
            cog.report_groups[report_group_index],
        )
    return contexts


def decode_aggregation_fields(
    tachyon_message: Any,  # noqa: ANN401 # Dynamically generated Tachyon type.
    context: ReportGroupDecodeContext | None,
) -> dict[str, object | None]:
    """Decode signal fields from a post-aggregated report-group message.

    Args:
        tachyon_message: Dynamically generated report-group message.
        context: Validity metadata, or ``None`` for direct legacy conversion.

    Returns:
        Signal fields with absent values replaced by ``None``.

    Raises:
        ValueError: If explicit validity metadata cannot be resolved.
    """
    if not dataclasses.is_dataclass(tachyon_message):
        return {}

    raw_fields = {
        field.name: getattr(tachyon_message, field.name)
        for field in dataclasses.fields(tachyon_message)
        if field.name not in {"execution_count", "execution_interval"}
    }
    presence_bits = raw_fields.pop(_SIGNAL_PRESENCE_FIELD, None)
    if context is None:
        return raw_fields

    field_numbers = (
        _field_numbers_by_name(tachyon_message, is_batched=False)
        if any(spec.source == _COUNT_FIELD for spec in context.signals)
        else {}
    )
    return _apply_validity(raw_fields, presence_bits, context, field_numbers)


def decode_batch_entries(
    tachyon_message: Any,  # noqa: ANN401 # Dynamically generated Tachyon type.
    context: ReportGroupDecodeContext | None,
) -> list[dict[str, object | None]]:
    """Decode per-execution signal fields from a batched report-group message.

    Args:
        tachyon_message: Dynamically generated batched report-group message.
        context: Validity metadata, or ``None`` for direct legacy conversion.

    Returns:
        One decoded signal dictionary per execution row.

    Raises:
        ValueError: If explicit validity metadata cannot be resolved.
    """
    signals_soa = getattr(tachyon_message, "signals", None)
    if signals_soa is None or not dataclasses.is_dataclass(signals_soa):
        return []

    soa_fields = dataclasses.fields(signals_soa)
    if not soa_fields:
        return []
    first_values = getattr(signals_soa, soa_fields[0].name)
    entry_count = len(first_values) if isinstance(first_values, list) else 0
    field_numbers = (
        _field_numbers_by_name(tachyon_message, is_batched=True)
        if context is not None and any(spec.source == _COUNT_FIELD for spec in context.signals)
        else {}
    )

    entries: list[dict[str, object | None]] = []
    for entry_index in range(entry_count):
        raw_entry = {
            field.name: field_values[entry_index]
            for field in soa_fields
            if isinstance((field_values := getattr(signals_soa, field.name)), list) and entry_index < len(field_values)
        }
        presence_bits = raw_entry.pop(_SIGNAL_PRESENCE_FIELD, None)
        entries.append(
            raw_entry if context is None else _apply_validity(raw_entry, presence_bits, context, field_numbers)
        )
    return entries


def decode_aggregation_observation_counts(
    tachyon_message: Any,  # noqa: ANN401 # Dynamically generated Tachyon type.
    context: ReportGroupDecodeContext,
) -> dict[str, int | None]:
    """Decode available per-signal observation counts from an aggregate message.

    Args:
        tachyon_message: Dynamically generated post-aggregated report-group message.
        context: Validity metadata for the report group.

    Returns:
        Counts keyed by signal field prefix.
        Presence-bit signals map to ``None`` because they do not expose an observation count.
        Legacy signals use the report-group execution count to preserve historical behavior.

    Raises:
        TypeError: If a count-backed validity field cannot be resolved to an integer.
    """
    field_numbers: dict[int, str] = (
        _field_numbers_by_name(tachyon_message, is_batched=False)
        if any(spec.source == _COUNT_FIELD for spec in context.signals)
        else {}
    )
    execution_count = int(getattr(tachyon_message, "execution_count", 0))
    counts: dict[str, int | None] = {}
    for spec in context.signals:
        if spec.source == _LEGACY_ASSUME_PRESENT:
            counts[spec.field_prefix] = execution_count
            continue
        if spec.source != _COUNT_FIELD:
            counts[spec.field_prefix] = None
            continue
        field_name = field_numbers.get(spec.index)
        count = getattr(tachyon_message, field_name, None) if field_name is not None else None
        if not isinstance(count, int):
            msg = f"Validity count field {spec.index} is unavailable for signal {spec.field_prefix}"
            raise TypeError(msg)
        counts[spec.field_prefix] = count
    return counts


def _field_numbers_by_name(tachyon_message: object, *, is_batched: bool) -> dict[int, str]:
    metadata_getter = getattr(type(tachyon_message), "get_tachyon_metadata", None)
    if metadata_getter is None:
        msg = f"Tachyon metadata is unavailable for {type(tachyon_message).__name__}"
        raise ValueError(msg)
    metadata = metadata_getter()
    schema_type = metadata.types[metadata.outer_type_id]
    if is_batched:
        signals_field = next((field for field in schema_type.fields if field.name == "signals"), None)
        if signals_field is None:
            msg = "Batched report group has no signals field in Tachyon metadata"
            raise ValueError(msg)
        soa_type = metadata.types[signals_field.type_id]
        schema_type = metadata.types[soa_type.schema_type_id]
    return {int(field.num): str(field.name) for field in schema_type.fields}


def _apply_validity(
    fields: dict[str, object | None],
    presence_bits: object | None,
    context: ReportGroupDecodeContext,
    field_numbers: dict[int, str],
) -> dict[str, object | None]:
    signal_presence = {
        spec.field_prefix: _is_present(spec, fields, presence_bits, field_numbers) for spec in context.signals
    }
    return {
        field_name: (
            field_value
            if (owner := _field_owner(field_name, context)) is None or signal_presence[owner.field_prefix]
            else None
        )
        for field_name, field_value in fields.items()
    }


def _field_owner(field_name: str, context: ReportGroupDecodeContext) -> SignalValiditySpec | None:
    return next(
        (spec for spec in context.signals if field_name.startswith(f"{spec.field_prefix}_")),
        None,
    )


def _is_present(
    spec: SignalValiditySpec,
    fields: dict[str, object | None],
    presence_bits: object | None,
    field_numbers: dict[int, str],
) -> bool:
    if spec.source == _LEGACY_ASSUME_PRESENT:
        return True
    if spec.source == _COUNT_FIELD:
        field_name = field_numbers.get(spec.index)
        if field_name is None or field_name not in fields:
            msg = f"Validity count field {spec.index} is unavailable for signal {spec.field_prefix}"
            raise ValueError(msg)
        count = fields[field_name]
        if not isinstance(count, int | float):
            msg = f"Validity count field {field_name} is not numeric for signal {spec.field_prefix}"
            raise ValueError(msg)
        return count > 0
    if spec.source == _PRESENCE_BIT:
        if not isinstance(presence_bits, int):
            msg = f"Presence bitset is unavailable for signal {spec.field_prefix}"
            raise ValueError(msg)
        return bool((presence_bits >> spec.index) & 1)
    msg = f"Unsupported validity source {spec.source} for signal {spec.field_prefix}"
    raise ValueError(msg)
