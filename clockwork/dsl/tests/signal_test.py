# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for signal entity compilation."""

from __future__ import annotations

import pytest
from clockwork.dsl.ir import clkbuiltins, compiler, expr, importer, primitive, schema, signal
from clockwork.dsl.ir.module_id import ModuleID


def test_simple_signal() -> None:
    """Test parsing and compiling a simple signal without options."""
    source = """
// Simple signal with UInt64 type
signal my_signal : UInt64;
"""
    module_id = ModuleID(repo="", name="test_signal")
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, module_id, fs_importer)

    # Verify the signal was created with default values
    assert "my_signal" in module.inner_scope.names
    signal_ir = module.inner_scope.names["my_signal"]
    assert isinstance(signal_ir, signal.Signal)
    assert signal_ir.name == "my_signal"

    # Access resolved properties
    resolved_signal = signal_ir.get_resolved()
    assert not resolved_signal.multi_instance
    assert resolved_signal.metadata is None
    assert resolved_signal.signal_name == signal_ir.fqn


def test_signal_with_name_option() -> None:
    """Test parsing a signal with a custom name option."""
    source = """
// Signal with custom name
signal my_signal : UInt64
{
    name: "custom_signal_name";
}
"""
    module_id = ModuleID(repo="", name="test_signal")
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, module_id, fs_importer)

    # Verify the signal_name was set to the custom value
    assert "my_signal" in module.inner_scope.names
    signal_ir = module.inner_scope.names["my_signal"]
    assert isinstance(signal_ir, signal.Signal)
    assert signal_ir.name == "my_signal"

    # Access resolved properties
    resolved_signal = signal_ir.get_resolved()
    assert isinstance(resolved_signal.signal_name, str)
    assert resolved_signal.signal_name == "custom_signal_name"


def test_signal_with_all_options() -> None:
    """Test parsing signals with all combinations of options."""
    source = """
// Define a schema to use as metadata
schema SignalMetadata
{
    fields
    {
        // Timestamp when the signal was emitted
        #0 timestamp : SyncTime;
        // ID of the source that emitted the signal
        #1 source_id : UInt32;
    }
}

// Signal with all options
signal full_featured_signal : Float32
{
    name: "full_signal_name";
    multi_instance: true;
    metadata: SignalMetadata;
}

// Signal with just multi_instance
signal multi_instance_signal : Int32
{
    multi_instance: true;
}

// Signal with just metadata
signal metadata_signal : Float64
{
    metadata: SyncTime;
}
"""
    module_id = ModuleID(repo="", name="test_signal")
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, module_id, fs_importer)

    # Verify signal with all options
    assert "full_featured_signal" in module.inner_scope.names
    full_signal = module.inner_scope.names["full_featured_signal"]
    assert isinstance(full_signal, signal.Signal)
    assert full_signal.name == "full_featured_signal"

    # Access resolved properties
    resolved_full = full_signal.get_resolved()
    assert resolved_full.multi_instance is True
    assert resolved_full.metadata is not None
    assert isinstance(resolved_full.signal_name, str)
    assert resolved_full.signal_name == "full_signal_name"

    assert "SignalMetadata" in module.inner_scope.names
    signal_metadata_schema = module.inner_scope.names["SignalMetadata"]
    assert isinstance(signal_metadata_schema, schema.Schema)
    assert isinstance(resolved_full.metadata, schema.Schema)
    assert resolved_full.metadata is signal_metadata_schema

    # Verify signal with just multi_instance
    assert "multi_instance_signal" in module.inner_scope.names
    multi_signal = module.inner_scope.names["multi_instance_signal"]
    assert isinstance(multi_signal, signal.Signal)

    resolved_multi = multi_signal.get_resolved()
    assert resolved_multi.multi_instance is True
    assert resolved_multi.metadata is None

    # Verify signal with just metadata
    assert "metadata_signal" in module.inner_scope.names
    metadata_signal = module.inner_scope.names["metadata_signal"]
    assert isinstance(metadata_signal, signal.Signal)

    resolved_metadata = metadata_signal.get_resolved()
    assert not resolved_metadata.multi_instance
    assert resolved_metadata.metadata is not None
    assert resolved_metadata.metadata is clkbuiltins.SYNC_TIME


def test_signal_with_invalid_schema_type() -> None:
    """Test that signals reject schema types (non-scalar types)."""
    source = """
// This should fail - signals can't use schema types
schema Point
{
    fields
    {
        // x
        #0 x: Float32;
        // y
        #1 y: Float32;
    }
}

// a position signal
signal position : Point;
"""
    module_id = ModuleID(repo="", name="test_signal")
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)

    with pytest.raises(TypeError) as exc_info:
        compiler.compile_source_text(source, module_id, fs_importer)

    # Verify the error message mentions that schemas aren't allowed
    assert "scalar value" in str(exc_info.value)
    assert "Schemas, arrays, optionals" in str(exc_info.value)


def test_signal_with_pre_aggregation() -> None:
    """Test that signals can specify pre-aggregation types."""
    source = """
// Signal with pre-aggregation
signal sensor_reading : Float32
{
    pre_aggregation: ["min", "max", "mean"];
}
"""
    module_id = ModuleID(repo="", name="test_signal")
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, module_id, fs_importer)

    # Verify the signal was created with pre-aggregation
    assert "sensor_reading" in module.inner_scope.names
    signal_ir = module.inner_scope.names["sensor_reading"]
    assert isinstance(signal_ir, signal.Signal)
    assert len(signal_ir.pre_aggregation) == 3
    assert signal.AggregationType.MIN in signal_ir.pre_aggregation
    assert signal.AggregationType.MAX in signal_ir.pre_aggregation
    assert signal.AggregationType.MEAN in signal_ir.pre_aggregation
    assert signal_ir.pre_aggregation_text == '["min", "max", "mean"]'


def test_signal_without_pre_aggregation_defaults_to_value() -> None:
    """Test that signals without pre-aggregation default to 'value'."""
    source = """
// Signal without pre-aggregation
signal simple_signal : UInt32;
"""
    module_id = ModuleID(repo="", name="test_signal")
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, module_id, fs_importer)

    # Verify the signal defaults to value aggregation
    assert "simple_signal" in module.inner_scope.names
    signal_ir = module.inner_scope.names["simple_signal"]
    assert isinstance(signal_ir, signal.Signal)
    assert len(signal_ir.pre_aggregation) == 1
    assert signal.AggregationType.VALUE in signal_ir.pre_aggregation
    assert signal_ir.pre_aggregation_text == "value"


def test_signal_with_all_aggregation_types() -> None:
    """Test signal with all valid aggregation types."""
    source = """
// Signal with all aggregation types
signal comprehensive_signal : Int64
{
    pre_aggregation: ["min", "max", "sum", "count", "mean", "final_value", "first_value"];
}
"""
    module_id = ModuleID(repo="", name="test_signal")
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, module_id, fs_importer)

    # Verify all aggregation types are present
    assert "comprehensive_signal" in module.inner_scope.names
    signal_ir = module.inner_scope.names["comprehensive_signal"]
    assert isinstance(signal_ir, signal.Signal)
    assert len(signal_ir.pre_aggregation) == 7
    assert signal.AggregationType.MIN in signal_ir.pre_aggregation
    assert signal.AggregationType.MAX in signal_ir.pre_aggregation
    assert signal.AggregationType.SUM in signal_ir.pre_aggregation
    assert signal.AggregationType.COUNT in signal_ir.pre_aggregation
    assert signal.AggregationType.MEAN in signal_ir.pre_aggregation
    assert signal.AggregationType.FINAL_VALUE in signal_ir.pre_aggregation
    assert signal.AggregationType.FIRST_VALUE in signal_ir.pre_aggregation


def test_signal_with_invalid_aggregation_type() -> None:
    """Test that signals reject invalid pre-aggregation types."""
    source = """
// This should fail - invalid aggregation type
signal bad_signal : Float32
{
    pre_aggregation: ["min", "invalid_agg", "max"];
}
"""
    module_id = ModuleID(repo="", name="test_signal")
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)

    with pytest.raises(TypeError) as exc_info:
        compiler.compile_source_text(source, module_id, fs_importer)

    # Verify the error message mentions the invalid aggregation
    assert "Invalid pre-aggregation type" in str(exc_info.value)
    assert "invalid_agg" in str(exc_info.value)


def test_signal_subscript_operations() -> None:
    """Test signal subscript operations with string indices, wildcards, and error cases."""
    source = """
// Multi-instance signal
signal multi_signal : UInt32 { multi_instance: true; }
// Non-multi-instance signal
signal single_signal : Float64;
"""
    module_id = ModuleID(repo="", name="test_signal")
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, module_id, fs_importer)

    multi_signal_ir = module.inner_scope.names["multi_signal"]
    single_signal_ir = module.inner_scope.names["single_signal"]
    assert isinstance(multi_signal_ir, signal.Signal)
    assert isinstance(single_signal_ir, signal.Signal)

    # Test string index subscript
    instance_name = primitive.StringValue(value="instance1", type_info=clkbuiltins.STRING)
    result = multi_signal_ir.evaluate_subscript(
        index=instance_name,
        cst_node=multi_signal_ir.cst_node,  # pyright: ignore[reportArgumentType]
        module=module,
    )
    assert isinstance(result, signal.SignalInstanceSpec)
    assert result.signal is multi_signal_ir
    assert isinstance(result.instance_key, primitive.StringValue)
    assert result.instance_key.value == "instance1"
    assert not result.is_wildcard()

    # Test wildcard subscript
    wildcard_result = multi_signal_ir.evaluate_subscript(
        index=expr.WILDCARD,
        cst_node=multi_signal_ir.cst_node,  # pyright: ignore[reportArgumentType]
        module=module,
    )
    assert isinstance(wildcard_result, signal.SignalInstanceSpec)
    assert wildcard_result.is_wildcard()

    # Test value keys are unique
    instance2_name = primitive.StringValue(value="instance2", type_info=clkbuiltins.STRING)
    result2 = multi_signal_ir.evaluate_subscript(
        index=instance2_name,
        cst_node=multi_signal_ir.cst_node,  # pyright: ignore[reportArgumentType]
        module=module,
    )
    assert result.value_key() != result2.value_key()
    assert "SignalInstance" in result.value_key()
    assert "instance1" in result.value_key()
    assert "*" in wildcard_result.value_key()
