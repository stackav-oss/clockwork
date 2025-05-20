# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Registry for which nanobind target binds each type."""

from dataclasses import dataclass

from clockwork.dsl.bazel.targets import Label
from clockwork.dsl.ir import clkenum, node, typesys
from clockwork.dsl.ir.module_id import ModuleID
from clockwork.dsl.ir.schema import InstantiatedSchema
from clockwork.dsl.serialization.tap import to_schema_instantiation

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


_NANOBIND_REGISTRY: dict[_SerializedKeyType, NanobindTargetInfo] = {}


def register_binding(key: KeyType, value: NanobindTargetInfo) -> None:
    """Register one binding for a target."""
    key_str = _serialize_key(key)
    old_value = _NANOBIND_REGISTRY.get(key_str)
    if old_value is not None:
        msg = f"Error: {key} registered twice to nanobind target registry ({old_value}, {value})."
        raise ValueError(msg)
    _NANOBIND_REGISTRY[key_str] = value

    # register both the schema and Tappy<> instantiation to make lookups more robust
    if isinstance(key, typesys.Instantiation):
        register_binding(to_schema_instantiation(key), value)


def lookup_binding(key: KeyType | typesys.TypeVal) -> NanobindTargetInfo | None:
    """Lookup the target in which a binding is contained."""
    key_str = _serialize_key(key)
    return _NANOBIND_REGISTRY.get(key_str)
