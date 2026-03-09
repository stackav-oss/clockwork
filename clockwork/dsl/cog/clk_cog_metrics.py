# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Utilities for Clockwork cog metrics schema generation.

Provides common imports for cog telemetry and event metrics schemas used
in Clockwork framework performance monitoring and analysis.
"""

from collections.abc import Iterable

from clockwork.dsl.ir import node
from clockwork.dsl.ir.module_id import CLK_REPO


def get_cog_metrics_imports(module: node.Module) -> Iterable[node.Module.UseResult]:
    """Returns a list of common cog metrics imports."""
    repo = "" if module.module_id.repo == CLK_REPO else CLK_REPO
    imports = []
    imports.append(
        node.Module.UseResult(
            repo=repo,
            path=(
                "clockwork",
                "dsl",
                "cog",
                "common_cog_telemetry_metrics",
                "InputChannelTelemetryMetrics",
            ),
            alias=None,
            use_targets=None,
            use_type=node.UseResultType.entity,
            cst_node=None,
        )
    )
    imports.append(
        node.Module.UseResult(
            repo=repo,
            path=(
                "clockwork",
                "dsl",
                "cog",
                "common_cog_telemetry_metrics",
                "CogTelemetryMetrics",
            ),
            alias=None,
            use_targets=None,
            use_type=node.UseResultType.entity,
            cst_node=None,
        )
    )
    imports.append(
        node.Module.UseResult(
            repo=repo,
            path=(
                "clockwork",
                "dsl",
                "cog",
                "common_cog_telemetry_metrics",
                "MinMaxMean16",
            ),
            alias=None,
            use_targets=None,
            use_type=node.UseResultType.entity,
            cst_node=None,
        )
    )
    imports.append(
        node.Module.UseResult(
            repo=repo,
            path=("clockwork", "dsl", "cog", "common_cog_event_metrics", "CogEventMetrics"),
            alias=None,
            use_targets=None,
            use_type=node.UseResultType.entity,
            cst_node=None,
        )
    )
    imports.append(
        node.Module.UseResult(
            repo=repo,
            path=("clockwork", "dsl", "cog", "common_cog_event_metrics", "InputChannelEventMetrics"),
            alias=None,
            use_targets=None,
            use_type=node.UseResultType.entity,
            cst_node=None,
        )
    )
    return imports
