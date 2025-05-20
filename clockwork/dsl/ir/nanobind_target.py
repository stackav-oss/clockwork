# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""NanobindTarget-related IR nodes."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from clockwork.dsl import cst
from clockwork.dsl.bazel.nanobind_targets import PyCcBinding
from clockwork.dsl.bazel.targets import get_bazel_label_for_clk_label
from clockwork.dsl.cpp.context import CppModuleChunks, as_cc_library, write_to_file
from clockwork.dsl.ir import nanobinding_registry, node
from clockwork.dsl.ir.cst_util import get_span
from clockwork.dsl.ir.nanobind_binding import NanobindBinding
from clockwork.dsl.ir.nanobind_target_render import get_dependent_targets, render_nanobind_bindings_module
from clockwork.dsl.ir.path_resolver import BazelPathResolver
from clockwork.dsl.ir.statement import ImmutableBinding
from jewels.nanobind.clk_bindings.common import common_clk_nanobind_libraries

if TYPE_CHECKING:
    from pathlib import Path

    from clockwork.dsl.bazel.cc_targets import CcLibrary


@dataclass
class NanobindTarget(node.NamedEntity, node.DocableEntity, node.CstNode[cst.NanobindTarget]):
    """IR for NanobindTargets."""

    nanobind_bindings: list[NanobindBinding]
    constants: dict[str, node.Deferrable[ImmutableBinding]]

    @classmethod
    def from_cst(cls: type[NanobindTarget], cst_node: cst.NanobindTarget, module: node.Module) -> NanobindTarget:
        """Create an IR NanobindTarget from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), terminals=module.terminals)
        nanobind_bindings = []
        constants: dict[str, node.Deferrable[ImmutableBinding]] = {}
        for statement_cst in cst_node.children_nanobind_target_statement():
            if nanobind_binding_cst := statement_cst.maybe_nanobind_binding():
                nanobind_binding_ir = NanobindBinding.from_cst(nanobind_binding_cst, module)
                nanobind_bindings.append(nanobind_binding_ir)
            elif constant_cst := statement_cst.maybe_target_constant():
                constant_lookup = node.DeferredLookup.make(
                    expected_type=ImmutableBinding,
                    cst_identifier=constant_cst.child_constant_name(),
                    terminals=module.terminals,
                )
                if constant_lookup.identifier in constants:
                    msg = node.append_error_line(
                        constant_cst,
                        module,
                        f"Constant {constant_lookup.identifier} is already included in target {name}",
                    )
                    raise ValueError(msg)
                constants[constant_lookup.identifier] = constant_lookup
            else:
                msg = node.append_error_line(statement_cst, module, "Unrecognized statement within nanobind_target")
                raise NotImplementedError(msg)

        return cls(
            module=module,
            cst_node=cst_node,
            doc=doc,
            name=name,
            scope=module.inner_scope,
            nanobind_bindings=nanobind_bindings,
            constants=constants,
        )

    def _get_target_id(self) -> nanobinding_registry.TargetId:
        """Get the TargetId of this nanobind target."""
        return nanobinding_registry.TargetId(
            name=self.name,
            module_id=self.module.module_id,
        )

    def _make_nanobind_target_info(self, maybe_generic_alias: str | None) -> nanobinding_registry.NanobindTargetInfo:
        """Get the NanobindTargetInfo which is the value in the nanobind registry key-value store.."""
        return nanobinding_registry.NanobindTargetInfo(
            target_id=self._get_target_id(),
            maybe_generic_alias=maybe_generic_alias,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        for entity in self.nanobind_bindings:
            entity.resolve()
            # Each bindings registers which target it came from.
            # This is used to compute target dependencies because nanobind modules need to import their dependencies.
            # It's also used for clk-deps to manage the dependencies automatically.
            nanobinding_registry.register_binding(
                entity.get_resolved().original_type, self._make_nanobind_target_info(entity.get_resolved().alias_name)
            )

    def _get_constants(self) -> list[ImmutableBinding]:
        """Get a list of all constants exported in this nanobind_target.

        This is also where we assert that Deferred lookups have been resolved.
        """
        constants = []
        for constant in self.constants.values():
            assert isinstance(constant, ImmutableBinding)  # noqa: S101 invariant
            constants.append(constant)
        return constants

    def render_cpp_entities(self) -> CppModuleChunks:
        """Render one nanobind C++ module definition."""
        cpp_mod = CppModuleChunks()

        resolved_bindings = [binding.get_resolved() for binding in self.nanobind_bindings]
        this_target = self._get_target_id()
        dependent_targets = get_dependent_targets(resolved_bindings, this_target)
        cpp_mod.implementation_chunk.append(
            render_nanobind_bindings_module(
                self.module.context,
                resolved_bindings,
                self._get_constants(),
                self.name,
                dependent_targets,
            )
        )

        return cpp_mod

    def render_and_write(self, root_dir: Path) -> None:
        """Convert to C++ and write output to files."""
        include_dir = self.module.module_id.get_base_path().parent
        write_dir = root_dir / BazelPathResolver().to_buildtime_path(self.module.module_id).parent

        cpp_mod = self.render_cpp_entities()
        write_to_file(cpp_mod, write_dir, include_dir, self.name, self.module.module_id.repo)

    def output_targets(self) -> list[PyCcBinding]:
        """Extract language target dependency information."""
        include_dir = self.module.module_id.get_base_path().parent
        cpp_mod = self.render_cpp_entities()
        # Process needs to manage namespace differently, therefore not included as part of `render_cpp_entities`
        main_target: CcLibrary = as_cc_library(cpp_mod, self.name, include_dir, self.module.module_id.repo)

        # compute target dependencies
        dependent_targets = get_dependent_targets(
            [binding.get_resolved() for binding in self.nanobind_bindings], self._get_target_id()
        )

        # To avoid churn in many build files when internal details change, there is a meta-package of common dependencies
        # that we use. Filter out all dependencies in that common target and depend directly on the meta-target.
        common_cc_deps = {
            get_bazel_label_for_clk_label(self.module.module_id.repo, dep)
            for dep in common_clk_nanobind_libraries.COMMON_CC_DEPS
            if dep
        }
        common_py_deps = {
            get_bazel_label_for_clk_label(self.module.module_id.repo, dep)
            for dep in common_clk_nanobind_libraries.COMMON_PY_DEPS
        }
        cc_deps = sorted(
            [
                get_bazel_label_for_clk_label(
                    self.module.module_id.repo, "//jewels/nanobind/clk_bindings/common:common_cc"
                ),
                *[cc_dep for cc_dep in main_target.deps if cc_dep not in common_cc_deps],
            ]
        )
        py_deps = sorted(
            [
                get_bazel_label_for_clk_label(
                    self.module.module_id.repo, "//jewels/nanobind/clk_bindings/common:common_py"
                ),
                *[
                    target_id.to_bazel_label(self.module.module_id.repo)
                    for target_id in dependent_targets.get_python_targets()
                    if target_id.to_bazel_label(self.module.module_id.repo) not in common_py_deps
                ],
            ]
        )

        return [
            PyCcBinding(
                name=main_target.name,
                srcs=[*main_target.srcs, *main_target.hdrs],
                deps=cc_deps,
                py_deps=py_deps,
            )
        ]
