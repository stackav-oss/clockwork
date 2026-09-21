# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pub_sub."""

from __future__ import annotations

from uuid import UUID, uuid4

import pytest
from clockwork.dsl.composition import pdf
from clockwork.dsl.ir import compiler
from clockwork.dsl.ir.importer import FilesystemImporter


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_cog_instance() -> None:
    cog1 = pdf.CogInstanceDescription(
        cog_class_id=uuid4(),
        cog_instance_id=uuid4(),
        endpoints=[pdf.EndpointInstanceDescription(endpoint_class_id=uuid4(), endpoint_instance_id=uuid4())],
        instance_path_name="asdf",
    )
    buffer = bytearray(pdf.CogInstanceDescription.get_tachyon_constraint().size)
    cog1.serialize_tachyon(memoryview(buffer))
    cog2 = pdf.CogInstanceDescription.deserialize_tachyon(memoryview(bytes(buffer)))
    assert cog1 == cog2


def test_process_description() -> None:
    pd1 = pdf.ProcessDescription(
        process_id=uuid4(),
        cog_instances=[],
        state_graph=pdf.StateGraph(state_instances=[], connections=[]),
        config_graph=pdf.ConfigGraph(config_instances=[], connections=[]),
        pubsub_graph=pdf.PubSubGraph(publish_endpoints=[], connections=[]),
        memory_resource_graph=pdf.MemoryResourceGraph(memory_resources=[], connections=[]),
        timers=[],
        init_cogs=[],
        log_cog=uuid4(),
        io_connections=[],
        not_connected_endpoints=[],
        snapshot_configs=[],
        data_sources=[],
    )
    buffer = bytearray(pdf.ProcessDescription.get_tachyon_constraint().size)
    pd1.serialize_tachyon(memoryview(buffer))
    pd2 = pdf.ProcessDescription.deserialize_tachyon(memoryview(bytes(buffer)))
    assert pd1 == pd2


@pytest.mark.parametrize("snapshot_representation_id", [None, uuid4()])
def test_state_instance_description_roundtrip(snapshot_representation_id: UUID | None) -> None:
    """Round-trip the optional external-state snapshot representation."""
    state = pdf.StateInstanceDescription(
        representation_id=uuid4(),
        state_instance_id=uuid4(),
        instance_path_name="state",
        snapshot_representation_id=snapshot_representation_id,
        maybe_buffer_layout=None,
        maybe_memory_resource=uuid4(),
        init_data_source=pdf.DEFAULT_CONSTRUCT_DATA_SOURCE_SENTINEL,
    )
    buffer = bytearray(pdf.StateInstanceDescription.get_tachyon_constraint().size)
    state.serialize_tachyon(memoryview(buffer))
    assert pdf.StateInstanceDescription.deserialize_tachyon(memoryview(bytes(buffer))) == state
