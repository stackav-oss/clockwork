# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Test uuid_reg."""

from typing import cast
from uuid import uuid5

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir import typesys, uuid_reg


class MockType(typesys.TypeVal, typesys.ObjectIdentityValue):
    """Mock type."""


@pytest.fixture()
def compiler_context() -> CompilerContext:
    """Create a fresh compiler context for testing."""
    return CompilerContext("test")


@pytest.fixture()
def typeval() -> typesys.TypeVal:
    return MockType(type_info=cast("typesys.TypeVal", None))


def test_register_uuid(compiler_context: CompilerContext, typeval: typesys.TypeVal) -> None:
    """Register an entity via register_uuid."""
    uuid = uuid5(uuid_reg._UUID_NAMESPACE, "hello_world")
    uuid_reg.register_uuid(compiler_context, typeval, uuid)

    # Access the registry from the context
    registry = compiler_context[uuid_reg.UUID_REGISTRY_KEY]
    assert len(registry.key_to_uuid) == 1
    assert len(registry.uuid_to_ir) == 1
    assert registry.key_to_uuid[typeval.value_key()] == uuid
    assert registry.uuid_to_ir[uuid] == typeval


def test_register_entity(compiler_context: CompilerContext, typeval: typesys.TypeVal) -> None:
    """Register an entity via register_entity."""
    name = "hello_world"
    expected_uuid = uuid5(uuid_reg._UUID_NAMESPACE, name)
    uuid_reg.register_entity(compiler_context, typeval, name)

    registry = compiler_context[uuid_reg.UUID_REGISTRY_KEY]
    assert len(registry.key_to_uuid) == 1
    assert len(registry.uuid_to_ir) == 1
    assert registry.key_to_uuid[typeval.value_key()] == expected_uuid
    assert registry.uuid_to_ir[expected_uuid] == typeval


def test_register_multiple_entities(compiler_context: CompilerContext) -> None:
    """Make sure can register multiple non-conflicting entities."""
    num_to_register = 10
    vals_uuids = [
        (
            MockType(type_info=cast("typesys.TypeVal", None)),
            uuid5(uuid_reg._UUID_NAMESPACE, f"hell_world:{i}"),
        )
        for i in range(num_to_register)
    ]
    # Register
    for typeval, uuid in vals_uuids:
        uuid_reg.register_uuid(compiler_context, typeval, uuid)

    # Check
    registry = compiler_context[uuid_reg.UUID_REGISTRY_KEY]
    assert len(registry.key_to_uuid) == num_to_register
    assert len(registry.uuid_to_ir) == num_to_register
    for typeval, uuid in vals_uuids:
        assert registry.key_to_uuid[typeval.value_key()] == uuid
        assert registry.uuid_to_ir[uuid] == typeval


def test_conflict(compiler_context: CompilerContext) -> None:
    """Test key and uuid conflict detection."""
    name1 = "hello_world:1"
    id1 = uuid5(uuid_reg._UUID_NAMESPACE, name1)
    val1 = MockType(type_info=cast("typesys.TypeVal", None))
    uuid_reg.register_uuid(compiler_context, val1, id1)

    # Conflicting Value/value_key with val1
    name2 = "hello_world:2"
    id2 = uuid5(uuid_reg._UUID_NAMESPACE, name2)
    val2 = val1
    with pytest.raises(RuntimeError, match=r"Entity with value_key .* already exists."):
        uuid_reg.register_uuid(compiler_context, val2, id2)

    # Conflicting UUID val1
    name3 = name1
    id3 = uuid5(uuid_reg._UUID_NAMESPACE, name3)
    val3 = MockType(type_info=cast("typesys.TypeVal", None))
    with pytest.raises(RuntimeError, match=r"UUID .* already exists."):
        uuid_reg.register_uuid(compiler_context, val3, id3)


def test_lookup_uuid(compiler_context: CompilerContext) -> None:
    """Test UUID lookup."""
    # Manually create entries for the registry
    num_to_register = 5
    entity_uuid = [
        (
            MockType(type_info=cast("typesys.TypeVal", None)),
            uuid5(uuid_reg._UUID_NAMESPACE, f"hello_world:{i}"),
        )
        for i in range(num_to_register)
    ]

    registry = compiler_context[uuid_reg.UUID_REGISTRY_KEY]
    for entity, uuid in entity_uuid:
        key = entity.value_key()
        registry.key_to_uuid[key] = uuid
        registry.uuid_to_ir[uuid] = entity

    # Lookup, using both Value and str arguments
    for entity, uuid in entity_uuid:
        key = entity.value_key()
        assert uuid_reg.lookup_uuid(compiler_context, key) == uuid
        assert uuid_reg.lookup_uuid(compiler_context, entity) == uuid

    # Look for non-existent key
    bad_entity = MockType(type_info=cast("typesys.TypeVal", None))
    with pytest.raises(RuntimeError, match=r"Entity with key .* does not exist .*"):
        uuid_reg.lookup_uuid(compiler_context, bad_entity.value_key())
    with pytest.raises(RuntimeError, match=r"Entity with key .* does not exist .*"):
        uuid_reg.lookup_uuid(compiler_context, bad_entity)


def test_lookup_entity(compiler_context: CompilerContext) -> None:
    """Test entity lookup."""
    # Manually create entries for the registry
    num_to_register = 5
    uuid_entity = [
        (
            uuid5(uuid_reg._UUID_NAMESPACE, f"hello_world:{i}"),
            MockType(type_info=cast("typesys.TypeVal", None)),
        )
        for i in range(num_to_register)
    ]

    registry = compiler_context[uuid_reg.UUID_REGISTRY_KEY]
    for uuid, entity in uuid_entity:
        registry.key_to_uuid[entity.value_key()] = uuid
        registry.uuid_to_ir[uuid] = entity

    # Lookup
    for uuid, entity in uuid_entity:
        assert uuid_reg.lookup_entity(compiler_context, uuid) == entity

    # Lookup a bad UUID
    bad_uuid = uuid5(uuid_reg._UUID_NAMESPACE, "bad_name")
    with pytest.raises(RuntimeError, match=r"UUID .* does not exist .*"):
        uuid_reg.lookup_entity(compiler_context, bad_uuid)


def test_import_from() -> None:
    """Test import_from merging registries."""
    # Create two contexts
    context1 = CompilerContext("context1")
    context2 = CompilerContext("context2")

    # Add different entries to each context
    entity1 = MockType(type_info=cast("typesys.TypeVal", None))
    uuid1 = uuid5(uuid_reg._UUID_NAMESPACE, "entity1")
    uuid_reg.register_uuid(context1, entity1, uuid1)

    entity2 = MockType(type_info=cast("typesys.TypeVal", None))
    uuid2 = uuid5(uuid_reg._UUID_NAMESPACE, "entity2")
    uuid_reg.register_uuid(context2, entity2, uuid2)

    # Import context2 into context1
    registry1 = context1[uuid_reg.UUID_REGISTRY_KEY]
    registry2 = context2[uuid_reg.UUID_REGISTRY_KEY]
    registry1.import_from(registry2)

    # Check that context1 now has both entries
    assert uuid_reg.lookup_uuid(context1, entity1) == uuid1
    assert uuid_reg.lookup_uuid(context1, entity2) == uuid2
    assert uuid_reg.lookup_entity(context1, uuid1) == entity1
    assert uuid_reg.lookup_entity(context1, uuid2) == entity2


def test_import_from_conflicts() -> None:
    """Test import_from error handling with conflicting entries."""
    # Create two contexts
    context1 = CompilerContext("context1")
    context2 = CompilerContext("context2")

    # Same entity with different UUIDs
    entity = MockType(type_info=cast("typesys.TypeVal", None))
    uuid1 = uuid5(uuid_reg._UUID_NAMESPACE, "uuid1")
    uuid2 = uuid5(uuid_reg._UUID_NAMESPACE, "uuid2")

    uuid_reg.register_uuid(context1, entity, uuid1)

    # Need to manually add to context2 to create the conflict
    registry2 = context2[uuid_reg.UUID_REGISTRY_KEY]
    registry2.key_to_uuid[entity.value_key()] = uuid2
    registry2.uuid_to_ir[uuid2] = entity

    # Attempt to import should fail due to conflicting entries
    registry1 = context1[uuid_reg.UUID_REGISTRY_KEY]
    with pytest.raises(RuntimeError, match=r"Entity with key .* has conflicting UUIDs"):
        registry1.import_from(registry2)

    # Test uuid conflict - same uuid pointing to different entities
    context3 = CompilerContext("context3")
    context4 = CompilerContext("context4")

    entity3 = MockType(type_info=cast("typesys.TypeVal", None))
    entity4 = MockType(type_info=cast("typesys.TypeVal", None))
    uuid3 = uuid5(uuid_reg._UUID_NAMESPACE, "uuid3")

    uuid_reg.register_uuid(context3, entity3, uuid3)

    # Manually add conflicting entry to context4
    registry4 = context4[uuid_reg.UUID_REGISTRY_KEY]
    registry4.key_to_uuid[entity4.value_key()] = uuid3
    registry4.uuid_to_ir[uuid3] = entity4

    registry3 = context3[uuid_reg.UUID_REGISTRY_KEY]
    with pytest.raises(RuntimeError, match=r"UUID .* has conflicting entities"):
        registry3.import_from(registry4)
