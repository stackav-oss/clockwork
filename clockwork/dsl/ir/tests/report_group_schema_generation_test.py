# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for report group schema generation."""

import pytest
from clockwork.dsl.ir import clkbuiltins, cog, compiler, schema, typesys
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def get_field_by_name(
    schema_ir: schema.Schema | schema.ResolvedSchema, field_name: str
) -> schema.FieldDef | schema.ResolvedFieldDef:
    """Helper to get a field by name from a schema."""
    for field in schema_ir.fields.values():
        if field.cur_name == field_name:
            return field

    msg = f"Field '{field_name}' not found in schema '{schema_ir.name}'"
    raise AssertionError(msg)


def field_exists(schema_ir: schema.Schema | schema.ResolvedSchema, field_name: str) -> bool:
    """Check if a field exists in a schema."""
    return any(field.cur_name == field_name for field in schema_ir.fields.values())


def get_inner_schema_from_var_soa_field(
    schema_ir: schema.Schema | schema.ResolvedSchema, field_name: str
) -> schema.Schema | schema.ResolvedSchema:
    """Extract the inner schema from a VarSoa field.

    Args:
        schema_ir: The schema containing the VarSoa field.
        field_name: The name of the VarSoa field.

    Returns:
        The inner schema used as the type argument for VarSoa.
    """
    field = get_field_by_name(schema_ir, field_name)
    assert isinstance(field.type_info, typesys.Instantiation)
    assert field.type_info.instantiates == clkbuiltins.VAR_SOA
    inner_schema = field.type_info.arguments["type"]
    assert isinstance(inner_schema, schema.InstantiatedSchema)
    return inner_schema.schema


def test_post_aggregated_comprehensive(fs_importer: FilesystemImporter) -> None:
    """Comprehensive test for post-aggregated report group schema generation.

    Tests:
    - Common fields (execution_count, execution_interval)
    - Basic signals with post-aggregation
    - Combined pre and post aggregation
    - All aggregation types (min, max, sum, count, mean, first_value, final_value)
    - Mean aggregation converting to Float32
    - Metadata preservation and stripping
    """
    source = """
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupPolicyConfig, ReportGroupLogType};

// Doc.
cog ComprehensiveCog
{
    signals test_group
    {
        // Basic signal with post-aggregation
        basic_signal: signal UInt64
        {
            post_aggregation: ["min", "max"];
        }

        // Signal with pre and post aggregation
        combined_signal: signal Duration
        {
            pre_aggregation: ["min", "max"];
            post_aggregation: ["min", "final_value"];
        }

        // Signal with mean aggregation (should become Float32)
        mean_signal: signal UInt32
        {
            pre_aggregation: ["sum"];
            post_aggregation: ["mean", "max"];
        }

        // Signal with all aggregation types to verify each works
        all_agg_signal: signal UInt32
        {
            post_aggregation: ["min", "max", "sum", "count", "mean", "first_value", "final_value"];
        }

        // Signal with metadata that is preserved
        metadata_preserved: signal Int64
        {
            metadata: SyncTime;
            pre_aggregation: ["min"];
            post_aggregation: ["max"];
        }

        // Signal with metadata stripped by aggregation
        metadata_stripped: signal UInt32
        {
            metadata: SyncTime;
            pre_aggregation: ["sum"];
            post_aggregation: ["sum"];
        }
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

policy ReportGroupPolicy for ComprehensiveCog.test_group
{
    reporting_strategy = ReportingStrategy::post_aggregated;
    log_type = ReportGroupLogType::telemetry;
    min_observations = 5;
    max_observations = 100;
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "comprehensive_test"), importer=fs_importer)
    test_cog = module.inner_scope.lookup("ComprehensiveCog")
    assert isinstance(test_cog, cog.Cog)

    report_group = next(iter(test_cog.report_groups.values()))
    report_group.resolve(module.inner_scope, "ComprehensiveCog")
    generated_schema = report_group.generated_outer_schema
    assert isinstance(generated_schema, schema.InstantiatedSchema)

    # Verify common fields
    execution_count = get_field_by_name(generated_schema.schema, "execution_count")
    assert execution_count.type_info == clkbuiltins.UINT16
    execution_interval = get_field_by_name(generated_schema.schema, "execution_interval")
    assert execution_interval.type_info == clkbuiltins.DURATION

    # Test basic signal with default pre-aggregation
    basic_min = get_field_by_name(generated_schema.schema, "basic_signal_value_min")
    assert basic_min.type_info == clkbuiltins.UINT64
    basic_max = get_field_by_name(generated_schema.schema, "basic_signal_value_max")
    assert basic_max.type_info == clkbuiltins.UINT64

    # Test combined pre and post aggregation (2 pre * 2 post = 4 fields)
    for pre_agg in ["min", "max"]:
        for post_agg in ["min", "final_value"]:
            field_name = f"combined_signal_{pre_agg}_{post_agg}"
            field = get_field_by_name(generated_schema.schema, field_name)
            assert field.type_info == clkbuiltins.DURATION

    # Test mean aggregation converts to Float32
    mean_field = get_field_by_name(generated_schema.schema, "mean_signal_sum_mean")
    assert mean_field.type_info == clkbuiltins.FLOAT32
    max_field = get_field_by_name(generated_schema.schema, "mean_signal_sum_max")
    assert max_field.type_info == clkbuiltins.UINT32  # max doesn't convert to float

    # Test all aggregation types - non-mean should preserve original type
    for agg_type in ["min", "max", "sum", "count", "first_value", "final_value"]:
        field_name = f"all_agg_signal_value_{agg_type}"
        field = get_field_by_name(generated_schema.schema, field_name)
        assert field.type_info == clkbuiltins.UINT32
    # Mean should be Float32
    all_agg_mean = get_field_by_name(generated_schema.schema, "all_agg_signal_value_mean")
    assert all_agg_mean.type_info == clkbuiltins.FLOAT32

    # Test metadata preservation
    preserved_field = get_field_by_name(generated_schema.schema, "metadata_preserved_min_max")
    assert preserved_field.type_info == clkbuiltins.INT64
    preserved_metadata = get_field_by_name(generated_schema.schema, "metadata_preserved_min_max_metadata")
    assert preserved_metadata.type_info == clkbuiltins.SYNC_TIME

    # Test metadata stripping
    stripped_field = get_field_by_name(generated_schema.schema, "metadata_stripped_sum_sum")
    assert stripped_field.type_info == clkbuiltins.UINT32
    assert not field_exists(generated_schema.schema, "metadata_stripped_sum_sum_metadata")


def test_batched_comprehensive(fs_importer: FilesystemImporter) -> None:
    """Comprehensive test for batched report group schema generation.

    Tests:
    - Outer schema structure (execution_count, execution_interval, signals VarSoa)
    - Inner SoA schema fields for various pre-aggregation types
    - Mean pre-aggregation expanded to sum and count fields
    - Deduplication when mean is combined with explicit sum/count
    - Metadata preservation and stripping
    - SoA enabled on inner schema
    """
    source = """
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupPolicyConfig, ReportGroupLogType};

// Doc.
cog BatchedCog
{
    signals batch_group
    {
        // Basic signal (uses default "value" pre-aggregation)
        basic_signal: signal UInt64;

        // Signal with multiple pre-aggregations
        multi_pre_agg: signal Int64
        {
            pre_aggregation: ["min", "max", "sum"];
        }

        // Mean pre-aggregation should be expanded to sum and count
        mean_signal: signal Float32
        {
            pre_aggregation: ["mean"];
        }

        // Mean with explicit sum should not duplicate sum field
        mean_and_sum: signal Int32
        {
            pre_aggregation: ["mean", "sum"];
        }

        // Mean with explicit count should not duplicate count field
        mean_and_count: signal Int32
        {
            pre_aggregation: ["mean", "count"];
        }

        // Mean with both sum and count should not duplicate either
        mean_sum_count: signal Int32
        {
            pre_aggregation: ["mean", "sum", "count"];
        }

        // Metadata preserved for min aggregation
        preserved_metadata: signal Int64
        {
            metadata: SyncTime;
            pre_aggregation: ["min"];
        }

        // Metadata stripped for mean aggregation (sum and count don't preserve metadata)
        stripped_metadata: signal UInt32
        {
            metadata: SyncTime;
            pre_aggregation: ["mean"];
        }
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

policy ReportGroupPolicy for BatchedCog.batch_group
{
    reporting_strategy = ReportingStrategy::batched;
    log_type = ReportGroupLogType::telemetry;
    max_observations = 100;
}
"""
    module = compiler.compile_source_text(
        source, ModuleID(CLK_REPO, "batched_comprehensive_test"), importer=fs_importer
    )
    batched_cog = module.inner_scope.lookup("BatchedCog")
    assert isinstance(batched_cog, cog.Cog)

    report_group = next(iter(batched_cog.report_groups.values()))
    report_group.resolve(module.inner_scope, "BatchedCog")
    generated_schema = report_group.generated_outer_schema
    assert generated_schema
    # Verify outer schema has correct name and common fields
    assert generated_schema.schema.name == "BatchedCog_batch_group"
    execution_interval = get_field_by_name(generated_schema.schema, "execution_interval")
    assert execution_interval.type_info == clkbuiltins.DURATION

    # Verify VarSoa field exists
    signals_field = get_field_by_name(generated_schema.schema, "signals")
    assert signals_field.type_info is not None

    # Get the inner schema from the VarSoa field
    inner_schema = get_inner_schema_from_var_soa_field(generated_schema.schema, "signals")

    # Verify inner schema has soa_enabled
    assert inner_schema.options is not None
    assert inner_schema.options.soa_enabled is True

    # Verify basic signal field
    assert field_exists(inner_schema, "basic_signal_value")

    # Verify multi pre-aggregation fields
    assert field_exists(inner_schema, "multi_pre_agg_min")
    assert field_exists(inner_schema, "multi_pre_agg_max")
    assert field_exists(inner_schema, "multi_pre_agg_sum")

    # Mean pre-aggregation should be expanded to sum and count
    mean_sum_field = get_field_by_name(inner_schema, "mean_signal_sum")
    assert mean_sum_field.type_info == clkbuiltins.FLOAT32
    mean_count_field = get_field_by_name(inner_schema, "mean_signal_count")
    assert mean_count_field.type_info == clkbuiltins.FLOAT32

    # Mean with explicit sum should not duplicate the sum field (only 2 fields: sum and count)
    assert field_exists(inner_schema, "mean_and_sum_sum")
    assert field_exists(inner_schema, "mean_and_sum_count")
    mean_and_sum_fields = [f for f in inner_schema.fields.values() if f.cur_name.startswith("mean_and_sum_")]
    assert len(mean_and_sum_fields) == 2

    # Mean with explicit count should not duplicate the count field (only 2 fields: sum and count)
    assert field_exists(inner_schema, "mean_and_count_sum")
    assert field_exists(inner_schema, "mean_and_count_count")
    mean_and_count_fields = [f for f in inner_schema.fields.values() if f.cur_name.startswith("mean_and_count_")]
    assert len(mean_and_count_fields) == 2

    # Mean with both sum and count should not duplicate either (only 2 fields: sum and count)
    assert field_exists(inner_schema, "mean_sum_count_sum")
    assert field_exists(inner_schema, "mean_sum_count_count")
    mean_sum_count_fields = [f for f in inner_schema.fields.values() if f.cur_name.startswith("mean_sum_count_")]
    assert len(mean_sum_count_fields) == 2

    # Metadata should be preserved for min aggregation
    assert field_exists(inner_schema, "preserved_metadata_min")
    assert field_exists(inner_schema, "preserved_metadata_min_metadata")
    metadata_field = get_field_by_name(inner_schema, "preserved_metadata_min_metadata")
    assert metadata_field.type_info == clkbuiltins.SYNC_TIME

    # Metadata should be stripped for mean aggregation (expanded to sum/count which strip metadata)
    assert field_exists(inner_schema, "stripped_metadata_sum")
    assert field_exists(inner_schema, "stripped_metadata_count")
    stripped_metadata_fields = [f for f in inner_schema.fields.values() if f.cur_name.startswith("stripped_metadata_")]
    assert len(stripped_metadata_fields) == 2  # Only sum and count, no metadata fields
