# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Alignment output schema generation."""

from __future__ import annotations

import uuid
from typing import TYPE_CHECKING

from clockwork.dsl.ir import clkbuiltins, node, schema

if TYPE_CHECKING:
    from clockwork.dsl.ir import aligner


def generate_alignment_schema(
    aligner_ir: aligner.ResolvedAligner,
    module: node.Module,
) -> schema.InstantiatedSchema:
    """Generate the alignment output schema for an aligner.

    Creates a schema named ``{AlignerName}AlignmentMsg`` with per-input fields:

    - Non-batch inputs: ``{name}_seq: U64``
    - Batch inputs: ``{name}_begin_seq: U64``, ``{name}_end_seq: U64`` (inclusive, closed range)
    - Optional inputs: ``has_{name}: Bool``
    - Non-batch reuse inputs: ``{name}_is_new: Bool``
    - Batch+reuse inputs: ``{name}_first_new_seq: U64``

    Args:
        aligner_ir: The resolved aligner definition.
        module: The module containing the aligner.

    Returns:
        The generated alignment schema as an ``InstantiatedSchema``.
    """
    schema_name = f"{aligner_ir.name}AlignmentMsg"
    fields = _build_alignment_fields(aligner_ir, module)
    schema_uuid = uuid.uuid3(clkbuiltins.CLOCKWORK_NAMESPACE_UUID, aligner_ir.source.fqn)

    return schema.make_schema_class(
        name=schema_name,
        module=module,
        fields=fields,
        uuid=schema_uuid,
        doc=f"Generated alignment output schema for {aligner_ir.source.fqn}",
    )


def _build_alignment_fields(
    aligner_ir: aligner.ResolvedAligner,
    module: node.Module,
) -> list[schema.FieldDef]:
    """Build the list of schema fields from the aligner's resolved inputs.

    Args:
        aligner_ir: The resolved aligner definition.
        module: The module.

    Returns:
        Ordered list of ``FieldDef`` objects.
    """
    fields: list[schema.FieldDef] = []
    field_num = 0

    for inp in aligner_ir.inputs.values():
        is_batch = inp.batch_size is not None

        if is_batch:
            fields.append(
                schema.make_field(
                    module,
                    field_num,
                    f"{inp.name}_begin_seq",
                    clkbuiltins.UINT64,
                    doc=f"Begin sequence number for {inp.name} batch",
                )
            )
            field_num += 1
            fields.append(
                schema.make_field(
                    module,
                    field_num,
                    f"{inp.name}_end_seq",
                    clkbuiltins.UINT64,
                    doc=f"End sequence number for {inp.name} batch (inclusive)",
                )
            )
            field_num += 1
        else:
            fields.append(
                schema.make_field(
                    module,
                    field_num,
                    f"{inp.name}_seq",
                    clkbuiltins.UINT64,
                    doc=f"Sequence number of aligned {inp.name} message",
                )
            )
            field_num += 1

        if inp.optional:
            fields.append(
                schema.make_field(
                    module,
                    field_num,
                    f"has_{inp.name}",
                    clkbuiltins.BOOL,
                    doc=f"Whether optional input {inp.name} contributed to this alignment",
                )
            )
            field_num += 1

        if inp.reuse:
            if is_batch:
                fields.append(
                    schema.make_field(
                        module,
                        field_num,
                        f"{inp.name}_first_new_seq",
                        clkbuiltins.UINT64,
                        doc=f"Sequence number of first new message in {inp.name} batch (equals end_seq if all reused)",
                    )
                )
            else:
                fields.append(
                    schema.make_field(
                        module,
                        field_num,
                        f"{inp.name}_is_new",
                        clkbuiltins.BOOL,
                        doc=f"Whether {inp.name} is a new message (not reused from previous alignment)",
                    )
                )
            field_num += 1

    return fields
