# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Tests for cog metrics schema generation."""

from pathlib import Path

import pytest
from clockwork.dsl.ir import clkbuiltins, compiler, importer, node, schema, typesys
from clockwork.dsl.ir.clkenum import ClkEnum
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.tests.support.py_test_utils import fix_clockwork_path


@pytest.fixture()
def metrics_file_path() -> Path:
    """Fixture to provide the path to the metrics file."""
    return fix_clockwork_path(Path("clockwork/dsl/tests/support/hellocog_metrics.clk"))


@pytest.fixture()
def metrics_module(metrics_file_path: Path) -> node.Module:
    """Fixture to provide the compiled metrics module."""
    # Ensure the metrics file exists
    assert metrics_file_path.exists()
    # Compile the metrics file
    return compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, metrics_file_path),
        importer=importer.FilesystemImporter(compile_fn=compiler.compile_source_file),
    )


@pytest.fixture()
def metrics_schemas(metrics_module: node.Module) -> dict[str, schema.Schema]:
    """Fixture to provide the schema objects from the compiled module."""
    schemas = {}
    # Get all schema objects from the module
    for obj in metrics_module.inner_scope.names.values():
        if isinstance(obj, schema.Schema):
            schemas[obj.name] = obj
    return schemas


@pytest.fixture()
def metrics_enums(metrics_module: node.Module) -> dict[str, ClkEnum]:
    """Fixture to provide the enum objects from the compiled module."""
    enums = {}
    # Get all enum objects from the module
    for obj in metrics_module.inner_scope.names.values():
        if isinstance(obj, ClkEnum):
            enums[obj.name] = obj
    return enums


def test_schemas_exist(metrics_schemas: dict[str, schema.Schema]) -> None:
    """Test that expected schemas are present."""
    expected_schemas = {"HelloCogTelemetryMetrics", "HelloCogEventMetrics", "BatchedHelloCogEventMetrics"}

    assert set(metrics_schemas.keys()) >= expected_schemas, "Not all expected schemas were generated"


def test_trigger_flags_enum_exists(metrics_enums: dict[str, ClkEnum]) -> None:
    """Test that the condition trigger flags enum is present."""
    assert "HelloCogTriggerFlags" in metrics_enums, "Condition trigger flags enum not generated"

    trigger_enum = metrics_enums["HelloCogTriggerFlags"]

    # Check that the enum has bit_flags option
    assert trigger_enum.bit_flags is True, "Trigger flags enum should have bit_flags option enabled"

    # Check that the enum has underlying type option
    assert trigger_enum.underlying_type == clkbuiltins.UINT8, (
        "Trigger flags enum should have underlying type option set to UInt8"
    )


def test_trigger_flags_enum_values(metrics_enums: dict[str, ClkEnum]) -> None:
    """Test that the condition trigger flags enum has correct values."""
    trigger_enum = metrics_enums["HelloCogTriggerFlags"]

    # Check that there's at least one value (none) plus values for each condition
    # HelloCog has 3 conditions: any_msg, new_msg, periodic
    assert len(trigger_enum.values) == 4, "Trigger flags enum should have values for 'none' plus each condition"

    # Find the 'none' value
    none_value = None
    for value_def in trigger_enum.values.values():
        if value_def.name == "none":
            none_value = value_def
            break

    assert none_value is not None, "Trigger flags enum should have a 'none' value"
    assert none_value.is_default, "The 'none' value should be marked as default"

    # Check that we have the expected condition names
    expected_conditions = {"any_msg", "new_msg", "periodic"}
    condition_names = {
        value_def.name for field_num, value_def in trigger_enum.values.items() if value_def.name != "none"
    }
    assert condition_names == expected_conditions, "Enum should have values for each condition"

    # Verify resolved integer values follow bit flag pattern (1, 2, 4, 8...)
    # This requires the enum to be resolved
    resolved_enum = trigger_enum.get_resolved()
    values_by_name = {
        value_def.name: value_def.integer_value
        for value_def in resolved_enum.values.values()
        if value_def.name != "none"
    }

    # Check bit flag pattern: each value should be a power of 2
    for name, value in values_by_name.items():
        # A power of 2 has exactly one bit set, so value & (value - 1) should be 0
        assert value & (value - 1) == 0, f"Value for '{name}' ({value}) is not a power of 2"
        # Also, value should be >= 1
        assert value >= 1, f"Value for '{name}' ({value}) is less than 1"


def test_telemetry_metrics_common_fields(metrics_schemas: dict[str, schema.Schema]) -> None:
    """Test that the telemetry metrics schema has the common fields."""
    telemetry_schema = metrics_schemas["HelloCogTelemetryMetrics"]
    common_field = get_field_by_name(telemetry_schema, "common")

    assert isinstance(common_field, schema.FieldDef)
    assert isinstance(common_field.type_info, typesys.TypeDef)
    assert common_field.type_info.name == "CogTelemetryMetrics"


def test_event_metrics_common_fields(metrics_schemas: dict[str, schema.Schema]) -> None:
    """Test that the event metrics schema has the common fields."""
    event_schema = metrics_schemas["HelloCogEventMetrics"]
    common_field = get_field_by_name(event_schema, "common")

    assert isinstance(common_field, schema.FieldDef)
    assert isinstance(common_field.type_info, typesys.TypeDef)
    assert common_field.type_info.name == "CogEventMetrics"


def test_batched_metrics_fields(metrics_schemas: dict[str, schema.Schema]) -> None:
    """Test that the batched event metrics schema has the expected fields."""
    batched_schema = metrics_schemas["BatchedHelloCogEventMetrics"]
    events_field = get_field_by_name(batched_schema, "events")

    assert isinstance(events_field, schema.FieldDef), "BatchedHelloCogEventMetrics should include events field"

    assert isinstance(events_field.type_info, typesys.Instantiation)

    assert events_field.type_info.instantiates == clkbuiltins.VAR_ARRAY, (
        "BatchedHelloCogEventMetrics events field should be a VarArray"
    )
    underlying_events_type = events_field.type_info.arguments["type"]
    assert isinstance(underlying_events_type, typesys.TypeDef)
    assert underlying_events_type.name == "HelloCogEventMetrics", (
        "BatchedHelloCogEventMetrics events field should be a VarArray of HelloCogEventMetrics"
    )


def test_input_channel_metrics(metrics_schemas: dict[str, schema.Schema]) -> None:
    """Test that input channel metrics are present and use correct containers."""
    telemetry_schema = metrics_schemas["HelloCogTelemetryMetrics"]
    event_schema = metrics_schemas["HelloCogEventMetrics"]

    # Check for metrics for each input
    expected_inputs = ["latest_hello", "multi_publisher_hello", "history_of_hellos"]

    # Check telemetry metrics
    for input_name in expected_inputs:
        field = get_field_by_name(telemetry_schema, input_name)
        assert isinstance(field, schema.FieldDef), f"HelloCogTelemetryMetrics should include {input_name} field"
        assert isinstance(field.type_info, typesys.TypeDef)
        assert field.type_info.name == "InputChannelTelemetryMetrics", (
            f"{input_name} field should be of type InputChannelTelemetryMetrics"
        )

    # Check event metrics
    for input_name in expected_inputs:
        field = get_field_by_name(event_schema, input_name)
        assert isinstance(field, schema.FieldDef), f"HelloCogEventMetrics should include {input_name} field"
        assert isinstance(field.type_info, typesys.TypeDef)
        assert field.type_info.name == "InputChannelEventMetrics", (
            f"{input_name} field should be of type InputChannelEventMetrics"
        )


def test_output_channel_metrics(metrics_schemas: dict[str, schema.Schema]) -> None:
    """Test that output channel metrics are present."""
    telemetry_schema = metrics_schemas["HelloCogTelemetryMetrics"]
    event_schema = metrics_schemas["HelloCogEventMetrics"]

    # Check for metrics for each output
    expected_outputs = ["out_world", "out_goodbye", "out_multi1", "out_multi2"]

    # Check telemetry metrics
    for output_name in expected_outputs:
        field_name = f"{output_name}_num_messages"
        field = get_field_by_name(telemetry_schema, field_name)
        assert isinstance(field, schema.FieldDef), f"HelloCogTelemetryMetrics should include {field_name} field"
        assert isinstance(field.type_info, typesys.Instantiation)
        assert isinstance(field.type_info.instantiates, schema.Schema)
        assert field.type_info.instantiates.name == "MinMaxMean"
        assert field.type_info.arguments["data_type"] == clkbuiltins.UINT16, (
            f"{field_name} should be MinMaxMean<UInt16> type"
        )

    # Check event metrics
    for output_name in expected_outputs:
        field_name = f"{output_name}_num_messages"
        field = get_field_by_name(event_schema, field_name)
        assert isinstance(field, schema.FieldDef), f"HelloCogEventMetrics should include {field_name} field"
        assert field.type_info == clkbuiltins.UINT16, f"{field_name} should be UInt16 type"


def test_condition_trigger_flags(metrics_schemas: dict[str, schema.Schema]) -> None:
    """Test that condition trigger flags are present in event metrics."""
    event_schema = metrics_schemas["HelloCogEventMetrics"]

    # HelloCog has 2-3 conditions, so should have at least one trigger flag field
    flag_field = get_field_by_name(event_schema, "trigger_flags_0")
    assert isinstance(flag_field, schema.FieldDef), "HelloCogEventMetrics should include trigger_flags_0 field"
    assert flag_field.type_info == clkbuiltins.UINT8, "trigger_flags field should be UInt8 type"


def test_cpp_target_includes_trigger_enum(metrics_module: node.Module) -> None:
    """Test that the cpp_target includes the trigger flags enum."""
    from clockwork.dsl.ir import cpp_target

    # Find the cpp_target in the module
    target_obj = None
    for obj in metrics_module.inner_scope.names.values():
        if isinstance(obj, cpp_target.CppTarget):
            target_obj = obj
            break

    assert target_obj is not None, "Module should contain a CppTarget definition"

    # Check that the target includes the trigger flags enum
    enum_names = []
    for tag in target_obj.enums:
        assert isinstance(tag.enum_ir, ClkEnum)
        enum_names.append(tag.enum_ir.name)

    assert "HelloCogTriggerFlags" in enum_names, "CppTarget should include the trigger flags enum"


def test_cpp_target_definition(metrics_module: node.Module) -> None:
    """Test that the cpp_target is correctly defined in the module."""
    from clockwork.dsl.ir import cpp_target

    # Find the cpp_target in the module
    target_obj = None
    for obj in metrics_module.inner_scope.names.values():
        if isinstance(obj, cpp_target.CppTarget):
            target_obj = obj
            break

    assert target_obj is not None, "Module should contain a CppTarget definition"
    assert target_obj.name == "hellocog_metrics", "CppTarget should be named 'hellocog_metrics'"
    assert target_obj.options.namespace == "clockwork::testing::cogs::metrics", (
        "CppTarget namespace should be 'clockwork::testing::cogs::metrics'"
    )

    # Check that the target includes all the schemas
    schema_names = [
        s.schema_ir.name
        for s in target_obj.schema_tags
        if hasattr(s, "schema_ir") and isinstance(s.schema_ir, schema.Schema)
    ]

    expected_schemas = {"HelloCogTelemetryMetrics", "HelloCogEventMetrics", "BatchedHelloCogEventMetrics"}
    assert all(name in schema_names for name in expected_schemas), "CppTarget should include all metrics schemas"


def get_field_by_name(schema_obj: schema.Schema, field_name: str) -> schema.FieldDef | None:
    """Helper to get a schema field by name."""
    for field in schema_obj.fields.values():
        if field.cur_name == field_name:
            return field
    return None
