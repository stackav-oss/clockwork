# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Metrics schema generation for Clockwork cogs.

This module provides utilities for generating standardized metrics schemas
for Clockwork cogs. It generates schema definitions in the Clockwork schema
format, covering various metrics categories:

- Telemetry metrics: Aggregated statistics for cog performance monitoring
- Event metrics: Per-execution cycle detailed metrics for in-depth analysis

The generated schemas include metrics for:
- Input channels (message counts, staleness, drops)
- Output channels (message production)
- Execution performance (timing, latencies)
- Condition triggers

The module offers both individual schema generation for single cogs and
comprehensive file generation for collections of cogs:

- Individual generators create metrics schemas that encapsulate common metrics types
- File generators combine multiple schemas with proper use declarations
- Target generators create cpp_target definitions for all related schemas

These utilities enable consistent metrics collection, analysis, and code generation
across different cog implementations within the Clockwork framework, facilitating
performance monitoring and optimization.
"""

from collections.abc import Iterable
from dataclasses import dataclass

from clockwork.dsl.ir import cog


@dataclass
class MetricsSchema:
    """A data class representing a metrics schema for a cog.

    Attributes:
        name: The name of the schema
        schema: The schema definition as a string
    """

    name: str
    schema: str


def generate_condition_trigger_enum(cog_ir: cog.Cog) -> MetricsSchema | None:
    """Generate a Clockwork enum for condition trigger flags.

    Creates a bit_flags enum that defines a flag for each condition in the cog.

    Args:
        cog_ir: The Cog IR object to generate metrics for

    Returns:
        A MetricsSchema containing the enum name and definition
    """
    # Extract conditions from the cog IR
    conditions = list(cog_ir.conditions.keys())

    if not conditions:
        return None

    enum_lines = []

    # Start the enum definition
    enum_lines.append(f"// Condition trigger flags for {cog_ir.name}")
    enum_lines.append(f"enum {cog_ir.name}TriggerFlags")
    enum_lines.append("{")
    enum_lines.append("  options")
    enum_lines.append("  {")
    enum_lines.append("    bit_flags;")
    enum_lines.append("    underlying_type: UInt8;")
    enum_lines.append("  }")
    enum_lines.append("  values")
    enum_lines.append("  {")

    # Add a "none" flag with value 0
    enum_lines.append("    // No conditions triggered")
    enum_lines.append("    #0 none default { underlying_value: 0; }")

    # Add a flag for each condition, with bit values (1, 2, 4, 8, etc.)
    for i, condition_name in enumerate(conditions):
        bit_value = 1 << i  # 2^i (1, 2, 4, 8, 16, etc.)
        enum_lines.append(f"    // {condition_name} condition triggered")
        enum_lines.append(f"    #{i + 1} {condition_name} {{ underlying_value: {bit_value}; }}")

    # Close the enum definition
    enum_lines.append("  }")
    enum_lines.append("}")

    return MetricsSchema(name=f"{cog_ir.name}TriggerFlags", schema="\n".join(enum_lines))


def generate_telemetry_metrics_schema(cog_ir: cog.Cog) -> MetricsSchema:
    """Generate a Clockwork schema for cog telemetry metrics.

    Creates a schema that encapsulates the common CogTelemetryMetrics schema
    and adds additional per-input/output metrics.

    Args:
        cog_ir: The Cog IR object to generate metrics for

    Returns:
        A MetricsSchema containing the schema name and definition
    """
    # Extract inputs and outputs from the cog IR
    inputs = [input_def.name for input_def in cog_ir.inputs.values()]
    outputs = [output_def.name for output_def in cog_ir.outputs.values()]

    schema_lines = []

    # Start the main metrics schema - containing the common telemetry metrics
    schema_lines.append(f"// Extended metrics schema for {cog_ir.name}")
    schema_lines.append(f"schema {cog_ir.name}TelemetryMetrics {{")
    schema_lines.append("    fields {")

    # Include the common telemetry metrics as a field
    schema_lines.append("        // Common telemetry metrics")
    schema_lines.append("        #0 common: CogTelemetryMetrics;")

    field_num = 1  # Start after the common metrics field

    # Add input channel metrics using the common container type
    if inputs:
        schema_lines.append("        // ---- Input Channel Metrics ----")

        for input_name in inputs:
            schema_lines.append(f"        // Metrics for {input_name} input")
            schema_lines.append(f"        #{field_num} {input_name}: InputChannelTelemetryMetrics;")
            field_num += 1

    # Add output channel metrics
    if outputs:
        schema_lines.append("        // ---- Output Channel Metrics ----")

        for output_name in outputs:
            schema_lines.append(f"        // Messages produced on {output_name} output")
            schema_lines.append(f"        #{field_num} {output_name}_num_messages: MinMaxMean<UInt16>;")
            field_num += 1

    # Close the schema definition
    schema_lines.append("    }")
    schema_lines.append("}")

    return MetricsSchema(name=f"{cog_ir.name}TelemetryMetrics", schema="\n".join(schema_lines))


def generate_event_metrics_schema(cog_ir: cog.Cog) -> Iterable[MetricsSchema]:
    """Generate Clockwork schemas for event metrics recording.

    Creates schemas that encapsulate the common CogEventMetrics schema
    and add additional cog-specific metrics.

    Args:
        cog_ir: The Cog IR object to generate metrics for

    Returns:
        A list of MetricsSchema objects containing the schema definitions
    """
    # Extract inputs, outputs, and conditions from the cog IR
    inputs = list(cog_ir.inputs.keys())
    outputs = list(cog_ir.outputs.keys())
    conditions = list(cog_ir.conditions.keys())

    result_schemas = []

    # Create the event metrics schema
    schema_lines = []
    schema_lines.append(f"// Event metrics schema for {cog_ir.name}")
    schema_lines.append(f"schema {cog_ir.name}EventMetrics {{")
    schema_lines.append("    fields {")

    # Include the common event metrics as a field
    schema_lines.append("        // Common event metrics")
    schema_lines.append("        #0 common: CogEventMetrics;")

    field_num = 1  # Start after the common metrics field

    # Input channel metrics using the common container type
    if inputs:
        schema_lines.append("")
        schema_lines.append("        // ---- Input Channel Metrics ----")

        for input_name in inputs:
            schema_lines.append(f"        // Metrics for {input_name} input")
            schema_lines.append(f"        #{field_num} {input_name}: InputChannelEventMetrics;")
            field_num += 1

    # Output channel metrics
    if outputs:
        schema_lines.append("")
        schema_lines.append("        // ---- Output Channel Metrics ----")

        for output_name in outputs:
            schema_lines.append(f"        // Messages produced on {output_name} output")
            schema_lines.append(f"        #{field_num} {output_name}_num_messages: UInt16;")
            field_num += 1

    # Condition trigger flags
    if conditions:
        schema_lines.append("")
        schema_lines.append("        // ---- Condition Trigger Flags ----")

        # Calculate how many bytes we need to store all condition flags (8 conditions per byte)
        condition_bytes = _calculate_bytes_needed_for_bits(len(conditions))
        for byte_idx in range(condition_bytes):
            schema_lines.append("        // Condition trigger flags")
            schema_lines.append(f"        #{field_num} trigger_flags_{byte_idx}: UInt8;")
            field_num += 1

    # Close the schema definition
    schema_lines.append("    }")
    schema_lines.append("}")

    # Add the main event metrics schema
    result_schemas.append(MetricsSchema(name=f"{cog_ir.name}EventMetrics", schema="\n".join(schema_lines)))

    # Create the batched event metrics container schema
    schema_lines = []
    schema_lines.append(f"// Batched event metrics for {cog_ir.name}")
    schema_lines.append(f"schema Batched{cog_ir.name}EventMetrics {{")
    schema_lines.append("    fields {")
    schema_lines.append("        // Array of event metrics")
    schema_lines.append(f"        #0 events: VarArray<{cog_ir.name}EventMetrics, 10>;")
    schema_lines.append("    }")
    schema_lines.append("}")

    # Add the batched event metrics schema
    result_schemas.append(MetricsSchema(name=f"Batched{cog_ir.name}EventMetrics", schema="\n".join(schema_lines)))

    return result_schemas


def generate_cog_metrics_cpp_target(
    schema_names: Iterable[str], enum_names: Iterable[str], namespace: str, target_name: str
) -> str:
    """Generate a Clockwork cpp_target for metrics schemas.

    Creates a cpp_target section in CLK format that includes all the provided schema names,
    along with their Tachyon representations and Tappy interfaces.

    Args:
        schema_names: List of schema names to include in the target
        enum_names: List of enum names to include in the target
        namespace: Namespace to use for the target
        target_name: Name of the target

    Returns:
        A string containing the cpp_target section
    """
    lines = []

    # Start the target definition
    lines.append(f"cpp_target {target_name}")
    lines.append("{")
    lines.append("  options")
    lines.append("  {")
    lines.append(f"    namespace {namespace};")
    lines.append("  }")
    lines.append("")

    # Add all enums to the target
    if enum_names:
        lines.extend([f"  enum {enum_name};" for enum_name in enum_names])
        lines.append("")

    # Add all schemas to the target
    if schema_names:
        lines.extend([f"  schema {schema_name};" for schema_name in schema_names])
        lines.append("")

    # Add Tachyon representations for all schemas
    lines.extend([f"  representation Tachyon<{type_name}>;" for type_name in schema_names])
    lines.append("")

    # Add Tappy interfaces for all schemas
    lines.extend([f"  interface Tappy<{type_name}>;" for type_name in schema_names])

    lines.append("}")

    return "\n".join(lines)


def generate_metrics_file(cogs: Iterable[cog.Cog], namespace: str, target_name: str) -> str:
    """Generate a complete CLK file with metrics schemas for multiple cogs.

    Creates a single CLK file containing metrics schemas for all provided cogs
    and a single cpp_target that includes all the schemas.

    Args:
        cogs: List of Cog IR objects to generate metrics for
        namespace: Namespace to use for the cpp_target
        target_name: Name of the cpp_target

    Returns:
        A string containing the complete CLK file
    """
    # Set of imports needed for all metrics schemas
    use_declarations = {
        "clockwork::dsl::cog::common_cog_telemetry_metrics::MinMaxMean",
        "clockwork::dsl::cog::common_cog_telemetry_metrics::CogTelemetryMetrics",
        "clockwork::dsl::cog::common_cog_telemetry_metrics::InputChannelTelemetryMetrics",
        "clockwork::dsl::cog::common_cog_event_metrics::CogEventMetrics",
        "clockwork::dsl::cog::common_cog_event_metrics::InputChannelEventMetrics",
    }
    lines = []

    # Generate schemas for each cog
    all_schemas = []
    schema_names = []
    enum_names = []

    for cog_ir in cogs:
        # Generate telemetry metrics schema
        telemetry_schema = generate_telemetry_metrics_schema(cog_ir)
        all_schemas.append(telemetry_schema)
        schema_names.append(telemetry_schema.name)

        # Generate event metrics schemas
        event_schemas = generate_event_metrics_schema(cog_ir)
        all_schemas.extend(event_schemas)
        schema_names.extend([schema.name for schema in event_schemas])

        # Generate condition trigger enum
        trigger_enum = generate_condition_trigger_enum(cog_ir)
        if trigger_enum:
            all_schemas.append(trigger_enum)
            enum_names.append(trigger_enum.name)

    if not all_schemas:
        return ""

    # Add use declarations
    lines.extend([f"use {use_path};" for use_path in sorted(use_declarations)])
    lines.append("")

    # Add all schema and enum definitions
    for schema in all_schemas:
        lines.extend([schema.schema, ""])

    # Add a C++ target for all the metrics - separate schemas and enums
    cpp_target = generate_cog_metrics_cpp_target(schema_names, enum_names, namespace, target_name)
    lines.append(cpp_target)

    # Ensure the file ends with a newline
    lines.append("")

    return "\n".join(lines)


def _calculate_bytes_needed_for_bits(num_bits: int) -> int:
    """Calculate how many bytes are needed to store a given number of bits.

    Uses ceiling division to ensure we always have enough bytes to store all bits.

    Args:
        num_bits: The number of bits that need to be stored

    Returns:
        The minimum number of bytes needed to store the bits
    """
    # Adding 7 before integer division ensures we round up rather than down
    return (num_bits + 7) // 8
