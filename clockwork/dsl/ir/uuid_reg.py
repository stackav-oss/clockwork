# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Global registry for entity UUIDs."""

from __future__ import annotations

from typing import Final
from uuid import UUID, uuid5

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.ir.node import enrich_error_if_possible
from clockwork.dsl.ir.typesys import NamedAttribute, NamedValue, Value
from typing_extensions import override


class UuidRegistry(Context):
    """Registry for entity UUIDs."""

    def __init__(self, name: str | None) -> None:
        """Create a new, empty UUID registry."""
        self.name = name
        self.key_to_uuid: dict[str, UUID] = {}
        self.uuid_to_ir: dict[UUID, Value] = {}

    @override
    def import_from(self, other: UuidRegistry) -> None:
        """Combine this registry with items from another.

        Raises:
            RuntimeError: If a entity already exists with a different UUID or vice versa.
        """
        for key, uuid in other.key_to_uuid.items():
            if key in self.key_to_uuid and self.key_to_uuid[key] != uuid:
                ours = self.uuid_to_ir[self.key_to_uuid[key]]
                theirs = other.uuid_to_ir[uuid]
                msg = f"Entity with key {key} has conflicting UUIDs"
                msg += enrich_error_if_possible(theirs, "Submodule UUID {uuid}")
                msg += enrich_error_if_possible(ours, "Current UUID {self.key_to_uuid[key]}")
                raise RuntimeError(msg)
            self.key_to_uuid[key] = uuid

        for uuid, entity in other.uuid_to_ir.items():
            if uuid in self.uuid_to_ir and self.uuid_to_ir[uuid] is not entity:
                msg = f"UUID {uuid} has conflicting entities"
                ours = self.uuid_to_ir[uuid]
                msg += enrich_error_if_possible(entity, "Submodule entity")
                msg += enrich_error_if_possible(ours, "Current entity")
                raise RuntimeError(msg)
            self.uuid_to_ir[uuid] = entity


class UuidRegistryKey(ContextKey[UuidRegistry]):
    """Compiler context key for UUID registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> UuidRegistry:
        """Create a default instance of the registry."""
        return UuidRegistry(compiler_context.name)


UUID_REGISTRY_KEY: Final = UuidRegistryKey("UuidRegistry")


_UUID_NAMESPACE = UUID("25772509-bfc5-4639-907f-863d58baac95")


def uuid_from_name(name: str) -> UUID:
    """Create a UUID from a unique name."""
    return uuid5(_UUID_NAMESPACE, name)


def register_entity(compiler_context: CompilerContext, entity: Value, name: str) -> UUID:
    """Register an entity with an internally generated UUID based on name.

    Args:
        compiler_context: Compiler context containing the registry.
        entity: The Value to register
        name: Unique name representing the entity and will be part of the hash to generate its UUID

    Returns:
        Generated UUID

    Raises:
        Exceptions raised by register_uuid
    """
    uuid = uuid_from_name(name)
    register_uuid(compiler_context, entity, uuid)
    return uuid


def register_entity_with_stable_key(compiler_context: CompilerContext, entity: NamedAttribute | NamedValue) -> UUID:
    """Register an entity with stable key, which will be used the key as well as the name for UUID hashing.

    Args:
        compiler_context: Compiler context containing the registry.
        entity: The entity to register

    Returns:
        Generated UUID

    Raises:
        Exceptions raised by register_uuid
    """
    name = entity.value_key()
    return register_entity(compiler_context, entity, name)


def register_uuid(compiler_context: CompilerContext, entity: Value, uuid: UUID) -> None:
    """Register an entity with user-provided UUID.

    Args:
        compiler_context: Compiler context containing the registry.
        entity: The Value to register
        uuid: UUID to use

    Raises:
        RuntimeError: if entity is already registered or if uuid is already in use
    """
    registry = compiler_context[UUID_REGISTRY_KEY]
    key = entity.value_key()
    if key in registry.key_to_uuid:
        msg = f"Entity with value_key {key} already exists."
        raise RuntimeError(msg)
    if uuid in registry.uuid_to_ir:
        msg = f"UUID {uuid!s} already exists."
        raise RuntimeError(msg)
    registry.key_to_uuid[key] = uuid
    registry.uuid_to_ir[uuid] = entity


def lookup_uuid(compiler_context: CompilerContext, entity: Value | str) -> UUID:
    """Look up UUID for an entity.

    Args:
        compiler_context: Compiler context containing the registry.
        entity: The Value/str to look up

    Returns:
        Entity's UUID

    Raises:
        RuntimeError: if entity is not in the registry
    """
    registry = compiler_context[UUID_REGISTRY_KEY]
    key = entity.value_key() if isinstance(entity, Value) else entity
    if key not in registry.key_to_uuid:
        raise RuntimeError("Entity with key '" + key + "' does not exist in the registry.")
    return registry.key_to_uuid[key]


def lookup_entity(compiler_context: CompilerContext, uuid: UUID) -> Value:
    """Look up Value entity by UUID.

    Args:
        compiler_context: Compiler context containing the registry.
        uuid: UUID to look up

    Return:
        Value entity

    Raises:
        RuntimeError: if UUID is not in the registry
    """
    registry = compiler_context[UUID_REGISTRY_KEY]
    if uuid not in registry.uuid_to_ir:
        raise RuntimeError("UUID " + str(uuid) + " does not exist in the registry.")
    return registry.uuid_to_ir[uuid]
