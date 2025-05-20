# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Implements Clockwork module Importers."""

from collections.abc import Callable
from dataclasses import dataclass, field

from clockwork.dsl.ir import node
from clockwork.dsl.ir.module_id import ModuleID
from clockwork.dsl.ir.path_resolver import BazelPathResolver, PathResolver

_MODULE_CACHE: dict[ModuleID, node.Module] = {}


@dataclass
class FilesystemImporter:
    """Implementation of Importer protocol that loads from the filesystem."""

    compile_fn: Callable[[ModuleID, node.Importer], node.Module]

    path_resolver: PathResolver = field(default_factory=BazelPathResolver)

    def resolve_import(self, enclosing_module: node.Module, use_result: node.Module.UseResult) -> node.ImportSpec:
        """Resolve how a UseResult will be interpreted.

        Returns:
            A specification of how to perform the import.
        """
        if not use_result.path:
            msg = f"Cannot import an empty path: {use_result}"
            raise ValueError(msg)

        repo = enclosing_module.module_id.repo
        if use_result.repo == repo:
            msg = node.append_error_line(
                use_result.cst_node,
                enclosing_module,
                f"Using statement is within repo '{repo}' and redundantly specifies the repo prefix '{use_result.repo}'.",
            )
            raise ValueError(msg)

        repo_to_use = use_result.repo if use_result.repo else repo

        import_name = use_result.path[-1] if use_result.alias is None else use_result.alias
        if "external" in use_result.path:
            msg = f"Found external in module path: {use_result.path}"
            raise ValueError(msg)
        module_id = ModuleID(repo_to_use, "::".join(use_result.path))
        if self.path_resolver.find_path(module_id):
            return node.ImportSpec(module_id=module_id, entity_name=None, import_name=import_name)

        module_id = ModuleID(repo_to_use, "::".join(use_result.path[:-1]))
        if len(use_result.path) > 1 and self.path_resolver.find_path(module_id):
            entity_name = use_result.path[-1]
            return node.ImportSpec(module_id=module_id, entity_name=entity_name, import_name=import_name)

        msg = node.append_error_line(
            use_result.cst_node,
            enclosing_module,
            f"Module not found: {ModuleID(repo_to_use, '::'.join(use_result.path)).get_fqn()}",
        )
        raise FileNotFoundError(msg)

    def execute_import(
        self, spec: node.ImportSpec, enclosing_module: node.Module, use_result: node.Module.UseResult
    ) -> tuple[node.Module, node.NamedEntity | None]:
        """Execute an import.

        Returns:
            The imported module and, optionally, a specific entity to import from it.
        """
        compiled_module = _MODULE_CACHE.get(spec.module_id)
        if compiled_module is None:
            compiled_module = self.compile_fn(spec.module_id, self)

        if spec.entity_name is None:
            return compiled_module, None
        if (entity := compiled_module.inner_scope.lookup(spec.entity_name, recursive=False)) is None:
            msg = f'Entity named "{spec.entity_name}" not found in module at "{spec.module_id.get_base_path()}"'
            if spec.module_id.repo:
                msg += f' from repository "{spec.module_id.repo}"'
            msg = node.append_error_line(use_result.cst_node, enclosing_module, msg)
            raise ValueError(msg)
        return compiled_module, entity

    def try_cached_load(self, module_id: ModuleID) -> node.Module | None:
        """Load a module from the Module cache.

        Returns:
            The Module if it's already loaded and cached, otherwise None
        """
        return _MODULE_CACHE.get(module_id)

    def cache_module(self, module_id: ModuleID, module: node.Module) -> None:
        """Add a compiled module to the Module cache."""
        _MODULE_CACHE[module_id] = module
