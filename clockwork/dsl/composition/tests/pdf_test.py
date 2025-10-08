# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pub_sub."""

from __future__ import annotations

from uuid import uuid4

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
    )
    buffer = bytearray(pdf.ProcessDescription.get_tachyon_constraint().size)
    pd1.serialize_tachyon(memoryview(buffer))
    pd2 = pdf.ProcessDescription.deserialize_tachyon(memoryview(bytes(buffer)))
    assert pd1 == pd2
