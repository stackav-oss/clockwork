# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Nanobind binding node."""

from __future__ import annotations

from dataclasses import dataclass

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.ir import (
    clkenum,
    expr,
    node,
    typesys,
)
from clockwork.dsl.ir.cst_util import get_span
from clockwork.dsl.ir.interface import InterfaceAlias, InterfaceReference
from clockwork.dsl.serialization.tap import to_schema_instantiation


@dataclass
class ResolvedNanobindBinding(node.CstNode[cst.NanobindBinding]):
    """A fully resolved nanobind binding."""

    original_type: typesys.Instantiation | clkenum.ResolvedEnum
    alias_name: str | None

    def get_resolved(self) -> ResolvedNanobindBinding:
        """Convenience function so code can work with either resolved or unresolved bindings."""
        return self


@dataclass
class NanobindBinding(node.CstNode[cst.NanobindBinding]):
    """IR node for nanobind binding specialization for Tappy types."""

    typespec: expr.Expr | node.DeferredLookup[typesys.Instantiation | clkenum.ClkEnum]
    alias_name: str | None
    resolved: ResolvedNanobindBinding | None

    @classmethod
    def from_cst(
        cls: type[NanobindBinding],
        cst_node: cst.NanobindBinding,
        module: node.Module,
    ) -> NanobindBinding:
        """Construct a NanobindBinding IR node from a CST node."""
        child_typespec = cst_node.child_typespec()
        alias_name_cst = cst_node.maybe_name()
        assert module.terminals is not None
        alias_name = get_span(alias_name_cst.child_value(), module.terminals) if alias_name_cst else None
        typespec: expr.Expr | node.DeferredLookup[typesys.Instantiation | clkenum.ClkEnum] | None = None
        if (identifier := child_typespec.maybe_identifier()) is not None:
            typespec = node.DeferredLookup.make(
                expected_type=InterfaceAlias | clkenum.ClkEnum,  # pyright: ignore[reportArgumentType]
                cst_identifier=identifier,
                terminals=module.terminals,
            )
        else:
            typespec = expr.Expr.from_cst(child_typespec, module)

        assert typespec is not None

        return cls(
            module=module,
            cst_node=cst_node,
            typespec=typespec,
            alias_name=alias_name,
            resolved=None,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if self.resolved:
            msg = self.append_error_line(f"Attempt to resolve nanobind type caster twice: {self}")
            raise RuntimeError(msg)

        original_type: typesys.Instantiation | clkenum.ResolvedEnum | None = None
        if isinstance(self.typespec, expr.Expr):
            typespec = self.typespec.evaluate()
            assert isinstance(typespec, typesys.Instantiation)

            # convert Tappy<> to Tap<Tachyon<>>
            interface_reference = InterfaceReference.from_typespec(typespec)
            assert not isinstance(interface_reference, str)
            original_type = interface_reference.typespec

            repr_schema = to_schema_instantiation(original_type)
            if repr_schema.generic_parameters() and not self.alias_name:
                msg = self.append_error_line("Nanobind bindings with generic schemas must use an alias.")
                raise ValueError(msg)

        elif isinstance(self.typespec, clkenum.ClkEnum):
            original_type = self.typespec.get_resolved()

        else:
            msg = self.append_error_line(f"Cannot generate nanobind bindings for {type(self.typespec)}.")
            raise TypeError(msg)

        assert isinstance(original_type, typesys.Instantiation | clkenum.ResolvedEnum)

        self.resolved = ResolvedNanobindBinding(
            module=self.module,
            cst_node=self.cst_node,
            original_type=original_type,
            alias_name=self.alias_name,
        )

    def get_resolved(self) -> ResolvedNanobindBinding:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)

        return self.resolved
