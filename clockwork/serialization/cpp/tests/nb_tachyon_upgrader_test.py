# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Nanobind tachyon upgrader test."""

import pytest
from clockwork.serialization.cpp import nb_tachyon_upgrader
from clockwork.serialization.metadata import tachyon as tachyon_metadata
from clockwork.serialization.metadata import tachyon_model_pb2 as model_pb2
from clockwork.serialization.py.tests.support import (
    simple_schema_v1_clk_py,
    simple_schema_v2_clk_py,
)


def test_python_upgrader() -> None:
    """Test upgrading with the python upgrader."""
    v1_instance = simple_schema_v1_clk_py.SimpleSchemaV1()
    v1_instance.integer_field = 42
    v1_buffer = bytearray(simple_schema_v1_clk_py.SimpleSchemaV1.get_tachyon_constraint().size)
    v1_instance.serialize_tachyon(memoryview(v1_buffer))

    v2_metadata = simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_metadata()
    v2_metadata.python_required = True

    upgrader = nb_tachyon_upgrader.TachyonPythonUpgrader(
        simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_module_name(),
        simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_source_file_name(),
        simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_class_name(),
        tachyon_metadata.to_protobuf(v2_metadata).SerializeToString(),
        tachyon_metadata.to_protobuf(simple_schema_v1_clk_py.SimpleSchemaV1.get_tachyon_metadata()).SerializeToString(),
        simple_schema_v1_clk_py.SimpleSchemaV1.get_tachyon_class_name(),
    )
    assert upgrader.upgrade_required is True
    assert upgrader.upgrader_type == "python"

    v2_buffer = bytearray(simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_constraint().size)
    upgrader.upgrade(memoryview(v1_buffer), memoryview(v2_buffer))

    v2_instance = simple_schema_v2_clk_py.SimpleSchemaV2.deserialize_tachyon(memoryview(v2_buffer))
    assert v2_instance.integer_field == 42
    assert v2_instance.string_field == ""


def test_cpp_upgrader() -> None:
    """Test upgrading with the python upgrader."""
    v1_instance = simple_schema_v1_clk_py.SimpleSchemaV1()
    v1_instance.integer_field = 42
    v1_buffer = bytearray(simple_schema_v1_clk_py.SimpleSchemaV1.get_tachyon_constraint().size)
    v1_instance.serialize_tachyon(memoryview(v1_buffer))

    v2_metadata = simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_metadata()
    v2_metadata.python_required = False

    upgrader = nb_tachyon_upgrader.TachyonCppUpgrader(
        simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_class_name(),
        tachyon_metadata.to_protobuf(v2_metadata).SerializeToString(),
        tachyon_metadata.to_protobuf(simple_schema_v1_clk_py.SimpleSchemaV1.get_tachyon_metadata()).SerializeToString(),
    )
    assert upgrader.upgrade_required is True
    assert upgrader.upgrader_type == "cpp"

    v2_buffer = bytearray(simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_constraint().size)
    upgrader.upgrade(memoryview(v1_buffer), memoryview(v2_buffer))

    v2_instance = simple_schema_v2_clk_py.SimpleSchemaV2.deserialize_tachyon(memoryview(v2_buffer))
    assert v2_instance.integer_field == 42
    assert v2_instance.string_field == ""


def test_memcpy_upgrader_with_python_required_true() -> None:
    """Test upgrading with the memcpy upgrader with python required set to true."""
    v1_instance = simple_schema_v2_clk_py.SimpleSchemaV2()
    v1_instance.integer_field = 42
    v1_instance.string_field = "test"
    v1_buffer = bytearray(simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_constraint().size)
    v1_instance.serialize_tachyon(memoryview(v1_buffer))

    v2_metadata = simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_metadata()
    v2_metadata.python_required = True

    upgrader = nb_tachyon_upgrader.TachyonPythonUpgrader(
        simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_module_name(),
        simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_source_file_name(),
        simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_class_name(),
        tachyon_metadata.to_protobuf(v2_metadata).SerializeToString(),
        tachyon_metadata.to_protobuf(simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_metadata()).SerializeToString(),
        simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_class_name(),
    )
    assert upgrader.upgrade_required is False
    assert upgrader.upgrader_type == "memcpy"

    v2_buffer = bytearray(simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_constraint().size)
    upgrader.upgrade(memoryview(v1_buffer), memoryview(v2_buffer))

    v2_instance = simple_schema_v2_clk_py.SimpleSchemaV2.deserialize_tachyon(memoryview(v2_buffer))
    assert v2_instance.integer_field == 42
    assert v2_instance.string_field == "test"


def test_memcpy_upgrader_with_python_required_false() -> None:
    """Test upgrading with the memcpy upgrader with python required set to false."""
    v1_instance = simple_schema_v2_clk_py.SimpleSchemaV2()
    v1_instance.integer_field = 42
    v1_instance.string_field = "test"
    v1_buffer = bytearray(simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_constraint().size)
    v1_instance.serialize_tachyon(memoryview(v1_buffer))

    v2_metadata = simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_metadata()
    v2_metadata.python_required = False

    upgrader = nb_tachyon_upgrader.TachyonCppUpgrader(
        simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_class_name(),
        tachyon_metadata.to_protobuf(v2_metadata).SerializeToString(),
        tachyon_metadata.to_protobuf(simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_metadata()).SerializeToString(),
    )
    assert upgrader.upgrade_required is False
    assert upgrader.upgrader_type == "memcpy"

    v2_buffer = bytearray(simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_constraint().size)
    upgrader.upgrade(memoryview(v1_buffer), memoryview(v2_buffer))

    v2_instance = simple_schema_v2_clk_py.SimpleSchemaV2.deserialize_tachyon(memoryview(v2_buffer))
    assert v2_instance.integer_field == 42
    assert v2_instance.string_field == "test"


def test_validate_tachyon_types_compatibility_newer_version() -> None:
    """Test validation when incoming metadata is newer version."""
    v1_metadata = simple_schema_v1_clk_py.SimpleSchemaV1.get_tachyon_metadata()
    v1_metadata.python_required = False

    # Newer version should raise ValueError
    with pytest.raises(RuntimeError, match=r"with version 3 .* with version 1"):
        _ = nb_tachyon_upgrader.TachyonCppUpgrader(
            simple_schema_v1_clk_py.SimpleSchemaV1.get_tachyon_class_name(),
            tachyon_metadata.to_protobuf(v1_metadata).SerializeToString(),
            tachyon_metadata.to_protobuf(
                simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_metadata()
            ).SerializeToString(),
        )


def test_validate_logged_channel_metatadata_success() -> None:
    """Test validating logged channel metadata that can be upgraded."""
    v1_metadata = simple_schema_v1_clk_py.SimpleSchemaV1.get_tachyon_metadata()
    prev_metadata = model_pb2.LoggedChannelMetadata()
    prev_metadata.channel_metadata["channel1"].CopyFrom(tachyon_metadata.to_protobuf(v1_metadata))
    v2_metadata = simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_metadata()
    curr_metadata = model_pb2.LoggedChannelMetadata()
    curr_metadata.channel_metadata["channel1"].CopyFrom(tachyon_metadata.to_protobuf(v2_metadata))
    assert nb_tachyon_upgrader.validate_logged_channel_metadata(
        prev_metadata.SerializeToString(), curr_metadata.SerializeToString()
    )


def test_validate_logged_channel_metatadata_failure() -> None:
    """Test validating logged channel metadata that cannot be upgraded."""
    v2_metadata = simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_metadata()
    prev_metadata = model_pb2.LoggedChannelMetadata()
    prev_metadata.channel_metadata["channel1"].CopyFrom(tachyon_metadata.to_protobuf(v2_metadata))
    v1_metadata = simple_schema_v1_clk_py.SimpleSchemaV1.get_tachyon_metadata()
    curr_metadata = model_pb2.LoggedChannelMetadata()
    curr_metadata.channel_metadata["channel1"].CopyFrom(tachyon_metadata.to_protobuf(v1_metadata))
    assert not nb_tachyon_upgrader.validate_logged_channel_metadata(
        prev_metadata.SerializeToString(), curr_metadata.SerializeToString()
    )
