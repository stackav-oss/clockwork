# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Facilities for generating C++ Signal API structs for Dial code generation.

This module contains all signal and report group related code generation logic,
extracted from cppdial.py to keep the Dial generation concerns separated.
"""

from copy import deepcopy
from dataclasses import dataclass

from clockwork.dsl.cog.pycog import (
    SignalEntry,
    SignalsStruct,
)
from clockwork.dsl.cpp.context import CppChunk, Header, SystemHeader
from clockwork.dsl.cpp.types import (
    BINARY_OUTCOME,
    BOOLEAN,
    VOID,
    CppMethod,
    CppNamedType,
    CppNamedValue,
    CppStruct,
    CppTemplate,
    CppTemplateType,
    CppType,
    CppTypeExpr,
    CppValue,
    CppValueExpr,
    Ref,
)
from clockwork.dsl.ir import primitive, units
from clockwork.dsl.ir import signal as signal_ir
from clockwork.dsl.ir.module_id import JEWELS_REPO

# Helper template for Out<T> output parameter wrapper
_OUT_TEMPLATE = CppTemplate(
    includes=[Header(JEWELS_REPO, "jewels/callsig/outparam.hh")],
    template_name="Out",
    cpp_namespace="jewels",
)

# Helper template for OptionalOut<T> optional output parameter wrapper
_OPTIONAL_OUT_TEMPLATE = CppTemplate(
    includes=[Header(JEWELS_REPO, "jewels/callsig/outparam.hh")],
    template_name="OptionalOut",
    cpp_namespace="jewels",
)

# Mapping from AggregationType to the C++ aggregator class name
_AGGREGATOR_CLASS_MAP: dict[signal_ir.AggregationType, str] = {
    signal_ir.AggregationType.VALUE: "ValueAggregator",
    signal_ir.AggregationType.FINAL_VALUE: "FinalValueAggregator",
    signal_ir.AggregationType.FIRST_VALUE: "FirstValueAggregator",
    signal_ir.AggregationType.MIN: "MinAggregator",
    signal_ir.AggregationType.MAX: "MaxAggregator",
    signal_ir.AggregationType.SUM: "SumAggregator",
    signal_ir.AggregationType.COUNT: "CountAggregator",
    signal_ir.AggregationType.MEAN: "MeanAggregator",
}


def _sorted_agg_types(agg_types: set[signal_ir.AggregationType]) -> list[signal_ir.AggregationType]:
    """Sort a set of aggregation types alphabetically by value for deterministic ordering."""
    return sorted(agg_types, key=lambda a: a.value)


@dataclass
class BatchedReportGroupInfo:
    """Information about a batched report group for signal API code generation."""

    name: str
    batch_size: int
    signal_keys: list[str]  # Keys into SignalsStruct.signals for signals in this group
    min_observations: int | None  # Minimum observations before publishing is allowed
    min_duration_ns: int | None  # Minimum duration in nanoseconds before publishing is allowed
    max_duration_ns: int | None  # Maximum duration in nanoseconds (triggers publish regardless of observations)


@dataclass
class PostAggregatedReportGroupInfo:
    """Information about a post-aggregated report group for signal API code generation."""

    name: str
    signal_keys: list[str]  # Keys into SignalsStruct.signals for signals in this group
    min_observations: int | None  # Minimum observations before publishing is allowed
    max_observations: int | None  # Maximum observations (triggers publish when reached if min_duration met)
    min_duration_ns: int | None  # Minimum duration in nanoseconds before publishing is allowed
    max_duration_ns: int | None  # Maximum duration in nanoseconds (triggers publish regardless of observations)


def _duration_to_nanoseconds(duration: primitive.UnitValue | None) -> int | None:
    """Convert a duration UnitValue to nanoseconds as an integer.

    Args:
        duration: The duration value, or None.

    Returns:
        The duration in nanoseconds as an integer, or None if duration is None.
    """
    if duration is None:
        return None
    return int(duration.as_unit(units.NANOSECONDS).value)


def collect_batched_report_groups(signals_struct: SignalsStruct) -> dict[str, BatchedReportGroupInfo]:
    """Collect information about all batched report groups.

    Groups signals by their report group and determines the batch size for each.

    Args:
        signals_struct: The SignalsStruct containing all signal entries.

    Returns:
        A dictionary mapping report group name to BatchedReportGroupInfo.
    """
    report_groups: dict[str, BatchedReportGroupInfo] = {}

    for signal_key, signal in signals_struct.signals.items():
        if not signal.is_batched:
            continue

        rg_name = signal.report_group_name
        if rg_name not in report_groups:
            batch_size = signal.max_observations if signal.max_observations is not None else 1

            rg_config = signals_struct.report_groups[rg_name].report_group_config
            min_observations = rg_config.min_observations if rg_config else None
            min_duration_ns = _duration_to_nanoseconds(rg_config.min_duration) if rg_config else None
            max_duration_ns = _duration_to_nanoseconds(rg_config.max_duration) if rg_config else None

            report_groups[rg_name] = BatchedReportGroupInfo(
                name=rg_name,
                batch_size=batch_size,
                signal_keys=[signal_key],
                min_observations=min_observations,
                min_duration_ns=min_duration_ns,
                max_duration_ns=max_duration_ns,
            )
        else:
            report_groups[rg_name].signal_keys.append(signal_key)

    return report_groups


def collect_post_aggregated_report_groups(signals_struct: SignalsStruct) -> dict[str, PostAggregatedReportGroupInfo]:
    """Collect information about all post-aggregated report groups.

    Groups signals by their report group for non-batched signals.

    Args:
        signals_struct: The SignalsStruct containing all signal entries.

    Returns:
        A dictionary mapping report group name to PostAggregatedReportGroupInfo.
    """
    report_groups: dict[str, PostAggregatedReportGroupInfo] = {}

    for signal_key, signal in signals_struct.signals.items():
        # Only process non-batched signals (post-aggregated)
        if signal.is_batched:
            continue

        rg_name = signal.report_group_name
        if rg_name not in report_groups:
            rg_config = signals_struct.report_groups[rg_name].report_group_config
            min_observations = rg_config.min_observations if rg_config else None
            max_observations = rg_config.max_observations if rg_config else None
            min_duration_ns = _duration_to_nanoseconds(rg_config.min_duration) if rg_config else None
            max_duration_ns = _duration_to_nanoseconds(rg_config.max_duration) if rg_config else None

            report_groups[rg_name] = PostAggregatedReportGroupInfo(
                name=rg_name,
                signal_keys=[signal_key],
                min_observations=min_observations,
                max_observations=max_observations,
                min_duration_ns=min_duration_ns,
                max_duration_ns=max_duration_ns,
            )
        else:
            report_groups[rg_name].signal_keys.append(signal_key)

    return report_groups


def make_signal_api_struct(
    name: str,
    signals_struct: SignalsStruct,
    policy_class_name: str | None = None,
    policy_template_params: str | None = None,
) -> CppStruct:
    """Make the CppStruct type to represent the SignalApi for the dial.

    Generates set_/accumulate_ methods for setting signal values and
    get_ methods for retrieving current values.

    For batched report groups:
    - Each report group has its own batch_size constant, batch index, and management methods
    - Aggregators are stored in arrays sized to the report group's max_observations
    - reset_batch_<group>() resets aggregators and batch index for a specific group
    - end_of_execution_<group>() advances to the next batch slot for a specific group
    - get_batch_count_<group>() returns completed executions for a specific group

    For post-aggregated report groups:
    - Pre-aggregated values are accumulated into post aggregators after each execution
    - Each report group has an execution count tracking how many executions have occurred
    - reset_<group>() resets all aggregators and execution count for a specific group
    - end_of_execution_<group>() accumulates pre-aggregated values into post aggregators
    - get_execution_count_<group>() returns the number of executions since last reset

    Args:
        name: The name of the SignalApi struct.
        signals_struct: The SignalsStruct containing all signal entries.
        policy_class_name: The name of the Policy class to declare as a friend, or None.
        policy_template_params: Policy template parameters or None.

    Returns:
        A CppStruct representing the SignalApi.
    """
    struct_name = CppType(includes=[], type_name=name, cpp_namespace=None)
    struct = CppStruct(
        name=struct_name,
        doc="Signal API for setting and retrieving signal values.",
        no_lints=["clang-analyzer-optin.performance.Padding"],
    )

    # Add friend declaration for the Policy so it can access private infra methods
    if policy_class_name:
        friend_chunk = CppChunk()
        if policy_template_params:
            friend_chunk.append(f"template <{policy_template_params}>")
            friend_chunk.append(f"friend struct {policy_class_name}Base;")
        else:
            friend_chunk.append(f"friend struct {policy_class_name};")
        struct.leading_header_chunk = friend_chunk

    batched_groups = collect_batched_report_groups(signals_struct)

    post_agg_groups = collect_post_aggregated_report_groups(signals_struct)

    for rg_info in batched_groups.values():
        _add_batch_size_constant_for_group(struct, rg_info)
        _add_timing_constants_for_batched_group(struct, rg_info)
        _add_batch_index_field_for_group(struct, rg_info)
        _add_window_start_time_field_for_group(struct, rg_info.name)
        _add_last_execution_end_time_field_for_group(struct, rg_info.name)

    for rg_info in post_agg_groups.values():
        _add_timing_constants_for_post_agg_group(struct, rg_info)
        _add_execution_count_field_for_group(struct, rg_info)
        _add_window_start_time_field_for_group(struct, rg_info.name)
        _add_last_execution_end_time_field_for_group(struct, rg_info.name)

    _add_signal_methods(struct, signals_struct, post_agg_groups)

    for rg_info in batched_groups.values():
        _add_batch_management_methods_for_group(struct, signals_struct, rg_info)
        struct.private.append(_make_populate_method_for_batched_group(signals_struct, rg_info))

    for rg_info in post_agg_groups.values():
        _add_post_agg_management_methods_for_group(struct, signals_struct, rg_info)
        struct.private.append(_make_populate_method_for_post_agg_group(signals_struct, rg_info))

    _add_pre_aggregator_fields(struct, signals_struct, batched_groups)

    _add_post_agg_pre_aggregator_fields(struct, signals_struct, post_agg_groups)
    _add_post_aggregator_fields(struct, signals_struct, post_agg_groups)

    return struct


def _add_batch_size_constant_for_group(struct: CppStruct, rg_info: BatchedReportGroupInfo) -> None:
    """Add a static constexpr batch_size_<group> member to the struct.

    Args:
        struct: The CppStruct to add the constant to.
        rg_info: The batched report group info containing name and batch size.
    """
    size_t_type = CppType(includes=[], type_name="size_t", cpp_namespace=None, const=True)
    struct.static_data_members.append(
        CppNamedValue(
            named_type=CppNamedType(
                argument_type=size_t_type,
                argument_name=f"batch_size_{rg_info.name}",
            ),
            value=CppValue(None, str(rg_info.batch_size)),
            doc=f"Maximum batch size for report group '{rg_info.name}'.",
            qualifiers=["static", "constexpr"],
            render_initializer=True,
        )
    )


def _add_batch_index_field_for_group(struct: CppStruct, rg_info: BatchedReportGroupInfo) -> None:
    """Add the current_batch_index_<group>_ private field to the struct.

    Args:
        struct: The CppStruct to add the field to.
        rg_info: The batched report group info containing the group name.
    """
    size_t_type = CppType(includes=[], type_name="size_t", cpp_namespace=None)
    struct.private.append(
        CppNamedValue(
            named_type=CppNamedType(
                argument_type=size_t_type,
                argument_name=f"current_batch_index_{rg_info.name}_",
            ),
            value=CppValue(None, "0"),
            doc=f"Current batch index for report group '{rg_info.name}'.",
            render_initializer=True,
        )
    )


def _add_execution_count_field_for_group(struct: CppStruct, rg_info: PostAggregatedReportGroupInfo) -> None:
    """Add the execution_count_<group>_ private field to the struct.

    Args:
        struct: The CppStruct to add the field to.
        rg_info: The post-aggregated report group info containing the group name.
    """
    size_t_type = CppType(includes=[], type_name="size_t", cpp_namespace=None)
    struct.private.append(
        CppNamedValue(
            named_type=CppNamedType(
                argument_type=size_t_type,
                argument_name=f"execution_count_{rg_info.name}_",
            ),
            value=CppValue(None, "0"),
            doc=f"Execution count for report group '{rg_info.name}'.",
            render_initializer=True,
        )
    )


def _add_window_start_time_field_for_group(struct: CppStruct, rg_name: str) -> None:
    """Add the window_start_time_<group>_ private field to the struct.

    Args:
        struct: The CppStruct to add the field to.
        rg_name: The report group name.
    """
    sync_time_type = CppType(
        includes=[Header(JEWELS_REPO, "jewels/time/sync_time.hh")],
        type_name="SyncTime",
        cpp_namespace="jewels::time",
    )
    optional_sync_time_type = CppTemplateType(
        include=[SystemHeader("optional")],
        template_name="optional",
        cpp_namespace="std",
        arguments=[sync_time_type],
    )
    struct.private.append(
        CppNamedValue(
            named_type=CppNamedType(
                argument_type=optional_sync_time_type,
                argument_name=f"window_start_time_{rg_name}_",
            ),
            value=CppValue(None, "std::nullopt"),
            doc=f"Start time of the current reporting window for report group '{rg_name}'.",
            render_initializer=True,
        )
    )


def _add_last_execution_end_time_field_for_group(struct: CppStruct, rg_name: str) -> None:
    """Add the last_execution_end_time_<group>_ private field to the struct.

    Args:
        struct: The CppStruct to add the field to.
        rg_name: The report group name.
    """
    sync_time_type = CppType(
        includes=[Header(JEWELS_REPO, "jewels/time/sync_time.hh")],
        type_name="SyncTime",
        cpp_namespace="jewels::time",
    )
    optional_sync_time_type = CppTemplateType(
        include=[SystemHeader("optional")],
        template_name="optional",
        cpp_namespace="std",
        arguments=[sync_time_type],
    )
    struct.private.append(
        CppNamedValue(
            named_type=CppNamedType(
                argument_type=optional_sync_time_type,
                argument_name=f"last_execution_end_time_{rg_name}_",
            ),
            value=CppValue(None, "std::nullopt"),
            doc=f"End time of the last execution for report group '{rg_name}'.",
            render_initializer=True,
        )
    )


def _add_timing_constants_for_batched_group(struct: CppStruct, rg_info: BatchedReportGroupInfo) -> None:
    """Add static constexpr timing constants for a batched report group.

    Args:
        struct: The CppStruct to add the constants to.
        rg_info: The batched report group info.
    """
    _add_timing_constants(
        struct, rg_info.name, rg_info.min_observations, rg_info.min_duration_ns, rg_info.max_duration_ns
    )


def _add_timing_constants_for_post_agg_group(struct: CppStruct, rg_info: PostAggregatedReportGroupInfo) -> None:
    """Add static constexpr timing constants for a post-aggregated report group.

    Args:
        struct: The CppStruct to add the constants to.
        rg_info: The post-aggregated report group info.
    """
    _add_timing_constants(
        struct, rg_info.name, rg_info.min_observations, rg_info.min_duration_ns, rg_info.max_duration_ns
    )


def _add_timing_constants(
    struct: CppStruct,
    rg_name: str,
    min_observations: int | None,
    min_duration_ns: int | None,
    max_duration_ns: int | None,
) -> None:
    """Add static constexpr timing constants for a report group.

    Args:
        struct: The CppStruct to add the constants to.
        rg_name: The report group name.
        min_observations: Minimum observations before publishing is allowed.
        min_duration_ns: Minimum duration in nanoseconds.
        max_duration_ns: Maximum duration in nanoseconds.
    """
    # Add min_observations constant if specified
    if min_observations is not None:
        size_t_type = CppType(includes=[], type_name="size_t", cpp_namespace=None, const=True)
        struct.static_data_members.append(
            CppNamedValue(
                named_type=CppNamedType(
                    argument_type=size_t_type,
                    argument_name=f"min_observations_{rg_name}",
                ),
                value=CppValue(None, str(min_observations)),
                doc=f"Minimum observations before publishing for report group '{rg_name}'.",
                qualifiers=["static", "constexpr"],
                render_initializer=True,
            )
        )

    # Add min_duration constant if specified
    if min_duration_ns is not None:
        duration_type = CppType(
            includes=[SystemHeader("chrono")],
            type_name="nanoseconds",
            cpp_namespace="std::chrono",
        )
        struct.static_data_members.append(
            CppNamedValue(
                named_type=CppNamedType(
                    argument_type=duration_type,
                    argument_name=f"min_duration_{rg_name}",
                ),
                value=CppValue(None, f"std::chrono::nanoseconds{{{min_duration_ns}}}"),
                doc=f"Minimum duration before publishing for report group '{rg_name}'.",
                qualifiers=["static", "constexpr", "inline"],
                render_initializer=True,
            )
        )

    # Add max_duration constant if specified
    if max_duration_ns is not None:
        duration_type = CppType(
            includes=[SystemHeader("chrono")],
            type_name="nanoseconds",
            cpp_namespace="std::chrono",
        )
        struct.static_data_members.append(
            CppNamedValue(
                named_type=CppNamedType(
                    argument_type=duration_type,
                    argument_name=f"max_duration_{rg_name}",
                ),
                value=CppValue(None, f"std::chrono::nanoseconds{{{max_duration_ns}}}"),
                doc=f"Maximum duration before publishing for report group '{rg_name}'.",
                qualifiers=["static", "constexpr", "inline"],
                render_initializer=True,
            )
        )


def _add_batch_management_methods_for_group(
    struct: CppStruct,
    signals_struct: SignalsStruct,
    rg_info: BatchedReportGroupInfo,
) -> None:
    """Add batch management methods for a specific report group.

    Adds:
    - reset_batch_<group>(): Resets aggregators, batch index, and window start time for the group.
    - start_of_execution_<group>(): Records the window start time if not already set.
    - end_of_execution_<group>(): Advances to next batch slot for the group.
    - get_batch_count_<group>(): Returns completed executions for the group.
    - get_batch_size_<group>(): Returns the batch size for the group.
    - should_publish_<group>(): Checks if the batch should be published.

    Args:
        struct: The CppStruct to add methods to.
        signals_struct: The SignalsStruct containing all signal entries.
        rg_info: The batched report group info.
    """
    struct.private.append(_make_reset_batch_method_for_group(signals_struct, rg_info))
    struct.private.append(_make_start_of_execution_method_for_group(rg_info.name))
    struct.private.append(_make_end_of_execution_method_for_group(rg_info))
    struct.public.append(_make_get_batch_count_method_for_group(rg_info))
    struct.public.append(_make_get_batch_size_method_for_group(rg_info))
    struct.private.append(_make_should_publish_method_for_batched_group(rg_info))


def _make_reset_batch_method_for_group(
    signals_struct: SignalsStruct,
    rg_info: BatchedReportGroupInfo,
) -> CppMethod:
    """Generate reset_batch_<group>() method to reset aggregators, batch index, and window for a group.

    Args:
        signals_struct: The SignalsStruct containing all signal entries.
        rg_info: The batched report group info.

    Returns:
        A CppMethod for reset_batch_<group>().
    """
    body = CppChunk()

    body.append(f"current_batch_index_{rg_info.name}_ = 0;")

    body.append(f"window_start_time_{rg_info.name}_ = std::nullopt;")
    body.append(f"last_execution_end_time_{rg_info.name}_ = std::nullopt;")

    for signal_key in rg_info.signal_keys:
        signal = signals_struct.signals[signal_key]

        if signal.has_pre_aggregation():
            pre_aggs = _expand_mean_for_batched(signal.pre_aggregation)
        else:
            pre_aggs = [signal_ir.AggregationType.VALUE]

        for pre_agg in pre_aggs:
            agg_field = _get_aggregator_field_name(signal.identifier, pre_agg)
            body.append(f"for (auto& agg : {agg_field}) {{ agg.reset(); }}")

    return CppMethod(
        name=f"reset_batch_{rg_info.name}",
        doc=f"Reset aggregators, batch index, window start time, and last execution end time for report group '{rg_info.name}'.",
        return_type=VOID,
        arguments=[],
        leading_qualifiers=[],
        trailing_qualifiers=[],
        body=body,
        no_discard=False,
    )


def _make_end_of_execution_method_for_group(rg_info: BatchedReportGroupInfo) -> CppMethod:
    """Generate end_of_execution_<group>() method to record execution end time and advance to the next batch slot.

    Args:
        rg_info: The batched report group info.

    Returns:
        A CppMethod for end_of_execution_<group>().
    """
    sync_time_type = CppType(
        includes=[Header(JEWELS_REPO, "jewels/time/sync_time.hh")],
        type_name="SyncTime",
        cpp_namespace="jewels::time",
    )

    body = CppChunk()
    body.append(f"last_execution_end_time_{rg_info.name}_ = current_time;")
    body.append(f"if (current_batch_index_{rg_info.name}_ < batch_size_{rg_info.name}) {{")
    body.append(f"    ++current_batch_index_{rg_info.name}_;")
    body.append("}")

    return CppMethod(
        name=f"end_of_execution_{rg_info.name}",
        doc=f"Record the execution end time and advance to the next batch slot for report group '{rg_info.name}'.",
        return_type=VOID,
        arguments=[CppNamedType(argument_type=sync_time_type, argument_name="current_time")],
        leading_qualifiers=[],
        trailing_qualifiers=[],
        body=body,
        no_discard=False,
    )


def _make_get_batch_count_method_for_group(rg_info: BatchedReportGroupInfo) -> CppMethod:
    """Generate get_batch_count_<group>() method to return completed executions.

    Args:
        rg_info: The batched report group info.

    Returns:
        A CppMethod for get_batch_count_<group>().
    """
    body = CppChunk()
    body.append(f"return current_batch_index_{rg_info.name}_;")

    size_t_type = CppType(includes=[], type_name="size_t", cpp_namespace=None)
    return CppMethod(
        name=f"get_batch_count_{rg_info.name}",
        doc=f"Get the number of executions in the current batch for report group '{rg_info.name}'.",
        return_type=size_t_type,
        arguments=[],
        leading_qualifiers=[],
        trailing_qualifiers=["const"],
        body=body,
        no_discard=True,
    )


def _make_get_batch_size_method_for_group(rg_info: BatchedReportGroupInfo) -> CppMethod:
    """Generate get_batch_size_<group>() method to return the batch size.

    Args:
        rg_info: The batched report group info.

    Returns:
        A CppMethod for get_batch_size_<group>().
    """
    body = CppChunk()
    body.append(f"return batch_size_{rg_info.name};")

    size_t_type = CppType(includes=[], type_name="size_t", cpp_namespace=None)
    return CppMethod(
        name=f"get_batch_size_{rg_info.name}",
        doc=f"Get the batch size for report group '{rg_info.name}'.",
        return_type=size_t_type,
        arguments=[],
        leading_qualifiers=[],
        trailing_qualifiers=["const"],
        body=body,
        no_discard=True,
    )


def _make_start_of_execution_method_for_group(rg_name: str) -> CppMethod:
    """Generate start_of_execution_<group>() method to record the window start time.

    Records the current time as the window start time if not already set.

    Args:
        rg_name: The report group name.

    Returns:
        A CppMethod for start_of_execution_<group>().
    """
    sync_time_type = CppType(
        includes=[Header(JEWELS_REPO, "jewels/time/sync_time.hh")],
        type_name="SyncTime",
        cpp_namespace="jewels::time",
    )

    body = CppChunk()
    body.append(f"if (!window_start_time_{rg_name}_) {{")
    body.append(f"    window_start_time_{rg_name}_ = current_time;")
    body.append("}")

    return CppMethod(
        name=f"start_of_execution_{rg_name}",
        doc=f"Record the window start time for report group '{rg_name}' if not already set.",
        return_type=VOID,
        arguments=[CppNamedType(argument_type=sync_time_type, argument_name="current_time")],
        leading_qualifiers=[],
        trailing_qualifiers=[],
        body=body,
        no_discard=False,
    )


def _make_should_publish_method_for_batched_group(rg_info: BatchedReportGroupInfo) -> CppMethod:
    """Generate should_publish_<group>() method to check if the batch should be published.

    Uses the time from the last end_of_execution call to determine elapsed time.
    Returns true if:
    - max_duration is reached, OR
    - max_observations (batch_size) is reached AND min_duration is met (if specified)

    Args:
        rg_info: The batched report group info.

    Returns:
        A CppMethod for should_publish_<group>().
    """
    body = CppChunk()

    # Window must be started and at least one execution must have completed
    body.append(f"if (!window_start_time_{rg_info.name}_ || !last_execution_end_time_{rg_info.name}_) {{")
    body.append("    return false;")
    body.append("}")
    body.append("")

    body.append(f"const auto observations = current_batch_index_{rg_info.name}_;")
    has_duration_check = rg_info.max_duration_ns is not None or rg_info.min_duration_ns is not None
    if has_duration_check:
        body.append(
            f"const auto elapsed = *last_execution_end_time_{rg_info.name}_ - *window_start_time_{rg_info.name}_;"
        )
    body.append("")

    # Check max_duration first - publish regardless of observations
    if rg_info.max_duration_ns is not None:
        body.append(f"if (elapsed >= max_duration_{rg_info.name}) {{")
        body.append("    return true;")
        body.append("}")
        body.append("")

    # Check if batch is full (max_observations/batch_size reached)
    body.append(f"if (observations >= batch_size_{rg_info.name}) {{")

    # If min_duration is specified, must also be met
    if rg_info.min_duration_ns is not None:
        body.append(f"    return elapsed >= min_duration_{rg_info.name};")
    else:
        body.append("    return true;")

    body.append("}")
    body.append("")
    body.append("return false;")

    return CppMethod(
        name=f"should_publish_{rg_info.name}",
        doc=f"Check if report group '{rg_info.name}' should be published based on observations and duration.",
        return_type=BOOLEAN,
        arguments=[],
        leading_qualifiers=[],
        trailing_qualifiers=["const"],
        body=body,
        no_discard=True,
    )


def _make_populate_method_for_batched_group(
    signals_struct: SignalsStruct,
    rg_info: BatchedReportGroupInfo,
) -> CppMethod:
    """Generate populate_<group>() method to populate a message with batched signal data.

    Populates the message's execution_interval and signals VarSoa from the internal aggregators.

    Args:
        signals_struct: The SignalsStruct containing all signal entries.
        rg_info: The batched report group info.

    Returns:
        A CppMethod for populate_<group>().
    """
    body = CppChunk()

    # Calculate execution interval
    body.append(f"if (window_start_time_{rg_info.name}_ && last_execution_end_time_{rg_info.name}_) {{")
    body.append(
        f"    msg.set_execution_interval(*last_execution_end_time_{rg_info.name}_ - *window_start_time_{rg_info.name}_);"
    )
    body.append("} else {")
    body.append("    msg.set_execution_interval(std::chrono::nanoseconds{0});")
    body.append("}")
    body.append("")

    # Populate the signals VarSoa
    body.append(f"const auto batch_count = current_batch_index_{rg_info.name}_;")
    body.append("auto& signals = msg.get_mutable_signals();")
    body.append("signals.clear();")
    body.append("for (std::size_t i = 0; i < batch_count; ++i) {")
    body.append("    auto elem = signals.emplace_back();")

    # For each signal in this group, populate its fields
    for signal_key in rg_info.signal_keys:
        signal = signals_struct.signals[signal_key]

        # Get effective pre-aggregation types (expand MEAN for batched)
        if signal.has_pre_aggregation():
            pre_aggs = _expand_mean_for_batched(signal.pre_aggregation)
        else:
            pre_aggs = [signal_ir.AggregationType.VALUE]

        if signal.presence_bit_index is not None:
            representative_agg = pre_aggs[0]
            representative_field = _get_aggregator_field_name(signal.identifier, representative_agg)
            body.context.add_include(Header(JEWELS_REPO, "jewels/callsig/outcome.hh"))
            body.context.add_include(SystemHeader("exception"))
            body.append(f"    if ({representative_field}.at(i).has_value()) {{")
            body.append(
                f"        if (jewels::fails(elem.get_mutable_signal_presence().try_set({signal.presence_bit_index}))) {{"
            )
            # It's an invariant that presence_bit_index is within the number of
            # bits in the signal_presence bitmask, so this std::terminate is
            # unreachable.
            body.append("            std::terminate();")
            body.append("        }")
            body.append("    }")

        for pre_agg in pre_aggs:
            agg_field = _get_aggregator_field_name(signal.identifier, pre_agg)
            field_name = f"{signal.identifier}_{pre_agg.value}"

            body.append(f"    if ({agg_field}.at(i).has_value()) {{")
            body.append(f"        elem.set_{field_name}({agg_field}.at(i).get_value());")

            # Handle metadata if preserved
            if signal.metadata_type is not None and _preserves_metadata(pre_agg):
                metadata_field_name = f"{signal.identifier}_{pre_agg.value}_metadata"
                body.append(f"        elem.set_{metadata_field_name}({agg_field}.at(i).get_metadata());")
            body.append("    }")

    body.append("}")

    return CppMethod(
        name=f"populate_{rg_info.name}",
        doc=f"Populate message with batched signal data for report group '{rg_info.name}'.",
        return_type=VOID,
        arguments=[CppNamedType(argument_type=CppType([], "auto", None, ref=Ref.L), argument_name="msg")],
        leading_qualifiers=["inline"],
        trailing_qualifiers=["const"],
        body=body,
        no_discard=False,
    )


def _make_populate_method_for_post_agg_group(
    signals_struct: SignalsStruct,
    rg_info: PostAggregatedReportGroupInfo,
) -> CppMethod:
    """Generate populate_<group>() method to populate a message with post-aggregated signal data.

    Populates the message's execution_count, execution_interval, and signal fields from post-aggregators.

    Args:
        signals_struct: The SignalsStruct containing all signal entries.
        rg_info: The post-aggregated report group info.

    Returns:
        A CppMethod for populate_<group>().
    """
    body = CppChunk()

    body.append(f"msg.set_execution_count(static_cast<uint16_t>(execution_count_{rg_info.name}_));")
    body.append("")

    # Calculate execution interval
    body.append(f"if (window_start_time_{rg_info.name}_ && last_execution_end_time_{rg_info.name}_) {{")
    body.append(
        f"    msg.set_execution_interval(*last_execution_end_time_{rg_info.name}_ - *window_start_time_{rg_info.name}_);"
    )
    body.append("} else {")
    body.append("    msg.set_execution_interval(std::chrono::nanoseconds{0});")
    body.append("}")
    body.append("")

    # For each signal in this group, populate its fields from post-aggregators
    for signal_key in rg_info.signal_keys:
        signal = signals_struct.signals[signal_key]

        if signal.presence_bit_index is not None:
            representative_pre_agg = _sorted_agg_types(signal.pre_aggregation)[0]
            representative_post_agg = _sorted_agg_types(signal.post_aggregation)[0]
            representative_field = _get_post_aggregator_field_name(
                signal.identifier,
                representative_pre_agg,
                representative_post_agg,
            )
            body.context.add_include(Header(JEWELS_REPO, "jewels/callsig/outcome.hh"))
            body.context.add_include(SystemHeader("exception"))
            body.append(f"if ({representative_field}.has_value()) {{")
            body.append(
                f"    if (jewels::fails(msg.get_mutable_signal_presence().try_set({signal.presence_bit_index}))) {{"
            )
            # It's an invariant that presence_bit_index is within the number of
            # bits in the signal_presence bitmask, so this std::terminate is
            # unreachable.
            body.append("        std::terminate();")
            body.append("    }")
            body.append("}")

        for pre_agg in _sorted_agg_types(signal.pre_aggregation):
            for post_agg in _sorted_agg_types(signal.post_aggregation):
                post_agg_field = _get_post_aggregator_field_name(signal.identifier, pre_agg, post_agg)
                field_name = f"{signal.identifier}_{pre_agg.value}_{post_agg.value}"

                body.append(f"if ({post_agg_field}.has_value()) {{")
                # Use get_mean() for mean aggregation, get_value() otherwise
                if post_agg == signal_ir.AggregationType.MEAN:
                    # Cast to float since the schema field for mean is always float
                    body.append(f"    msg.set_{field_name}(static_cast<float>({post_agg_field}.get_mean()));")
                else:
                    body.append(f"    msg.set_{field_name}({post_agg_field}.get_value());")

                # Handle metadata if preserved
                if signal.metadata_type is not None and _preserves_metadata(pre_agg) and _preserves_metadata(post_agg):
                    metadata_field_name = f"{signal.identifier}_{pre_agg.value}_{post_agg.value}_metadata"
                    body.append(f"    msg.set_{metadata_field_name}({post_agg_field}.get_metadata());")

                body.append("}")

    return CppMethod(
        name=f"populate_{rg_info.name}",
        doc=f"Populate message with post-aggregated signal data for report group '{rg_info.name}'.",
        return_type=VOID,
        arguments=[CppNamedType(argument_type=CppType([], "auto", None, ref=Ref.L), argument_name="msg")],
        leading_qualifiers=["inline"],
        trailing_qualifiers=["const"],
        body=body,
        no_discard=False,
    )


def _add_post_agg_management_methods_for_group(
    struct: CppStruct,
    signals_struct: SignalsStruct,
    rg_info: PostAggregatedReportGroupInfo,
) -> None:
    """Add management methods for a post-aggregated report group.

    Adds:
    - reset_<group>(): Resets all pre and post aggregators, execution count, window, and last execution end time for the group.
    - start_of_execution_<group>(): Records the window start time if not already set.
    - end_of_execution_<group>(): Records execution end time and accumulates pre-aggregated values into post aggregators.
    - get_execution_count_<group>(): Returns the number of executions since last reset.
    - should_publish_<group>(): Checks if the signals should be published.

    Args:
        struct: The CppStruct to add methods to.
        signals_struct: The SignalsStruct containing all signal entries.
        rg_info: The post-aggregated report group info.
    """
    struct.private.append(_make_post_agg_reset_method_for_group(signals_struct, rg_info))
    struct.private.append(_make_start_of_execution_method_for_group(rg_info.name))
    struct.private.append(_make_post_agg_end_of_execution_method_for_group(signals_struct, rg_info))
    struct.public.append(_make_post_agg_get_execution_count_method_for_group(rg_info))
    struct.private.append(_make_should_publish_method_for_post_agg_group(rg_info))


def _make_post_agg_reset_method_for_group(
    signals_struct: SignalsStruct,
    rg_info: PostAggregatedReportGroupInfo,
) -> CppMethod:
    """Generate reset_<group>() method for post-aggregated report group.

    Resets all pre-aggregators, post-aggregators, the execution count, and the window start time.

    Args:
        signals_struct: The SignalsStruct containing all signal entries.
        rg_info: The post-aggregated report group info.

    Returns:
        A CppMethod for reset_<group>().
    """
    body = CppChunk()

    # Reset the execution count for this group
    body.append(f"execution_count_{rg_info.name}_ = 0;")

    body.append(f"window_start_time_{rg_info.name}_ = std::nullopt;")
    body.append(f"last_execution_end_time_{rg_info.name}_ = std::nullopt;")

    # Reset all pre-aggregators and post-aggregators for signals in this group
    for signal_key in rg_info.signal_keys:
        signal = signals_struct.signals[signal_key]

        # Get effective pre-aggregation types
        pre_aggs = (
            _sorted_agg_types(signal.pre_aggregation)
            if signal.has_pre_aggregation()
            else [signal_ir.AggregationType.VALUE]
        )

        for pre_agg in pre_aggs:
            # Reset pre-aggregator
            pre_agg_field = _get_aggregator_field_name(signal.identifier, pre_agg)
            body.append(f"{pre_agg_field}.reset();")

            # Reset post-aggregators if signal has post-aggregation
            if signal.post_aggregation:
                for post_agg in _sorted_agg_types(signal.post_aggregation):
                    post_agg_field = _get_post_aggregator_field_name(signal.identifier, pre_agg, post_agg)
                    body.append(f"{post_agg_field}.reset();")

    return CppMethod(
        name=f"reset_{rg_info.name}",
        doc=f"Reset all aggregators, execution count, window start time, and last execution end time for report group '{rg_info.name}'.",
        return_type=VOID,
        arguments=[],
        leading_qualifiers=[],
        trailing_qualifiers=[],
        body=body,
        no_discard=False,
    )


def _make_post_agg_end_of_execution_method_for_group(
    signals_struct: SignalsStruct,
    rg_info: PostAggregatedReportGroupInfo,
) -> CppMethod:
    """Generate end_of_execution_<group>() method for post-aggregated report group.

    Records the execution end time, accumulates pre-aggregated values into post-aggregators, and resets pre-aggregators.
    Also increments the execution count.

    Args:
        signals_struct: The SignalsStruct containing all signal entries.
        rg_info: The post-aggregated report group info.

    Returns:
        A CppMethod for end_of_execution_<group>().
    """
    sync_time_type = CppType(
        includes=[Header(JEWELS_REPO, "jewels/time/sync_time.hh")],
        type_name="SyncTime",
        cpp_namespace="jewels::time",
    )

    body = CppChunk()

    body.append(f"last_execution_end_time_{rg_info.name}_ = current_time;")

    body.append(f"++execution_count_{rg_info.name}_;")

    for signal_key in rg_info.signal_keys:
        signal = signals_struct.signals[signal_key]

        if not signal.post_aggregation:
            continue

        pre_aggs = (
            _sorted_agg_types(signal.pre_aggregation)
            if signal.has_pre_aggregation()
            else [signal_ir.AggregationType.VALUE]
        )

        for pre_agg in pre_aggs:
            pre_agg_field = _get_aggregator_field_name(signal.identifier, pre_agg)
            preserves_meta = _preserves_metadata(pre_agg)

            for post_agg in _sorted_agg_types(signal.post_aggregation):
                post_agg_field = _get_post_aggregator_field_name(signal.identifier, pre_agg, post_agg)
                post_preserves_meta = _preserves_metadata(post_agg)

                body.append(f"if ({pre_agg_field}.has_value()) {{")
                if preserves_meta and post_preserves_meta and signal.metadata_type is not None:
                    body.append(
                        f"    {post_agg_field}.accumulate({pre_agg_field}.get_value(), {pre_agg_field}.get_metadata());"
                    )
                else:
                    body.append(f"    {post_agg_field}.accumulate({pre_agg_field}.get_value());")
                body.append("}")

    # Reset all pre-aggregators after accumulating
    for signal_key in rg_info.signal_keys:
        signal = signals_struct.signals[signal_key]
        pre_aggs = (
            _sorted_agg_types(signal.pre_aggregation)
            if signal.has_pre_aggregation()
            else [signal_ir.AggregationType.VALUE]
        )

        for pre_agg in pre_aggs:
            pre_agg_field = _get_aggregator_field_name(signal.identifier, pre_agg)
            body.append(f"{pre_agg_field}.reset();")

    return CppMethod(
        name=f"end_of_execution_{rg_info.name}",
        doc=f"Record execution end time and accumulate pre-aggregated values into post-aggregators for report group '{rg_info.name}'.",
        return_type=VOID,
        arguments=[CppNamedType(argument_type=sync_time_type, argument_name="current_time")],
        leading_qualifiers=[],
        trailing_qualifiers=[],
        body=body,
        no_discard=False,
    )


def _make_post_agg_get_execution_count_method_for_group(rg_info: PostAggregatedReportGroupInfo) -> CppMethod:
    """Generate get_execution_count_<group>() method for post-aggregated report group.

    Args:
        rg_info: The post-aggregated report group info.

    Returns:
        A CppMethod for get_execution_count_<group>().
    """
    body = CppChunk()
    body.append(f"return execution_count_{rg_info.name}_;")

    size_t_type = CppType(includes=[], type_name="size_t", cpp_namespace=None)
    return CppMethod(
        name=f"get_execution_count_{rg_info.name}",
        doc=f"Get the number of executions since last reset for report group '{rg_info.name}'.",
        return_type=size_t_type,
        arguments=[],
        leading_qualifiers=[],
        trailing_qualifiers=["const"],
        body=body,
        no_discard=True,
    )


def _make_should_publish_method_for_post_agg_group(rg_info: PostAggregatedReportGroupInfo) -> CppMethod:
    """Generate should_publish_<group>() method to check if the signals should be published.

    Uses the time from the last end_of_execution call to determine elapsed time.
    Returns true if:
    - max_duration is reached, OR
    - max_observations is reached AND min_duration is met (if specified)

    Args:
        rg_info: The post-aggregated report group info.

    Returns:
        A CppMethod for should_publish_<group>().
    """
    body = CppChunk()

    # Window must be started and at least one execution must have completed
    body.append(f"if (!window_start_time_{rg_info.name}_ || !last_execution_end_time_{rg_info.name}_) {{")
    body.append("    return false;")
    body.append("}")
    body.append("")

    # Only compute elapsed time when duration-based checks are needed
    has_duration_check = rg_info.max_duration_ns is not None or rg_info.min_duration_ns is not None
    if has_duration_check:
        body.append(
            f"const auto elapsed = *last_execution_end_time_{rg_info.name}_ - *window_start_time_{rg_info.name}_;"
        )
    body.append("")

    # Check max_duration first - publish regardless of observations
    if rg_info.max_duration_ns is not None:
        body.append(f"if (elapsed >= max_duration_{rg_info.name}) {{")
        body.append("    return true;")
        body.append("}")
        body.append("")

    # Check if max_observations is reached
    if rg_info.max_observations is not None:
        body.append(f"const auto observations = execution_count_{rg_info.name}_;")
        body.append(f"if (observations >= {rg_info.max_observations}) {{")
        # If min_duration is specified, must also be met
        if rg_info.min_duration_ns is not None:
            body.append(f"    return elapsed >= min_duration_{rg_info.name};")
        else:
            body.append("    return true;")
        body.append("}")
        body.append("")

    body.append("return false;")

    return CppMethod(
        name=f"should_publish_{rg_info.name}",
        doc=f"Check if report group '{rg_info.name}' should be published based on observations and duration.",
        return_type=BOOLEAN,
        arguments=[],
        leading_qualifiers=[],
        trailing_qualifiers=["const"],
        body=body,
        no_discard=True,
    )


def _add_post_aggregator_fields(
    struct: CppStruct,
    signals_struct: SignalsStruct,
    post_agg_groups: dict[str, PostAggregatedReportGroupInfo],
) -> None:
    """Add private post-aggregator fields for post-aggregated signals.

    Post-aggregators accumulate pre-aggregated values across multiple cog executions.
    For each signal, we create one post-aggregator for each (pre_agg, post_agg) combination.

    Args:
        struct: The CppStruct to add fields to.
        signals_struct: The SignalsStruct containing all signal entries.
        post_agg_groups: Dictionary mapping report group name to PostAggregatedReportGroupInfo.
    """
    for rg_info in post_agg_groups.values():
        for signal_key in rg_info.signal_keys:
            signal = signals_struct.signals[signal_key]

            if not signal.post_aggregation:
                continue

            pre_aggs = (
                _sorted_agg_types(signal.pre_aggregation)
                if signal.has_pre_aggregation()
                else [signal_ir.AggregationType.VALUE]
            )

            for pre_agg in pre_aggs:
                pre_agg_metadata_type = signal.metadata_type if _preserves_metadata(pre_agg) else None

                for post_agg in _sorted_agg_types(signal.post_aggregation):
                    agg_type = _get_aggregator_type(post_agg, signal.signal_type, pre_agg_metadata_type)
                    field_name = _get_post_aggregator_field_name(signal.identifier, pre_agg, post_agg)
                    struct.private.append(
                        CppNamedValue(
                            named_type=CppNamedType(
                                argument_type=agg_type,
                                argument_name=field_name,
                            ),
                            value=None,
                            doc=f"Post-aggregator for signal '{signal.signal_name}' ({pre_agg.value} -> {post_agg.value}).",
                            render_initializer=False,
                        )
                    )


def _add_signal_methods(
    struct: CppStruct,
    signals_struct: SignalsStruct,
    post_agg_groups: dict[str, PostAggregatedReportGroupInfo],
) -> None:
    """Add set_/accumulate_/get_ methods to the SignalApi struct.

    Signals without pre-aggregation use set_ methods (with VALUE aggregator).
    Signals with pre-aggregation use accumulate_ methods.
    Getter methods are generated based on the number of aggregations:
    - No or single aggregation: get_<identifier>()
    - Multiple aggregations: get_<identifier>_<agg>() for each

    For batched signals, getters retrieve from pre-aggregators at current batch index.
    For post-aggregated signals, getters retrieve from post-aggregators.

    Args:
        struct: The CppStruct to add methods to.
        signals_struct: The SignalsStruct containing all signal entries.
        post_agg_groups: Dictionary mapping report group name to PostAggregatedReportGroupInfo.
    """
    for signal in signals_struct.signals.values():
        if signal.is_batched:
            rg_name = signal.report_group_name

            if signal.has_pre_aggregation():
                struct.public.append(_make_accumulate_method(signal, rg_name))
            else:
                struct.public.append(_make_set_method(signal, rg_name))

            struct.public.extend(_make_getter_methods(signal, rg_name))

    for rg_info in post_agg_groups.values():
        for signal_key in rg_info.signal_keys:
            signal = signals_struct.signals[signal_key]

            if signal.has_pre_aggregation():
                struct.public.append(_make_post_agg_accumulate_method(signal))
            else:
                struct.public.append(_make_post_agg_set_method(signal))

            struct.public.extend(_make_post_agg_getter_methods(signal))


def _make_accumulate_method(signal: SignalEntry, rg_name: str) -> CppMethod:
    """Generate accumulate_<identifier> method for a signal with pre-aggregation.

    Pre-aggregation happens immediately at the point of the C++ call.
    The data will be extracted from aggregators at a different stage.

    Args:
        signal: The SignalEntry to generate an accumulate method for.
        rg_name: The report group name for this signal (used for batch index).

    Returns:
        A CppMethod for accumulate_<identifier>().
    """
    args = [
        CppNamedType(
            argument_type=deepcopy(signal.signal_type),
            argument_name="value",
        )
    ]
    if signal.metadata_type is not None:
        args.append(
            CppNamedType(
                argument_type=deepcopy(signal.metadata_type),
                argument_name="metadata",
                qualifiers=["const"],
            )
        )

    body = CppChunk()
    batch_index = f"current_batch_index_{rg_name}_"

    pre_aggs = (
        _expand_mean_for_batched(signal.pre_aggregation)
        if signal.is_batched
        else _sorted_agg_types(signal.pre_aggregation)
    )

    any_uses_metadata = signal.metadata_type is not None and any(_preserves_metadata(pre_agg) for pre_agg in pre_aggs)

    if signal.metadata_type is not None and not any_uses_metadata:
        body.append("(void)metadata;  // Metadata not preserved by any aggregator")

    for pre_agg in pre_aggs:
        agg_field = _get_aggregator_field_name(signal.identifier, pre_agg)
        if signal.metadata_type is not None and _preserves_metadata(pre_agg):
            body.append(f"{agg_field}.at({batch_index}).accumulate(value, metadata);")
        else:
            body.append(f"{agg_field}.at({batch_index}).accumulate(value);")

    return CppMethod(
        name=f"accumulate_{signal.identifier}",
        doc=f"Accumulate a value for signal '{signal.signal_name}'.",
        return_type=VOID,
        arguments=args,
        leading_qualifiers=[],
        trailing_qualifiers=[],
        body=body,
        no_discard=False,
    )


def _make_set_method(signal: SignalEntry, rg_name: str) -> CppMethod:
    """Generate set_<identifier> method for a signal without pre-aggregation.

    Signals without pre-aggregation use a VALUE aggregator (FinalValueAggregator)
    to store the latest value.

    """
    args = [
        CppNamedType(
            argument_type=deepcopy(signal.signal_type),
            argument_name="value",
        )
    ]
    if signal.metadata_type is not None:
        args.append(
            CppNamedType(
                argument_type=deepcopy(signal.metadata_type),
                argument_name="metadata",
                qualifiers=["const"],
            )
        )

    body = CppChunk()
    batch_index = f"current_batch_index_{rg_name}_"

    # VALUE aggregation always preserves metadata
    agg_field = _get_aggregator_field_name(signal.identifier, signal_ir.AggregationType.VALUE)
    if signal.metadata_type is not None:
        body.append(f"{agg_field}.at({batch_index}).accumulate(value, metadata);")
    else:
        body.append(f"{agg_field}.at({batch_index}).accumulate(value);")

    return CppMethod(
        name=f"set_{signal.identifier}",
        doc=f"Set the value for signal '{signal.signal_name}'.",
        return_type=VOID,
        arguments=args,
        leading_qualifiers=[],
        trailing_qualifiers=[],
        body=body,
        no_discard=False,
    )


def _make_getter_methods(signal: SignalEntry, rg_name: str) -> list[CppMethod]:
    """Generate getter methods for retrieving current aggregated values.

    The getter methods allow user code to retrieve the current value of a signal
    within the current reporting interval.

    For signals with no pre-aggregation or a single aggregation type:
        get_<identifier>(Out<T> value) -> BinaryOutcome
        get_<identifier>(Out<T> value, OptionalOut<M> metadata) -> BinaryOutcome (if metadata preserved)

    For signals with multiple aggregation types:
        get_<identifier>_<agg>(Out<T> value) -> BinaryOutcome
        get_<identifier>_<agg>(Out<T> value, OptionalOut<M> metadata) -> BinaryOutcome (if metadata preserved)

    The methods return failure if the signal has not yet been set/accumulated in the
    current reporting interval.

    Args:
        signal: The SignalEntry to generate getter methods for.
        rg_name: The report group name for this signal (used for batch index).

    Returns:
        A list of CppMethod objects for the getter methods.
    """
    methods: list[CppMethod] = []

    if signal.has_pre_aggregation():
        pre_aggs = (
            _expand_mean_for_batched(signal.pre_aggregation)
            if signal.is_batched
            else _sorted_agg_types(signal.pre_aggregation)
        )
    else:
        pre_aggs = [signal_ir.AggregationType.VALUE]

    has_multiple_aggregations = len(pre_aggs) > 1

    for pre_agg in pre_aggs:
        method = _make_single_getter_method(signal, pre_agg, has_multiple_aggregations, rg_name)
        methods.append(method)

    return methods


def _make_single_getter_method(
    signal: SignalEntry,
    pre_agg: signal_ir.AggregationType,
    include_agg_in_name: bool,
    rg_name: str,
) -> CppMethod:
    """Generate a single getter method for one aggregation type.

    Args:
        signal: The SignalEntry to generate a getter for.
        pre_agg: The pre-aggregation type this getter retrieves.
        include_agg_in_name: If True, append the aggregation type to the method name.
        rg_name: The report group name for this signal (used for batch index).

    Returns:
        A CppMethod for the getter.
    """
    method_name = f"get_{signal.identifier}_{pre_agg.value}" if include_agg_in_name else f"get_{signal.identifier}"

    value_out_type = _OUT_TEMPLATE.instantiate([deepcopy(signal.signal_type)])
    args = [
        CppNamedType(
            argument_type=value_out_type,
            argument_name="value",
        )
    ]

    has_metadata_param = signal.metadata_type is not None and _preserves_metadata(pre_agg)
    if has_metadata_param:
        assert signal.metadata_type is not None  # Type guard for pyright
        metadata_out_type = _OPTIONAL_OUT_TEMPLATE.instantiate([deepcopy(signal.metadata_type)])
        args.append(
            CppNamedType(
                argument_type=metadata_out_type,
                argument_name="metadata",
            )
        )

    body = CppChunk()
    agg_field = _get_aggregator_field_name(signal.identifier, pre_agg)
    batch_index = f"current_batch_index_{rg_name}_"

    body.append(f"if (!{agg_field}.at({batch_index}).has_value()) {{")
    body.append("    return ::jewels::failure;")
    body.append("}")
    body.append(f"*value = {agg_field}.at({batch_index}).get_value();")
    if has_metadata_param:
        body.append("if (metadata.has_value()) {")
        body.append(f"    *metadata = {agg_field}.at({batch_index}).get_metadata();")
        body.append("}")
    body.append("return ::jewels::success;")

    return CppMethod(
        name=method_name,
        doc=f"Get the current {pre_agg.value} value for signal '{signal.signal_name}'.",
        return_type=BINARY_OUTCOME,
        arguments=args,
        leading_qualifiers=[],
        trailing_qualifiers=["const"],
        body=body,
        no_discard=True,
    )


def _add_pre_aggregator_fields(
    struct: CppStruct,
    signals_struct: SignalsStruct,
    batched_groups: dict[str, BatchedReportGroupInfo],
) -> None:
    """Add private aggregator array fields for batched signals.

    Aggregators are stored as std::array<AggregatorType, batch_size_<group>> to store
    values for each execution in the batch. Each signal uses the batch size of its
    containing report group.

    Only batched signals get aggregators. Signals with pre-aggregation use the
    specified aggregation types, while signals without pre-aggregation use VALUE
    aggregation (FinalValueAggregator). Post-aggregated signals will be handled separately.

    Args:
        struct: The CppStruct to add fields to.
        signals_struct: The SignalsStruct containing all signal entries.
        batched_groups: Dictionary mapping report group name to BatchedReportGroupInfo.
    """
    for signal in signals_struct.signals.values():
        if not signal.is_batched:
            continue

        rg_name = signal.report_group_name
        if rg_name not in batched_groups:
            msg = f"Signal '{signal.signal_name}' is batched but its report group '{rg_name}' was not found."
            raise RuntimeError(msg)

        # Get effective pre-aggregation types (defaults to VALUE if none specified)
        if signal.has_pre_aggregation():
            pre_aggs = _expand_mean_for_batched(signal.pre_aggregation)
        else:
            pre_aggs = [signal_ir.AggregationType.VALUE]

        for pre_agg in pre_aggs:
            agg_type = _get_aggregator_type(pre_agg, signal.signal_type, signal.metadata_type)
            array_type = _get_aggregator_array_type_for_group(agg_type, rg_name)
            field_name = _get_aggregator_field_name(signal.identifier, pre_agg)
            struct.private.append(
                CppNamedValue(
                    named_type=CppNamedType(
                        argument_type=array_type,
                        argument_name=field_name,
                    ),
                    value=None,
                    doc=f"Aggregator array for signal '{signal.signal_name}' with {pre_agg.value} aggregation.",
                    render_initializer=False,
                )
            )


def _get_aggregator_type(
    agg_type: signal_ir.AggregationType,
    value_type: CppType | CppTemplateType,
    metadata_type: CppType | CppTemplateType | None,
) -> CppTemplateType:
    """Get the C++ aggregator type for the given aggregation type.

    Args:
        agg_type: The aggregation type.
        value_type: The C++ type of the signal value.
        metadata_type: The C++ type of the signal metadata, or None.

    Returns:
        A CppTemplateType representing the aggregator type.
    """
    class_name = _AGGREGATOR_CLASS_MAP[agg_type]
    template_args: list[CppTypeExpr | CppValueExpr] = [deepcopy(value_type)]

    if metadata_type is not None and _preserves_metadata(agg_type):
        template_args.append(deepcopy(metadata_type))

    return CppTemplateType(
        include=[],
        template_name=class_name,
        cpp_namespace="clockwork",
        arguments=template_args,
    )


def _get_aggregator_array_type_for_group(
    agg_type: CppTemplateType,
    rg_name: str,
) -> CppTemplateType:
    """Wrap an aggregator type in std::array using the per-group batch size constant.

    Args:
        agg_type: The C++ aggregator type.
        rg_name: The report group name (used to reference batch_size_<rg_name>).

    Returns:
        A CppTemplateType representing std::array<AggregatorType, batch_size_<rg_name>>.
    """
    return CppTemplateType(
        include=[SystemHeader("array")],
        template_name="array",
        cpp_namespace="std",
        arguments=[agg_type, CppValue(None, f"batch_size_{rg_name}")],
    )


def _get_aggregator_field_name(signal_id: str, pre_agg: signal_ir.AggregationType) -> str:
    """Get the field name for an aggregator member variable."""
    return f"{signal_id}_{pre_agg.value}_aggregator_"


def _expand_mean_for_batched(
    pre_agg_types: set[signal_ir.AggregationType],
) -> list[signal_ir.AggregationType]:
    """Expand MEAN aggregation to SUM and COUNT for batched report groups.

    For batched report groups, MEAN is stored as SUM and COUNT so that the mean
    can be computed in post-processing.

    Args:
        pre_agg_types: The original set of pre-aggregation types.

    Returns:
        A list of aggregation types with MEAN replaced by SUM and COUNT.
    """
    result: list[signal_ir.AggregationType] = []
    for agg in _sorted_agg_types(pre_agg_types):
        if agg == signal_ir.AggregationType.MEAN:
            result.append(signal_ir.AggregationType.SUM)
            result.append(signal_ir.AggregationType.COUNT)
        else:
            result.append(agg)
    return result


def _preserves_metadata(agg_type: signal_ir.AggregationType) -> bool:
    """Return True if the aggregation type preserves metadata.

    Metadata is preserved through min, max, first_value, final_value, and value aggregations.
    Metadata is stripped by sum, count, and mean aggregations.
    """
    return agg_type in {
        signal_ir.AggregationType.VALUE,
        signal_ir.AggregationType.MIN,
        signal_ir.AggregationType.MAX,
        signal_ir.AggregationType.FIRST_VALUE,
        signal_ir.AggregationType.FINAL_VALUE,
    }


def _add_post_agg_pre_aggregator_fields(
    struct: CppStruct,
    signals_struct: SignalsStruct,
    post_agg_groups: dict[str, PostAggregatedReportGroupInfo],
) -> None:
    """Add private pre-aggregator fields for post-aggregated signals.

    Unlike batched signals which store arrays of aggregators, post-aggregated signals
    use single aggregators that are reset after each execution.

    Args:
        struct: The CppStruct to add fields to.
        signals_struct: The SignalsStruct containing all signal entries.
        post_agg_groups: Dictionary mapping report group name to PostAggregatedReportGroupInfo.
    """
    for rg_info in post_agg_groups.values():
        for signal_key in rg_info.signal_keys:
            signal = signals_struct.signals[signal_key]

            pre_aggs = (
                _sorted_agg_types(signal.pre_aggregation)
                if signal.has_pre_aggregation()
                else [signal_ir.AggregationType.VALUE]
            )

            for pre_agg in pre_aggs:
                agg_type = _get_aggregator_type(pre_agg, signal.signal_type, signal.metadata_type)
                field_name = _get_aggregator_field_name(signal.identifier, pre_agg)
                struct.private.append(
                    CppNamedValue(
                        named_type=CppNamedType(
                            argument_type=agg_type,
                            argument_name=field_name,
                        ),
                        value=None,
                        doc=f"Pre-aggregator for signal '{signal.signal_name}' with {pre_agg.value} aggregation.",
                        render_initializer=False,
                    )
                )


def _make_post_agg_accumulate_method(signal: SignalEntry) -> CppMethod:
    """Generate accumulate_<identifier> method for a post-aggregated signal with pre-aggregation.

    Args:
        signal: The SignalEntry to generate an accumulate method for.

    Returns:
        A CppMethod for accumulate_<identifier>().
    """
    args = [
        CppNamedType(
            argument_type=deepcopy(signal.signal_type),
            argument_name="value",
        )
    ]
    if signal.metadata_type is not None:
        args.append(
            CppNamedType(
                argument_type=deepcopy(signal.metadata_type),
                argument_name="metadata",
                qualifiers=["const"],
            )
        )

    body = CppChunk()

    pre_aggs = _sorted_agg_types(signal.pre_aggregation)

    any_uses_metadata = signal.metadata_type is not None and any(_preserves_metadata(pre_agg) for pre_agg in pre_aggs)

    if signal.metadata_type is not None and not any_uses_metadata:
        body.append("(void)metadata;  // Metadata not preserved by any aggregator")

    for pre_agg in pre_aggs:
        agg_field = _get_aggregator_field_name(signal.identifier, pre_agg)
        if signal.metadata_type is not None and _preserves_metadata(pre_agg):
            body.append(f"{agg_field}.accumulate(value, metadata);")
        else:
            body.append(f"{agg_field}.accumulate(value);")

    return CppMethod(
        name=f"accumulate_{signal.identifier}",
        doc=f"Accumulate a value for signal '{signal.signal_name}'.",
        return_type=VOID,
        arguments=args,
        leading_qualifiers=[],
        trailing_qualifiers=[],
        body=body,
        no_discard=False,
    )


def _make_post_agg_set_method(signal: SignalEntry) -> CppMethod:
    """Generate set_<identifier> method for a post-aggregated signal without pre-aggregation.

    Args:
        signal: The SignalEntry to generate a set method for.

    Returns:
        A CppMethod for set_<identifier>().
    """
    args = [
        CppNamedType(
            argument_type=deepcopy(signal.signal_type),
            argument_name="value",
        )
    ]
    if signal.metadata_type is not None:
        args.append(
            CppNamedType(
                argument_type=deepcopy(signal.metadata_type),
                argument_name="metadata",
                qualifiers=["const"],
            )
        )

    body = CppChunk()

    agg_field = _get_aggregator_field_name(signal.identifier, signal_ir.AggregationType.VALUE)
    if signal.metadata_type is not None:
        body.append(f"{agg_field}.accumulate(value, metadata);")
    else:
        body.append(f"{agg_field}.accumulate(value);")

    return CppMethod(
        name=f"set_{signal.identifier}",
        doc=f"Set the value for signal '{signal.signal_name}'.",
        return_type=VOID,
        arguments=args,
        leading_qualifiers=[],
        trailing_qualifiers=[],
        body=body,
        no_discard=False,
    )


def _make_post_agg_getter_methods(signal: SignalEntry) -> list[CppMethod]:
    """Generate getter methods for post-aggregated signals.

    For post-aggregated signals, getters retrieve from post-aggregators.
    The naming convention depends on the number of aggregation combinations:
    - Single (pre_agg, post_agg) pair: get_<identifier>()
    - Multiple pairs: get_<identifier>_<pre_agg>_<post_agg>()

    Args:
        signal: The SignalEntry to generate getter methods for.

    Returns:
        A list of CppMethod objects for the getter methods.
    """
    methods: list[CppMethod] = []

    if not signal.post_aggregation:
        return methods

    pre_aggs = (
        _sorted_agg_types(signal.pre_aggregation) if signal.has_pre_aggregation() else [signal_ir.AggregationType.VALUE]
    )

    total_combinations = len(pre_aggs) * len(signal.post_aggregation)
    include_suffix = total_combinations > 1

    for pre_agg in pre_aggs:
        pre_agg_preserves_metadata = _preserves_metadata(pre_agg)
        for post_agg in _sorted_agg_types(signal.post_aggregation):
            method = _make_single_post_agg_getter_method(
                signal=signal,
                pre_agg=pre_agg,
                post_agg=post_agg,
                pre_agg_preserves_metadata=pre_agg_preserves_metadata,
                include_suffix=include_suffix,
            )
            methods.append(method)

    return methods


def _make_single_post_agg_getter_method(
    signal: SignalEntry,
    pre_agg: signal_ir.AggregationType,
    post_agg: signal_ir.AggregationType,
    pre_agg_preserves_metadata: bool,
    include_suffix: bool,
) -> CppMethod:
    """Generate a single getter method for one post-aggregation type.

    Args:
        signal: The SignalEntry to generate a getter for.
        pre_agg: The pre-aggregation type.
        post_agg: The post-aggregation type this getter retrieves.
        pre_agg_preserves_metadata: Whether pre-aggregation preserves metadata.
        include_suffix: If True, append the aggregation types to the method name.

    Returns:
        A CppMethod for the getter.
    """
    if include_suffix:
        method_name = f"get_{signal.identifier}_{pre_agg.value}_{post_agg.value}"
    else:
        method_name = f"get_{signal.identifier}"

    if post_agg == signal_ir.AggregationType.MEAN:
        out_type = CppType(includes=[], type_name="double", cpp_namespace=None)
    else:
        out_type = deepcopy(signal.signal_type)

    value_out_type: CppTemplateType = _OUT_TEMPLATE.instantiate([out_type])
    args = [
        CppNamedType(
            argument_type=value_out_type,
            argument_name="value",
        )
    ]

    has_metadata_param = (
        signal.metadata_type is not None and pre_agg_preserves_metadata and _preserves_metadata(post_agg)
    )
    if has_metadata_param:
        assert signal.metadata_type is not None  # Type guard for pyright
        metadata_out_type = _OPTIONAL_OUT_TEMPLATE.instantiate([deepcopy(signal.metadata_type)])
        args.append(
            CppNamedType(
                argument_type=metadata_out_type,
                argument_name="metadata",
            )
        )

    body = CppChunk()
    post_agg_field = _get_post_aggregator_field_name(signal.identifier, pre_agg, post_agg)

    body.append(f"if (!{post_agg_field}.has_value()) {{")
    body.append("    return ::jewels::failure;")
    body.append("}")

    getter_method = "get_mean" if post_agg == signal_ir.AggregationType.MEAN else "get_value"
    body.append(f"*value = {post_agg_field}.{getter_method}();")

    if has_metadata_param:
        body.append("if (metadata.has_value()) {")
        body.append(f"    *metadata = {post_agg_field}.get_metadata();")
        body.append("}")
    body.append("return ::jewels::success;")

    return CppMethod(
        name=method_name,
        doc=f"Get the {pre_agg.value} -> {post_agg.value} aggregated value for signal '{signal.signal_name}'.",
        return_type=BINARY_OUTCOME,
        arguments=args,
        leading_qualifiers=[],
        trailing_qualifiers=["const"],
        body=body,
        no_discard=True,
    )


def _get_post_aggregator_field_name(
    signal_id: str,
    pre_agg: signal_ir.AggregationType,
    post_agg: signal_ir.AggregationType,
) -> str:
    """Get the field name for a post-aggregator member variable.

    Args:
        signal_id: The signal identifier.
        pre_agg: The pre-aggregation type.
        post_agg: The post-aggregation type.

    Returns:
        The field name for the post-aggregator (e.g., "my_signal_min_max_post_aggregator_").
    """
    return f"{signal_id}_{pre_agg.value}_{post_agg.value}_post_aggregator_"
