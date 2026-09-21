# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Replay-readiness summaries for journal generation."""

from __future__ import annotations

import json
from typing import TYPE_CHECKING, Final

from clockwork.journal import journal_pb2

if TYPE_CHECKING:
    from collections.abc import Iterable
    from pathlib import Path

_MAX_GAP_GROUP_COUNT: Final = 10
_MAX_GAP_SAMPLE_COUNT: Final = 5


def replay_readiness_is_sufficient(
    journal: journal_pb2.JournalFile,
    *,
    alignment_readiness_assessed: bool = True,
) -> bool:
    """Return whether a journal is sufficient for single-cog replay."""
    return alignment_readiness_assessed and implemented_readiness_checks_pass(journal)


def implemented_readiness_checks_pass(journal: journal_pb2.JournalFile) -> bool:
    """Return whether the implemented replay-readiness checks pass."""
    replay_readiness = journal.metadata.replay_readiness
    return (
        not journal.metadata.scope.box_instance_paths
        and len(journal.metadata.scope.cog_instance_paths) == 1
        and len(journal.cog_journals) == 1
        and journal.cog_journals[0].cog_instance_path == journal.metadata.scope.cog_instance_paths[0]
        and len(journal.cog_journals[0].executions) > 0
        and _state_snapshot_requirement_met(journal)
        and len(replay_readiness.gaps) == 0
    )


def _state_snapshot_requirement_met(journal: journal_pb2.JournalFile) -> bool:
    if len(journal.cog_journals) != 1:
        return False
    return not journal.cog_journals[0].has_clockwork_state or journal.metadata.replay_readiness.has_state_snapshot


def build_readiness_summary(journal: journal_pb2.JournalFile) -> dict[str, object]:
    """Build a deterministic machine-readable readiness summary."""
    replay_readiness = journal.metadata.replay_readiness
    return {
        "channel_summary_count": len(journal.channel_summaries),
        "cog_count": len(journal.cog_journals),
        "cog_instance_paths": list(journal.metadata.scope.cog_instance_paths),
        "end_time_ns": journal.metadata.end_time_ns,
        "execution_count": sum(len(cog_journal.executions) for cog_journal in journal.cog_journals),
        "gap_count": len(replay_readiness.gaps),
        "gaps": [_readiness_gap_summary(gap) for gap in replay_readiness.gaps],
        "has_state_snapshot": replay_readiness.has_state_snapshot,
        "implemented_readiness_checks_passed": implemented_readiness_checks_pass(journal),
        "log_uri": journal.metadata.log_uri,
        "missing_inputs": sorted(replay_readiness.missing_inputs),
        "start_time_ns": journal.metadata.start_time_ns,
        "sufficient_for_replay": replay_readiness.sufficient_for_replay,
    }


def write_readiness_summary_json(journal: journal_pb2.JournalFile, output: Path) -> None:
    """Write a deterministic JSON readiness summary."""
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(build_readiness_summary(journal), indent=2, sort_keys=True) + "\n", encoding="utf-8")


def format_readiness_summary(
    journal: journal_pb2.JournalFile,
    *,
    output_path: Path | None = None,
    summary_json_path: Path | None = None,
    include_gaps: bool = False,
) -> str:
    """Format a concise human-readable readiness summary."""
    replay_readiness = journal.metadata.replay_readiness
    status = "sufficient" if replay_readiness.sufficient_for_replay else "insufficient"
    snapshot_status = "yes" if replay_readiness.has_state_snapshot else "no"
    lines: list[str] = []
    if output_path is not None:
        lines.append(f"Journal written: {output_path}")
    if summary_json_path is not None:
        lines.append(f"Summary JSON written: {summary_json_path}")
    lines.append(
        "; ".join(
            [
                f"Replay readiness: {status}",
                f"cogs={len(journal.cog_journals)}",
                f"executions={sum(len(cog_journal.executions) for cog_journal in journal.cog_journals)}",
                f"gaps={len(replay_readiness.gaps)}",
                f"state_snapshot={snapshot_status}",
            ]
        )
    )
    if not replay_readiness.sufficient_for_replay and implemented_readiness_checks_pass(journal):
        lines.append("Implemented readiness checks passed, but alignment readiness was not assessed.")
    if replay_readiness.missing_inputs:
        lines.append(f"Missing inputs: {', '.join(sorted(replay_readiness.missing_inputs))}")
    if include_gaps and replay_readiness.gaps:
        lines.extend(_format_readiness_gaps(replay_readiness.gaps, summary_json_path=summary_json_path))
    return "\n".join(lines)


def _format_readiness_gaps(
    gaps: Iterable[journal_pb2.ReplayReadinessGap],
    *,
    summary_json_path: Path | None,
) -> list[str]:
    gap_list = sorted(gaps, key=_gap_summary_key)
    lines = [f"Replay-readiness gaps: total={len(gap_list)}"]
    lines.append("Gap reasons:")
    lines.extend(f"- {reason}: {count}" for reason, count in _count_gap_reasons(gap_list))
    lines.append("Most common gap channels:")
    gap_channels = _count_gap_channels(gap_list)
    lines.extend(f"- {channel_name}: {count}" for channel_name, count in gap_channels[:_MAX_GAP_GROUP_COUNT])
    remaining_channel_count = len(gap_channels) - _MAX_GAP_GROUP_COUNT
    if remaining_channel_count > 0:
        lines.append(f"- ... {remaining_channel_count} more channels")
    lines.append("Sample gaps:")
    lines.extend(f"- {_format_gap_sample(gap)}" for gap in gap_list[:_MAX_GAP_SAMPLE_COUNT])
    remaining_gap_count = len(gap_list) - _MAX_GAP_SAMPLE_COUNT
    if remaining_gap_count > 0:
        lines.append(f"Omitted {remaining_gap_count} readiness gap details from console output.")
    if summary_json_path is not None:
        lines.append(f"Full readiness gap details are in: {summary_json_path}")
    else:
        lines.append("Pass --summary-json to write full readiness gap details.")
    return lines


def _count_gap_reasons(gaps: Iterable[journal_pb2.ReplayReadinessGap]) -> list[tuple[str, int]]:
    reason_counts: dict[str, int] = {}
    for gap in gaps:
        reason = _gap_reason_name(gap.reason)
        reason_counts[reason] = reason_counts.get(reason, 0) + 1
    return sorted(reason_counts.items(), key=lambda item: (-item[1], item[0]))


def _count_gap_channels(gaps: Iterable[journal_pb2.ReplayReadinessGap]) -> list[tuple[str, int]]:
    channel_counts: dict[str, int] = {}
    for gap in gaps:
        channel_name = gap.channel_name or "(no channel)"
        channel_counts[channel_name] = channel_counts.get(channel_name, 0) + 1
    return sorted(channel_counts.items(), key=lambda item: (-item[1], item[0]))


def _format_gap_sample(gap: journal_pb2.ReplayReadinessGap) -> str:
    channel_name = gap.channel_name or "(no channel)"
    # fmt: off
    return (
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        f"{_gap_reason_name(gap.reason)}; channel={channel_name}; execution={int(gap.execution_index)}; {gap.message}"
    )
    # fmt: on


def _readiness_gap_summary(gap: journal_pb2.ReplayReadinessGap) -> dict[str, object]:
    # fmt: off
    return {
        "channel_name": gap.channel_name,
        "cog_instance_path": gap.cog_instance_path,
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        "execution_index": int(gap.execution_index),
        "message": gap.message,
        "reason": _gap_reason_name(gap.reason),
    }
    # fmt: on


def _gap_reason_name(reason: int) -> str:
    try:
        return journal_pb2.ReplayReadinessGapReason.Name(reason)
    except ValueError:
        return f"UNKNOWN_REPLAY_READINESS_GAP_REASON_{reason}"


def _gap_summary_key(gap: journal_pb2.ReplayReadinessGap) -> tuple[str, str, str, int, str]:
    # fmt: off
    return (
        _gap_reason_name(gap.reason),
        gap.channel_name,
        gap.cog_instance_path,
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        int(gap.execution_index),
        gap.message,
    )
    # fmt: on
