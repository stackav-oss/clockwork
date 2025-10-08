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
                "cog_telemetry_metrics_tach",
            ),
            alias=None,
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
                "cog_telemetry_metrics_tappy",
            ),
            alias=None,
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
                "input_telemetry_metrics_rep",
            ),
            alias=None,
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
                "min_max_16",
            ),
            alias=None,
            cst_node=None,
        )
    )
    imports.append(
        node.Module.UseResult(
            repo=repo,
            path=("clockwork", "dsl", "cog", "common_cog_event_metrics", "cog_event_metrics_tach"),
            alias=None,
            cst_node=None,
        )
    )
    imports.append(
        node.Module.UseResult(
            repo=repo,
            path=("clockwork", "dsl", "cog", "common_cog_event_metrics", "cog_event_metrics_tappy"),
            alias=None,
            cst_node=None,
        )
    )
    imports.append(
        node.Module.UseResult(
            repo=repo,
            path=("clockwork", "dsl", "cog", "common_cog_event_metrics", "input_event_metrics_rep"),
            alias=None,
            cst_node=None,
        )
    )
    return imports
