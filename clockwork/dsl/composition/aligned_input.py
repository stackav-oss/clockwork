# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Aligned input expansion and connection resolution for system composition."""

from __future__ import annotations

from typing import TYPE_CHECKING, cast

from clockwork.dsl.ir import aligner, clkbuiltins, cog, cog_components, uuid_reg

if TYPE_CHECKING:
    from clockwork.dsl.compiler_context import CompilerContext


def expand_aligned_inputs(cog_instance: cog.CogInstance, compiler_context: CompilerContext) -> None:
    """Expand all aligned inputs on a cog instance into upstream InputDef members.

    For each CogAlignedInputDef in the cog instance, creates N CogInstanceMember[InputDef]
    members (one per aligner upstream input) and appends them to the cog instance.
    Also registers UUIDs for the expanded members.

    Must be called after the aligner module is fully compiled (so that resolved inputs
    with InterfaceInfo are available).

    Args:
        cog_instance: The cog instance to expand.
        compiler_context: Compiler context for UUID registration.
    """
    aligned_members = [
        cast("cog.CogInstanceMember[cog_components.CogAlignedInputDef]", member)
        for member in cog_instance.members
        if isinstance(member.member, cog_components.CogAlignedInputDef)
    ]
    for member in aligned_members:
        _expand_one_aligned_input(cog_instance, member, compiler_context)


def _expand_one_aligned_input(
    cog_instance: cog.CogInstance,
    aligned_input_member: cog.CogInstanceMember[cog_components.CogAlignedInputDef],
    compiler_context: CompilerContext,
) -> None:
    """Expand a single CogAlignedInputDef into N upstream InputDef members."""
    aligned_def = aligned_input_member.member
    aligner_type = aligned_def.aligned_type
    if not isinstance(aligner_type, aligner.Aligner):
        msg = f"Expected Aligner, got {type(aligner_type).__name__}"
        raise TypeError(msg)

    _validate_aligner_ready(aligner_type)

    class_defs = cog_instance.cog_class.expanded_aligned_input_defs
    for aligner_input in aligner_type.inputs.values():
        input_name = f"{aligned_def.name}.{aligner_input.name}"
        input_def = class_defs[input_name]
        member = cog.CogInstanceMember.make(cog_instance, input_def, clkbuiltins.COG_INPUT_INSTANCE_TYPE)
        uuid_reg.register_entity_with_stable_key(compiler_context, member)
        cog_instance.inner_scope.define(input_name, member, cog_instance.module.terminals)
        # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        cog_instance.members.append(member)


def _validate_aligner_ready(aligner_type: aligner.Aligner) -> None:
    """Validate that the aligner is fully resolved and has alignment artifacts."""
    if aligner_type.resolved is None:
        msg = f"Aligner '{aligner_type.name}' is not resolved"
        raise RuntimeError(msg)
    if aligner_type.alignment_iface is None:
        msg = f"Aligner '{aligner_type.name}' has no alignment interface (compiler ordering bug?)"
        raise RuntimeError(msg)
