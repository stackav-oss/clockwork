# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for cog_metrics_schema_generation."""

from pathlib import Path

from clockwork.dsl.ir import clkbuiltins, clkenum, cog, compiler, importer, primitive, schema, typesys
from clockwork.dsl.ir.cog_metrics_schema_generation import Decimal
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


def test_generate_metrics_schemas() -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellocog.clk")),
        importer=importer.FilesystemImporter(compile_fn=compiler.compile_source_file),
    )

    cog_ir = module.inner_scope.lookup("HelloCog")
    assert isinstance(cog_ir, cog.Cog)
    schemas = list(cog_ir.generated_schemas())
    # Make sure we have the expected 3 metrics schemas.
    assert len(schemas) == 3
    assert any(schema.name == "HelloCogTelemetryMetrics" for schema in schemas)
    assert any(schema.name == "HelloCogEventMetrics" for schema in schemas)
    assert any(schema.name == "HelloCogEventMetricsBatch" for schema in schemas)

    # Because we know that all the schemas are there from the above assertions, we can
    # just iterate through and validate each one.
    for metrics_schema in schemas:
        if metrics_schema.name == "HelloCogTelemetryMetrics":
            validate_telemetry_metrics_schema(metrics_schema)
        elif metrics_schema.name == "HelloCogEventMetrics":
            validate_event_metrics_schema(metrics_schema)
        elif metrics_schema.name == "HelloCogEventMetricsBatch":
            validate_event_metrics_batch_schema(metrics_schema)


def get_field_by_name(metrics_schema: schema.Schema, field_name: str) -> schema.FieldDef:
    for field in metrics_schema.fields.values():
        if field.cur_name == field_name:
            return field

    msg = f"Field '{field_name}' not found in schema '{metrics_schema.name}'"
    raise AssertionError(msg)


def validate_telemetry_metrics_output(metrics_schema: schema.Schema, output_name: str) -> None:
    output = get_field_by_name(metrics_schema, output_name)
    assert isinstance(output.type_info, schema.InstantiatedSchema)
    assert output.type_info.schema_name == "MinMaxMean"
    assert output.type_info.arguments
    assert output.type_info.arguments["data_type"] == clkbuiltins.UINT16


def validate_telemetry_metrics_input(metrics_schema: schema.Schema, input_name: str) -> None:
    input_field = get_field_by_name(metrics_schema, input_name)
    assert isinstance(input_field.type_info, schema.InstantiatedSchema)
    assert input_field.type_info.schema_name == "InputChannelTelemetryMetrics"


def validate_telemetry_metrics_condition(metrics_schema: schema.Schema, condition_name: str) -> None:
    condition_field = get_field_by_name(metrics_schema, f"{condition_name}_trigger_vals")
    assert condition_field.type_info == clkbuiltins.UINT16


def validate_telemetry_metrics_schema(metrics_schema: schema.Schema) -> None:
    assert metrics_schema.name == "HelloCogTelemetryMetrics"

    validate_telemetry_metrics_input(metrics_schema, "latest_hello")
    validate_telemetry_metrics_input(metrics_schema, "multi_publisher_hello")
    validate_telemetry_metrics_input(metrics_schema, "history_of_hellos")

    validate_telemetry_metrics_output(metrics_schema, "out_world_num_messages")
    validate_telemetry_metrics_output(metrics_schema, "out_goodbye_num_messages")
    validate_telemetry_metrics_output(metrics_schema, "out_multi1_num_messages")
    validate_telemetry_metrics_output(metrics_schema, "out_multi2_num_messages")

    validate_telemetry_metrics_condition(metrics_schema, "any_msg")
    validate_telemetry_metrics_condition(metrics_schema, "new_msg")
    validate_telemetry_metrics_condition(metrics_schema, "periodic")

    common_telemetry_metrics = get_field_by_name(metrics_schema, "common_telemetry_metrics")
    assert isinstance(common_telemetry_metrics.type_info, schema.InstantiatedSchema)
    assert common_telemetry_metrics.type_info.schema_name == "CogTelemetryMetrics"


def validate_event_metrics_output(metrics_schema: schema.Schema, output_name: str) -> None:
    output = get_field_by_name(metrics_schema, output_name)
    assert output.type_info == clkbuiltins.UINT16


def validate_event_metrics_input(metrics_schema: schema.Schema, input_name: str) -> None:
    input_field = get_field_by_name(metrics_schema, input_name)
    assert isinstance(input_field.type_info, schema.InstantiatedSchema)
    assert input_field.type_info.schema_name == "InputChannelEventMetrics"


def validate_event_metrics_schema(metrics_schema: schema.Schema) -> None:
    assert metrics_schema.name == "HelloCogEventMetrics"
    validate_event_metrics_input(metrics_schema, "latest_hello")
    validate_event_metrics_input(metrics_schema, "multi_publisher_hello")
    validate_event_metrics_input(metrics_schema, "history_of_hellos")

    validate_event_metrics_output(metrics_schema, "out_world_num_messages")
    validate_event_metrics_output(metrics_schema, "out_goodbye_num_messages")
    validate_event_metrics_output(metrics_schema, "out_multi1_num_messages")
    validate_event_metrics_output(metrics_schema, "out_multi2_num_messages")

    trigger_flags = get_field_by_name(metrics_schema, "trigger_flags")
    assert isinstance(trigger_flags.type_info, clkenum.ClkEnum)
    assert trigger_flags.type_info.underlying_type == clkbuiltins.UINT8
    assert trigger_flags.type_info.bit_flags

    common_event_metrics = get_field_by_name(metrics_schema, "common_event_metrics")
    assert isinstance(common_event_metrics.type_info, schema.InstantiatedSchema)
    assert common_event_metrics.type_info.schema_name == "CogEventMetrics"


def validate_event_metrics_batch_schema(metrics_schema: schema.Schema) -> None:
    assert metrics_schema.name == "HelloCogEventMetricsBatch"
    event_metrics = get_field_by_name(metrics_schema, "event_metrics")
    assert isinstance(event_metrics.type_info, typesys.Instantiation)
    assert event_metrics.type_info.instantiates == clkbuiltins.VAR_ARRAY
    event_metrics_type = event_metrics.type_info.arguments["type"]
    assert isinstance(event_metrics_type, schema.InstantiatedSchema)
    assert event_metrics_type.schema_name == "HelloCogEventMetrics"
    assert event_metrics.type_info.arguments["max_size"] == primitive.DecimalValue(clkbuiltins.INT64, Decimal(10))
