# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Tachyon Language-Independent Layout."""

from __future__ import annotations

import bisect
import itertools
from dataclasses import dataclass
from typing import TYPE_CHECKING

from clockwork.dsl.serialization import tachyon_reg

if TYPE_CHECKING:
    from clockwork.dsl.compiler_context import CompilerContext
    from clockwork.dsl.ir import schema


@dataclass(frozen=True, eq=True, slots=True)
class ConstrainedField(tachyon_reg.FieldConstraint):
    """Sortable combination of a FieldConstraint with a field number."""

    field_num: int

    def __lt__(self, other: ConstrainedField) -> bool:
        """See tachyon.md for explanation of this sort order."""
        # We take `-max` and `-min` here to get largest-first sort order.  This
        # prevents us having to reverse the list later.
        return (-max(self.size, self.alignment), -min(self.size, self.alignment), self.field_num) < (
            -max(other.size, other.alignment),
            -min(other.size, other.alignment),
            other.field_num,
        )


@dataclass(frozen=True, eq=True, slots=True)
class FieldSpan:
    """Records the byte span of a field in a Tachyon representation."""

    field_num: int
    offset: int
    size: int


@dataclass(frozen=True, eq=True, slots=True)
class Gap:
    """A Gap (padding) in a Tachyon layout."""

    size: int
    offset: int

    def __lt__(self, other: Gap) -> bool:
        """Compare two _Gap instances for sorting.

        We sort smallest-first, then least-aligned-first, then finally (as a
        tiebreaker for determinism) lowest-offset-first.
        """
        return (self.size, self.alignment(), self.offset) < (other.size, other.alignment(), other.offset)

    def alignment(self) -> int | float:
        """Calculate the alignment of a memory offset.

        The alignment of offset 0 is infinite, which we represent here using
        float("inf").  Otherwise an integer will be returned.
        """
        if self.offset == 0:
            return float("inf")
        # Bit magic: This isolates the lowest bit that's set in the value, which is
        # what determines its alignment.
        return self.offset & -self.offset


@dataclass(frozen=True, eq=True, slots=True)
class Layout:
    """A Tachyon layout."""

    fields: list[FieldSpan]
    gaps: list[Gap]
    size: int
    alignment: int

    def layout_order_elements(self) -> list[FieldSpan | Gap]:
        """Retrieve FieldSpans and Gaps in layout order."""
        return sorted(itertools.chain(self.fields, self.gaps), key=lambda x: x.offset)

    def field_number_order_fields(self) -> list[FieldSpan]:
        """Retrieve FieldSpans in field number order."""
        return sorted(self.fields, key=lambda x: x.field_num)


def layout_schema(compiler_context: CompilerContext, schema_ir: schema.InstantiatedSchema) -> Layout:
    """Generate a Tachyon layout for the given schema.

    Please see tachyon.md for a description of the algorithm implemented here.

    Returns:
        A list of FieldSpans, one per schema field, in arbitrary (but
        deterministic) order.
    """
    field_constraints = []
    for field in schema_ir.fields.values():
        constraint = tachyon_reg.constraint_for_type(compiler_context, field.type_info)
        if constraint is None:
            msg = field.append_error_line(
                f"Cannot determine size and alignment of field #{field.num} ({field.type_info.value_key()})",
            )
            raise RuntimeError(msg)
        field_constraints.append(
            ConstrainedField(field_num=field.num, size=constraint.size, alignment=constraint.alignment),
        )

    return _layout_fields(field_constraints)


def _layout_fields(field_constraints: list[ConstrainedField]) -> Layout:
    """Helper function for layout_schema.

    Args:
        field_constraints: List of (constraint, field_num)
    """
    layout: list[FieldSpan] = []

    # We maintain gaps as a sorted list, which is adequate for the current expected size of a message given current use
    # cases (<1000 fields per message).
    # TODO(OI-3055): Consider implementing gaps list optimization for larger element counts
    gaps: list[Gap] = []

    def _fit_into_gap(field_num: int, gap_index: int, gap_offset: int, gap_size: int, offset: int) -> None:
        """Fit a field into a gap; pulled out of loop below for less nesting/more clarity."""
        layout.append(FieldSpan(field_num=field_num, offset=offset, size=size))
        del gaps[gap_index]
        # Create new gap(s) with leftover space
        padding = offset - gap_offset
        if padding > 0:
            # Add a new gap before the field
            bisect.insort(gaps, Gap(offset=gap_offset, size=padding))
        gap_end = gap_offset + gap_size
        field_end = offset + size
        leftover = gap_end - field_end
        if leftover > 0:
            # Add a new gap after the field
            bisect.insort(gaps, Gap(offset=field_end, size=leftover))

    max_alignment = 0
    total_size = 0
    last_field: FieldSpan | None = None
    for constraint in sorted(field_constraints):
        size = constraint.size
        alignment = constraint.alignment
        max_alignment = max(max_alignment, alignment)

        for i, gap in enumerate(gaps):
            # This rounds the offset up to the nearest multiple of `alignment`
            # if necessary, or leaves it unchanged if it's already aligned.
            offset = ((gap.offset + alignment - 1) // alignment) * alignment
            if offset + size <= gap.offset + gap.size:
                _fit_into_gap(
                    field_num=constraint.field_num,
                    gap_index=i,
                    gap_offset=gap.offset,
                    gap_size=gap.size,
                    offset=offset,
                )
                break
        else:
            # Add the field at the end of the layout
            offset = (last_field.offset + last_field.size) if last_field else 0
            # Round up to next aligned address (same as above)
            field_offset = ((offset + alignment - 1) // alignment) * alignment
            layout.append(FieldSpan(field_num=constraint.field_num, offset=field_offset, size=size))
            padding = field_offset - offset
            if padding > 0:
                bisect.insort(gaps, Gap(offset=offset, size=padding))
            next_byte = field_offset + size
            assert next_byte > total_size
            total_size = next_byte
            last_field = layout[-1]

    spillover = total_size % max_alignment
    if spillover > 0:
        trailing_padding = max_alignment - spillover
        gaps.append(Gap(offset=total_size, size=trailing_padding))
        total_size += trailing_padding

    return Layout(fields=layout, gaps=gaps, size=total_size, alignment=max_alignment)
