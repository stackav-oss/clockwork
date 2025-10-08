# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Cog-related schema generation functions."""

from __future__ import annotations

import uuid
from decimal import Decimal
from typing import TYPE_CHECKING

from clockwork.dsl.ir import (
    clkbuiltins,
    clkenum,
    node,
    primitive,
    representation,
    schema,
    typesys,
)

if TYPE_CHECKING:
    from clockwork.dsl.ir.cog_components import ConditionDef, InputDef, OutputDef

DEFAULT_METRICS_BATCH_SIZE = 10


def get_underlying_enum_type(cog_name: str, conditions: dict[str, ConditionDef]) -> clkbuiltins.IntegerPrimitiveType:
    """Determine the smallest integer type that can hold all condition flags."""
    eight = 8
    sixteen = 16
    thirty_two = 32
    sixty_four = 64
    if len(conditions) <= eight:
        return clkbuiltins.UINT8
    if len(conditions) <= sixteen:
        return clkbuiltins.UINT16
    if len(conditions) <= thirty_two:
        return clkbuiltins.UINT32
    if len(conditions) <= sixty_four:
        return clkbuiltins.UINT64

    msg = f"Exceeded maximum number of condition definitions (64) for cog '{cog_name}'."
    raise ValueError(msg)


def generate_conditions_mask_enum(
    cog_name: str, conditions: dict[str, ConditionDef], module: node.Module
) -> clkenum.ClkEnum:
    """Generates a ClkEnum for the Cog's conditions to be used as a bitmask."""
    enum_name = f"{cog_name}ConditionsMask"
    enum_uuid_val = uuid.uuid3(clkbuiltins.CLOCKWORK_NAMESPACE_UUID, enum_name)

    # Ensure module and its parent scope exist
    if module.inner_scope.parent is None:
        msg = "Module or module's parent scope is not properly initialized."
        raise RuntimeError(msg)

    enum_scope: node.Scope = module.inner_scope.make_child_scope(cog_name)
    enum_inner_scope = enum_scope.make_child_scope(enum_name)

    value_defs_map: dict[int, clkenum.ValueDef] = {}
    underlying_enum_type = get_underlying_enum_type(cog_name, conditions)
    # Placeholder for the ClkEnum instance to allow ValueDef to reference it.
    # Actual fields will be filled in after ValueDefs are prepared.
    temp_enum_ref_for_valuedefs = clkenum.ClkEnum(
        type_info=clkbuiltins.TYPE_TYPE,
        doc=node.Doc(module, None, ""),
        module=module,
        cst_node=None,
        name=enum_name,
        scope=enum_scope,
        inner_scope=enum_inner_scope,
        uuid=enum_uuid_val,
        values={},
        default_field_num=0,
        bit_flags=True,
        underlying_type=underlying_enum_type,
        has_explicit_underlying_type=True,
        linter_overrides=set(),
        history=None,
        resolved=None,
    )

    # Default value for no conditions active
    default_value_def = clkenum.ValueDef(
        doc=node.Doc(module=module, cst_node=None, value="No conditions are active."),
        module=module,
        cst_node=None,
        name="no_conditions_active",
        scope=enum_inner_scope,
        enum=temp_enum_ref_for_valuedefs,
        field_num=0,
        is_default=True,
        integer_value=0,
        resolved=None,
    )
    default_value_def.resolved = clkenum.ResolvedValueDef(
        doc=default_value_def.doc,
        module=module,
        cst_node=None,
        name="no_conditions_active",
        scope=enum_inner_scope,
        enum=temp_enum_ref_for_valuedefs,
        field_num=0,
        is_default=True,
        integer_value=0,
        value_is_explicit=True,
        source=default_value_def,
    )

    value_defs_map[default_value_def.field_num] = default_value_def
    for idx, cond_def in enumerate(conditions.values()):
        val_def = clkenum.ValueDef(
            doc=node.Doc(module=module, cst_node=None, value=f"Condition {cond_def.name} is active."),
            module=module,
            cst_node=None,
            name=cond_def.name,
            scope=enum_inner_scope,
            enum=temp_enum_ref_for_valuedefs,
            field_num=idx + 1,
            is_default=False,
            integer_value=int(1 << idx),
            resolved=None,
        )
        val_def.resolved = clkenum.ResolvedValueDef(
            doc=val_def.doc,
            module=module,
            cst_node=None,
            name=val_def.name,
            scope=enum_inner_scope,
            enum=temp_enum_ref_for_valuedefs,
            field_num=val_def.field_num,
            is_default=val_def.is_default,
            integer_value=val_def.integer_value,  # pyright: ignore[reportArgumentType], we set this directly above
            value_is_explicit=True,
            source=val_def,
        )
        value_defs_map[val_def.field_num] = val_def

    # Now create the actual ClkEnum with all ValueDefs
    final_enum = clkenum.ClkEnum(
        type_info=clkbuiltins.TYPE_TYPE,
        doc=node.Doc(module=module, cst_node=None, value=f"Bitmask for active conditions in Cog {cog_name}."),
        module=module,
        cst_node=None,  # No direct CST node for generated enums
        name=enum_name,
        scope=enum_scope,
        inner_scope=enum_inner_scope,
        uuid=enum_uuid_val,
        values=value_defs_map,
        default_field_num=0,  # Field number of "NoConditionsActive"
        bit_flags=True,
        underlying_type=underlying_enum_type,
        has_explicit_underlying_type=True,
        linter_overrides=set(),
        history=None,
        resolved=None,
    )

    final_enum.resolved = clkenum.ResolvedEnum(
        doc=node.Doc(module=module, cst_node=None, value=f"Bitmask for active conditions in Cog {cog_name}."),
        type_info=clkbuiltins.TYPE_TYPE,
        module=module,
        cst_node=None,
        name=enum_name,
        scope=enum_scope,
        inner_scope=enum_inner_scope,
        uuid=enum_uuid_val,
        values={vd.field_num: vd.resolved for vd in value_defs_map.values()},  # pyright: ignore[reportArgumentType] we set this directly above
        default_field_num=0,  # Field number of "NoConditionsActive"
        bit_flags=True,
        underlying_type=underlying_enum_type,
        has_explicit_underlying_type=True,
        linter_overrides=set(),
        has_explicit_values=True,
        history=clkenum.ResolvedEnumHistory([], [], {}, None, None),
        source=final_enum,
    )
    # Update the enum reference in ValueDefs and define ValueRefs in the enum's inner scope
    for vd in value_defs_map.values():
        vd.enum = final_enum
        if vd.resolved:
            vd.resolved.enum = final_enum
        enum_inner_scope.define(vd.name, clkenum.ValueRef.make(vd), module.terminals if module else None)

    enum_scope.define(final_enum.name, final_enum, module.terminals if module else None)
    return final_enum


def generate_telemetry_metrics_schema(
    cog_name: str,
    inputs: dict[str, InputDef],
    outputs: dict[str, OutputDef],
    conditions: dict[str, ConditionDef],
    module: node.Module,
) -> schema.InstantiatedSchema:
    """Generate the telemetry metrics schema for a cog."""
    field_num_counter = 0
    fields: list[schema.FieldDef] = []

    input_telemtry_metrics = get_instantiated_schema_from_rep_alias(module, "input_telemetry_metrics_rep")
    min_max_uint16 = get_instantiated_schema_from_rep_alias(module, "min_max_16")

    for input_channel in inputs.values():
        fields.append(schema.make_field(module, field_num_counter, input_channel.name, input_telemtry_metrics))
        field_num_counter += 1

    for output_channel in outputs.values():
        fields.append(
            schema.make_field(module, field_num_counter, output_channel.name + "_num_messages", min_max_uint16)
        )
        field_num_counter += 1

    for condition in conditions.values():
        fields.append(
            schema.make_field(module, field_num_counter, f"{condition.name}_trigger_vals", clkbuiltins.UINT16)
        )
        field_num_counter += 1
    common_telemetry_metrics = get_instantiated_schema_from_rep_alias(module, "cog_telemetry_metrics_tach")
    fields.append(schema.make_field(module, field_num_counter, "common_telemetry_metrics", common_telemetry_metrics))
    return schema.make_schema_class(
        name=cog_name + "TelemetryMetrics",
        module=module,
        fields=fields,
        uuid=uuid.uuid3(clkbuiltins.CLOCKWORK_NAMESPACE_UUID, cog_name + "TelemetryMetrics"),
    )


def generate_event_metrics_schema(  # noqa: PLR0913 All inputs needed to create metrics schema
    cog_name: str,
    batch_size: int,
    inputs: dict[str, InputDef],
    outputs: dict[str, OutputDef],
    conditions: dict[str, ConditionDef],
    module: node.Module,
) -> tuple[schema.InstantiatedSchema, list[schema.InstantiatedSchema], list[clkenum.ClkEnum]]:
    """Generate the event metrics schema for a cog."""
    field_num_counter = 0
    fields: list[schema.FieldDef] = []

    input_event_metrics = get_instantiated_schema_from_rep_alias(module, "input_event_metrics_rep")
    for input_channel in inputs.values():
        fields.append(schema.make_field(module, field_num_counter, input_channel.name, input_event_metrics))
        field_num_counter += 1
    for output_channel in outputs.values():
        fields.append(
            schema.make_field(module, field_num_counter, output_channel.name + "_num_messages", clkbuiltins.UINT16)
        )
        field_num_counter += 1

    conditions_mask_enum = generate_conditions_mask_enum(cog_name, conditions, module)
    fields.append(
        schema.make_field(
            module,
            field_num_counter,
            "trigger_flags",
            typespec=conditions_mask_enum,
        )
    )
    field_num_counter += 1

    common_event_metrics = get_instantiated_schema_from_rep_alias(module, "cog_event_metrics_tach")
    fields.append(schema.make_field(module, field_num_counter, "common_event_metrics", common_event_metrics))
    event_metrics = schema.make_schema_class(
        name=cog_name + "EventMetrics",
        module=module,
        fields=fields,
        uuid=uuid.uuid3(clkbuiltins.CLOCKWORK_NAMESPACE_UUID, cog_name + "EventMetrics"),
    )

    event_metrics_batch_schema = schema.make_schema_class(
        name=cog_name + "EventMetricsBatch",
        module=module,
        fields=[
            schema.make_field(
                module,
                0,
                "event_metrics",
                typesys.Instantiation(
                    type_info=clkbuiltins.TYPE_TYPE,
                    instantiates=clkbuiltins.VAR_ARRAY,
                    arguments={
                        "type": event_metrics,
                        "max_size": primitive.DecimalValue(clkbuiltins.INT64, Decimal(value=batch_size)),
                    },
                ),
            ),
        ],
        uuid=uuid.uuid3(clkbuiltins.CLOCKWORK_NAMESPACE_UUID, cog_name + "EventMetricsBatch"),
    )
    return (
        event_metrics_batch_schema,
        [event_metrics],
        [conditions_mask_enum],
    )


def get_instantiated_schema_from_rep_alias(module: node.Module, rep_alias: str) -> schema.InstantiatedSchema:
    """Get the instantiated schema from a representation alias."""
    if module.inner_scope.parent is None:
        msg = "Module's inner scope must have a parent to define metrics outputs. Has the module been resolved?"
        raise ValueError(msg)
    rep = module.inner_scope.parent.lookup(rep_alias)
    if not isinstance(rep, representation.ReprInstantiation):
        msg = f"Expected {rep_alias} to be a representation got {rep}"
        raise TypeError(msg)
    return rep.get_resolved().schema_ir
