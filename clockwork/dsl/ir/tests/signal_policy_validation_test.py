# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false
"""Unit tests for signal policy validation."""

import pytest
from clockwork.dsl.ir import compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import ModuleID
from clockwork.dsl.ir.signal_policy_validation import validate_signal_policies


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_validate_signal_policies_detects_conflicts(fs_importer: FilesystemImporter) -> None:
    """Ensure overlapping signal policies raise an error during validation."""
    source = """
// Schema for policy
schema TestConfig
{
    fields
    {
        // Doc
        #0 enabled: Bool;
    }
}

// Policy definition
def policy TestPolicy
{
    binds_to: Signal;
    schema: TestConfig;
}

signal sensor_data : Float32
{
    multi_instance: true;
}

// Conflict: specific instance and wildcard for same policy
policy TestPolicy for sensor_data["sensor1"]
{
    enabled = true;
}

policy TestPolicy for sensor_data[*]
{
    enabled = false;
}

box ChildBox
{
}

system_target log_runner_example_system
{
    box: ChildBox;
}

"""
    with pytest.raises(
        ValueError,
        match=r"Policy 'TestPolicy' applies to signal 'policy_conflict.sensor_data' instance 'sensor1' more than once",
    ):
        compiler.compile_source_text(source, ModuleID(repo="", name="policy_conflict"), importer=fs_importer)


def test_validate_signal_policies_disallow_entire_multi_signal(fs_importer: FilesystemImporter) -> None:
    """Policies must not target a multi-instance signal without selecting an instance."""
    source = """
// Schema for policy
schema TestConfig
{
    fields
    {
        // Doc
        #0 enabled: Bool;
    }
}

// Policy definition
def policy TestPolicy
{
    binds_to: Signal;
    schema: TestConfig;
}

signal sensor_data : Float32
{
    multi_instance: true;
}

// Invalid: policy targets entire multi-instance signal
policy TestPolicy for sensor_data
{
    enabled = true;
}
"""

    module = compiler.compile_source_text(source, ModuleID(repo="", name="policy_multi_signal"), importer=fs_importer)

    with pytest.raises(ValueError, match=r"multi-instance signal"):
        validate_signal_policies(module)
