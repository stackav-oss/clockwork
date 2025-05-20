# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Extern type support."""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import TYPE_CHECKING

from clockwork.dsl import cst
from clockwork.dsl.cpp import context, literal, typereg, types
from clockwork.dsl.ir import clkbuiltins, expr, extern_type, node, primitive, strongtypes
from clockwork.dsl.ir.cst_util import get_span
from clockwork.dsl.ir.module_id import CLK_REPO
from clockwork.dsl.serialization import tachyon_reg

if TYPE_CHECKING:
    from collections.abc import Sequence

    from clockwork.dsl.compiler_context import CompilerContext


@dataclass
class CppExtern(node.CstNode[cst.CppExtern], node.DocableEntity):
    """Manage extern Cpp class."""

    header: context.Header
    namespace: str
    extern_types: Sequence[CppExternType]

    @classmethod
    def from_cst(cls: type[CppExtern], cst_node: cst.CppExtern, module: node.Module) -> CppExtern:
        """Construct an IR node form a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        extern_block = cst_node.child_cpp_extern_block()
        header_path = primitive.Literal.from_cst(extern_block.child_cpp_extern_header().child_header(), module)
        if not isinstance(header_path, primitive.StringLiteral):
            msg = "The header of a cpp_extern must be a string literal."
            raise TypeError(msg)
        # Need to strip the outer quotes from the string literal.
        header = context.Header(module.module_id.repo, header_path.value, iwyu_pragma="IWYU pragma: export")
        namespace = get_span(extern_block.child_cpp_extern_namespace().child_namespace().span, module.terminals)

        extern_types = [CppExternType.from_cst(cst, module) for cst in extern_block.children_cpp_extern_type()]

        return CppExtern(
            doc=doc,
            module=module,
            cst_node=cst_node,
            header=header,
            namespace=namespace,
            extern_types=extern_types,
        )

    def resolve(self) -> None:
        """Perform IR finalization."""
        for extern_typ in self.extern_types:
            extern_typ.resolve()

    def register(self, target_header: context.Header) -> None:
        """Register the externed types.

        Args:
            target_header: header file associated with the cpp_target

        target_header may not be the header included in the CppType during registration.  The type will either be
        target_header or the header specified in the definition, depending on use-case.
        """
        for extern_typ in self.extern_types:
            extern_typ.register(target_header, self.header, self.namespace)

    def render(self, enclosing_namespace: str) -> context.CppModuleChunks:
        """Render the externed types to cpp."""
        cpp_mod = context.CppModuleChunks()
        cpp_mod.header_chunk.context.add_include(self.header)
        for extern_typ in self.extern_types:
            cpp_mod.append(extern_typ.render(enclosing_namespace))

        return cpp_mod


@dataclass
class CppExternType(node.CstNode[cst.CppExternType]):
    """An externed type."""

    extern_type_expr: expr.Expr
    subclass_handler: ExternTypeSubclassHandler | None = None

    @classmethod
    def from_cst(cls: type[CppExternType], cst_node: cst.CppExternType, module: node.Module) -> CppExternType:
        """Make CppExternType."""
        extern_type_expr = expr.Expr.from_cst(cst_node.child_cpp_extern_type_name().child_typespec(), module)

        return CppExternType(
            module=module,
            cst_node=cst_node,
            extern_type_expr=extern_type_expr,
        )

    def resolve(self) -> None:
        """Perform IR finalization."""
        # Create the handler first here
        if self.subclass_handler:
            msg = "Already resolved."
            raise TypeError(msg)

        # Implementation explanation
        # A bit unconventional.  A bit limited in when the type can be determined.  It seems appropriate to
        # call expr.evaluate() as part of resolve(), and we only want to resolve once.  This means we
        # can't/shouldn't try to determine type beforehand, or during any class instantiation.  Therefore,
        # we ended up with this compromise where the type-related functions are encapsulated into their
        # own handler (as oppose a design where there is a complete subclass for each type)
        assert self.cst_node  # noqa: S101 (for mypy)
        extern_t = self.extern_type_expr.evaluate()
        if isinstance(extern_t, strongtypes.StrongType):
            self.subclass_handler = StrongTypeHandler.make(extern_t, self.cst_node, self.module)
        elif isinstance(extern_t, extern_type.ExternType):
            self.subclass_handler = CppExternHandler.make(extern_t)
        else:
            msg = "Resolution failed.  Expected a StrongType or ExternType."
            raise TypeError(msg)

        self.subclass_handler.additional_resolve()

    def register(self, target_header: context.Header, type_header: context.Header, namespace: str) -> None:
        """Register the externed type."""
        if not self.subclass_handler:
            msg = "Handler does not exist.  Resolve first."
            raise TypeError(msg)
        self.subclass_handler.register(self.module.context, target_header, type_header, namespace)

    def render(self, enclosing_namespace: str) -> context.CppModuleChunks:
        """Render the externed type to cpp."""
        if not self.subclass_handler:
            msg = "Handler does not exist.  Resolve first."
            raise TypeError(msg)
        return self.subclass_handler.render(self.module.context, enclosing_namespace)


class ExternTypeSubclassHandler(ABC):
    """Internal mechanisms for handling different extern types.

    Create a subclass for each CppExternType and add instantiation logic to CppExternType.resolve().
    """

    @abstractmethod
    def register(
        self,
        compiler_context: CompilerContext,
        target_header: context.Header,
        type_header: context.Header,
        namespace: str,
    ) -> None:
        """Register the externed type."""

    def additional_resolve(self) -> None:
        """Perform additional IR finalization.

        Before invocation, the type expr would've been resolved.  If any addition resolving required, override.
        """
        return

    def render(self, compiler_context: CompilerContext, enclosing_namespace: str) -> context.CppModuleChunks:  # noqa: ARG002 (Unused but required for parent method signature)
        """Render the externed type to cpp.

        If not overridden, returns empty CppModuleChunks
        """
        return context.CppModuleChunks()


@dataclass
class StrongTypeHandler(ExternTypeSubclassHandler):
    """An externed type for strong types."""

    strong_type: strongtypes.StrongType
    factory: str | None
    cst_node: cst.CppExternType
    module: node.Module

    @classmethod
    def make(
        cls: type[StrongTypeHandler],
        strong_type: strongtypes.StrongType,
        cst_node: cst.CppExternType,
        module: node.Module,
    ) -> StrongTypeHandler:
        """Construct handler."""
        if not isinstance(strong_type.typespec, clkbuiltins.PrimitiveType):
            msg = "Must resolve before rendering."
            raise TypeError(msg)

        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        allowlist = [
            f"@{CLK_REPO}::jewels::units::clk::au",
            f"@{CLK_REPO}::cpp_extern_test",
            f"@{CLK_REPO}::clockwork::dsl::cog::ten_nanosecond",
            f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg",
        ]
        if module.module_id.get_fqn() not in allowlist:
            msg = f"Use of cpp_extern is restricted and not allowed in module {module.module_id.get_fqn()} (repo:{module.module_id.repo}, name:{module.module_id.name})."
            raise ValueError(msg)

        factory_block = cst_node.maybe_cpp_extern_type_factory()
        factory = get_span(factory_block.child_factory().child_value(), module.terminals) if factory_block else None

        return StrongTypeHandler(
            module=module,
            cst_node=cst_node,
            strong_type=strong_type,
            factory=factory,
        )

    def register(
        self,
        compiler_context: CompilerContext,  # noqa: ARG002 (Unused but required by parent method signature)
        target_header: context.Header,
        type_header: context.Header,  # noqa: ARG002 (Unused but required by parent method signature)
        namespace: str,
    ) -> None:
        """Register the externed type."""
        typereg.register_cpp_type(
            self.module.context,
            self.strong_type,
            types.CppType(includes=[target_header], type_name=self.strong_type.name, cpp_namespace=namespace),
            # Need to overwrite the default registration.
            overwrite=True,
        )

        assert isinstance(self.strong_type.typespec, clkbuiltins.PrimitiveType)  # noqa: S101 (for mypy)
        if self.factory:
            literal.register_factory_fn(
                self.strong_type,
                self.strong_type.typespec,
                types.CppFn(headers=[target_header], namespace=namespace, name=self.factory),
            )

    def render(self, compiler_context: CompilerContext, enclosing_namespace: str) -> context.CppModuleChunks:
        """Register the externed type to cpp."""
        if not isinstance(self.strong_type, strongtypes.StrongType):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            msg = "Must resolve before rendering."
            raise TypeError(msg)

        if not isinstance(self.strong_type.typespec, clkbuiltins.PrimitiveType):
            msg = "Must resolve before rendering."
            raise TypeError(msg)

        cpp_mod = context.CppModuleChunks()

        cpp_type = typereg.get_cpp_type(compiler_context, self.strong_type)
        constraint = tachyon_reg.constraint_for_type(compiler_context, self.strong_type)
        if not constraint:
            msg = "Unable to get constraint for externed type."
            raise ValueError(msg)

        # Fully qualified name
        fqn = cpp_type.render(enclosing_namespace)
        cpp_mod.implementation_chunk.append(
            [
                f"static_assert(alignof({fqn}) == {constraint.alignment});",
                f"static_assert(sizeof({fqn}) == {constraint.size});",
            ]
        )

        if self.factory:
            factory_fqn = types.CppType([], self.factory, cpp_type.cpp_namespace).render(enclosing_namespace)
            underlying_type = typereg.get_cpp_type(self.module.context, self.strong_type.typespec).render(
                enclosing_namespace
            )
            cpp_mod.implementation_chunk.append(
                f"static_assert(std::is_same<decltype({factory_fqn}(std::declval<{underlying_type}>())), {fqn}>::value);",
            )

        cpp_mod.implementation_chunk.context.add_includes(cpp_type.includes)

        return cpp_mod


@dataclass
class CppExternHandler(ExternTypeSubclassHandler):
    """An generic externed cpp type."""

    extern_typ: extern_type.ExternType
    use_type_header: bool = True  # Fixed right now, though reasonable to use this to drive header modification

    @classmethod
    def make(cls: type[CppExternHandler], extern_typ: extern_type.ExternType) -> CppExternHandler:
        """Construct handler."""
        # Maybe adjust use_type_header based on the intended use

        return CppExternHandler(extern_typ=extern_typ)

    def register(
        self,
        compiler_context: CompilerContext,
        target_header: context.Header,
        type_header: context.Header,
        namespace: str,
    ) -> None:
        """Register the externed type."""
        header = type_header if self.use_type_header else target_header
        typereg.register_cpp_type(
            compiler_context,
            self.extern_typ,
            types.CppType(includes=[header], type_name=self.extern_typ.name, cpp_namespace=namespace),
            # Need to overwrite the default registration.
            overwrite=True,
        )
