# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unimplemented Audio source IR node."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from clockwork.dsl import cst
from clockwork.dsl.cpp import types
from clockwork.dsl.cpp.context import Header
from clockwork.dsl.ir import expr, node, typesys
from clockwork.dsl.ir.module_id import CLK_REPO
from typing_extensions import override

if TYPE_CHECKING:
    from clockwork.dsl.ir.diagnostics import DiagnosticsDef, DiagnosticsInstance


@dataclass
class AudioSource(node.CstNode[cst.AudioSource], node.DocRequiredEntity, typesys.TypeDef, typesys.InstantiatableEntity):
    """An audio source."""

    message_type: expr.TypeExpression | typesys.TypeVal

    diagnostics: DiagnosticsDef

    @classmethod
    def from_cst(cls: type[AudioSource], cst_node: cst.AudioSource, module: node.Module) -> AudioSource:
        """Construct a AudioSource IR node form a CST node."""
        msg = node.append_error_line(
            cst_node,
            module,
            "AudioSource not implemented.",
        )
        raise NotImplementedError(msg)

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        msg = node.append_error_line(
            self.cst_node,
            self.module,
            "AudioSource not implemented.",
        )
        raise NotImplementedError(msg)

    @property
    def endpoint(self) -> str:
        """Audio endpoint as a string."""
        return "Unimplemented AudioSource"

    @override
    def make_instance(
        self,
        *,
        cst_node: cst.NewStmt | None,
        module: node.Module,
        scope: node.Scope,
        name: str,
        doc: node.Doc | None,
    ) -> AudioSourceInstance:
        """Create an instance of the entity."""
        msg = node.append_error_line(
            cst_node,
            module,
            "AudioSource not implemented.",
        )
        raise NotImplementedError(msg)


@dataclass
class AudioSourceInstance(
    node.CstNode[cst.NewStmt], node.DocableEntity, typesys.NamedAttribute, typesys.MembershipEntity
):
    """An instantiation of an audio source."""

    source: AudioSource
    inner_scope: node.Scope
    diagnostics: DiagnosticsInstance

    # We have to suppress PLR0913 (too many args) because this is already an extremely simple function that can't be split but still needs all these args. The args are all different types so mypy will catch any mixups in the call sites, and we have made the args kwonly as extra assurance.
    @classmethod
    def make(  # noqa: PLR0913 (see above)
        cls: type[AudioSourceInstance],
        *,
        source: AudioSource,  # noqa: ARG003 (must be consistent with override)
        cst_node: cst.NewStmt | None,
        module: node.Module,
        scope: node.Scope,  # noqa: ARG003 (must be consistent with override)
        name: str,  # noqa: ARG003 (must be consistent with override)
        doc: node.Doc | None,  # noqa: ARG003 (must be consistent with override)
    ) -> AudioSourceInstance:
        """Factory function for AudioSourceInstance."""
        msg = node.append_error_line(
            cst_node,
            module,
            "AudioSourceInstance not implemented.",
        )
        raise NotImplementedError(msg)

    @override
    def attribute(self, name: str) -> typesys.Value | None:
        """Look up a definition in the membership entity."""
        msg = node.append_error_line(
            self.cst_node,
            self.module,
            "AudioSourceInstance not implemented.",
        )
        raise NotImplementedError(msg)


def validate_message_type(message_type: typesys.TypeVal, source: AudioSource, channel_name: str) -> str | None:
    """Check for the expected AudioSource message type."""
    msg = "AudioSource not implemented."
    raise NotImplementedError(msg)


AUDIO_SOURCE_TEMPLATE = types.CppTemplate(
    includes=[Header(CLK_REPO, "NOT IMPLEMENTED")],
    template_name="",
    cpp_namespace="",
)
