# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Registry for which nanobind target binds each type."""

from dataclasses import dataclass
from typing import Final, final

from clockwork.dsl.bazel.targets import Label
from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.ir import clkenum, node, typesys
from clockwork.dsl.ir.module_id import ModuleID
from clockwork.dsl.ir.schema import InstantiatedSchema
from clockwork.dsl.serialization.tap import to_schema_instantiation
from typing_extensions import override

# InstantiatedSchema used to also register Tappy<> instantiation of schema
KeyType = typesys.Instantiation | clkenum.ResolvedEnum | InstantiatedSchema


@dataclass(frozen=True, eq=True, order=True)
class TargetId:
    """Identifier for a target."""

    name: str
    module_id: ModuleID

    def is_this_module(self, module: node.Module) -> bool:
        """Check if the target info matches a given module."""
        return self.module_id == module.module_id

    def to_py_module(self) -> str:
        """Render the python module "foo.bar.baz_nb"."""
        return ".".join([*self.module_id.name.split("::")[:-1], self.name])

    def to_bazel_label(self, current_repo: str) -> Label:
        """Render the bazel label "//foo/bar:baz_nb".."""
        path = self.module_id.get_base_path().parent
        if self.module_id.repo and self.module_id.repo != current_repo:
            return Label(f"@{self.module_id.repo}//{path}:{self.name}")
        return Label(f"//{path}:{self.name}")


@dataclass(frozen=True, eq=True, order=True)
class NanobindTargetInfo:
    """A target in which a binding is declared.

    This is the key information from a binding in a 'NanobindTarget' that we'll need later.
    """

    target_id: TargetId
    maybe_generic_alias: str | None


_SerializedKeyType = str


def _serialize_key(key: KeyType | typesys.TypeVal) -> _SerializedKeyType:
    """For this user-facing key, compute the internal key which is used in the dictionary."""
    return key.value_key()


@final
class NanobindRegistry(Context):
    """Registry for Nanobindings."""

    def __init__(self, name: str | None) -> None:
        """Create an empty nanobind registry."""
        self.name = name
        self.nanobind_registry: dict[_SerializedKeyType, NanobindTargetInfo] = {}

    @override
    def import_from(self, other: "NanobindRegistry") -> None:
        """Combine this registry with registered nanobindings from another.

        Raises:
            RuntimeError: If a nanobinding already exists with a different definition
        """
        for key, target_info in other.nanobind_registry.items():
            if key in self.nanobind_registry and self.nanobind_registry[key] != target_info:
                msg = f"Binding {key} has a conflicting registry entry: {self.nanobind_registry[key]} vs {target_info}\nWhen merging from {other.name} into {self.name}"
                raise RuntimeError(msg)
            self.nanobind_registry[key] = target_info


class NanobindRegistryKey(ContextKey[NanobindRegistry]):
    """Compiler context key for nanobind registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> NanobindRegistry:
        """Default instance of the nanobind registry."""
        return NanobindRegistry(compiler_context.name)


NANOBIND_REGISTRY_KEY: Final = NanobindRegistryKey(name="NanobindRegistry")


def register_binding(key: KeyType, value: NanobindTargetInfo, compiler_context: CompilerContext) -> None:
    """Register one binding for a target."""
    registry = compiler_context[NANOBIND_REGISTRY_KEY]
    key_str = _serialize_key(key)
    old_value = registry.nanobind_registry.get(key_str)
    if old_value is not None:
        msg = f"Error: {key} registered twice to nanobind target registry ({old_value}, {value})."
        raise ValueError(msg)
    registry.nanobind_registry[key_str] = value

    # register both the schema and Tappy<> instantiation to make lookups more robust
    if isinstance(key, typesys.Instantiation):
        register_binding(to_schema_instantiation(key), value, compiler_context)


def lookup_binding(key: KeyType | typesys.TypeVal, compiler_context: CompilerContext) -> NanobindTargetInfo | None:
    """Lookup the target in which a binding is contained."""
    registry = compiler_context[NANOBIND_REGISTRY_KEY]
    key_str = _serialize_key(key)
    return registry.nanobind_registry.get(key_str)
