# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for aligner_metrics_report_groups.generate_aligner_metrics_report_groups."""

from __future__ import annotations

from typing import Final

import pytest
from clockwork.dsl.ir import aligner, compiler
from clockwork.dsl.ir.aligner_metrics_report_groups import generate_aligner_metrics_report_groups
from clockwork.dsl.ir.cog_metrics_report_groups import EVENT_METRICS_GROUP_NAME, TELEMETRY_METRICS_GROUP_NAME
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.report_group import ReportGroupLogType, ReportingStrategy

# Template uses {{}} for literal braces (.format() escaping).
_ALIGNER_TEMPLATE: Final = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
use std::aligner_metrics_policy::{{AlignerEventMetricsPolicy, AlignerTelemetryMetricsPolicy}};
// Schema for aligner messages.
schema SensorMsg {{
    uuid: aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa;
    fields {{
        // Timestamp.
        #0 observation_time: SyncTime;
    }}
}}
// Test aligner.
aligner TestAligner {{
    inputs {{
        // Primary sensor.
        primary: Tappy<SensorMsg> {{ max_msgs: 10; }}
    }}
}}
{policies}
"""


def _compile(module_name: str, policies: str, fs_importer: FilesystemImporter) -> aligner.Aligner:
    source = _ALIGNER_TEMPLATE.format(policies=policies)
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, module_name), fs_importer)
    aligner_ir = module.inner_scope.lookup("TestAligner")
    assert isinstance(aligner_ir, aligner.Aligner)
    return aligner_ir


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


def test_default_generates_both_groups(fs_importer: FilesystemImporter) -> None:
    """With no policy both groups are produced with the expected log types and registered in scope."""
    aligner_ir = _compile("aligner_rg_defaults", "", fs_importer)
    module = aligner_ir.module
    result = generate_aligner_metrics_report_groups(
        module=module,
        parent_scope=module.inner_scope,
        aligner=aligner_ir,
        input_names=[],
        output_names=["alignment"],
        condition_names=[],
    )

    assert set(result.keys()) == {EVENT_METRICS_GROUP_NAME, TELEMETRY_METRICS_GROUP_NAME}

    event_cfg = result[EVENT_METRICS_GROUP_NAME].report_group_config
    assert event_cfg is not None
    assert event_cfg.reporting_strategy == ReportingStrategy.BATCHED
    assert event_cfg.log_type == ReportGroupLogType.EVENT

    telemetry_cfg = result[TELEMETRY_METRICS_GROUP_NAME].report_group_config
    assert telemetry_cfg is not None
    assert telemetry_cfg.reporting_strategy == ReportingStrategy.POST_AGGREGATED
    assert telemetry_cfg.log_type == ReportGroupLogType.NON_REDUNDANT_TELEMETRY

    for name in result:
        assert module.inner_scope.lookup(name) is result[name]


def test_event_policy_disabled(fs_importer: FilesystemImporter) -> None:
    aligner_ir = _compile(
        "aligner_rg_event_disabled",
        "policy AlignerEventMetricsPolicy for TestAligner { enabled = false; }",
        fs_importer,
    )
    module = aligner_ir.module
    result = generate_aligner_metrics_report_groups(
        module=module,
        parent_scope=module.inner_scope,
        aligner=aligner_ir,
        input_names=[],
        output_names=["alignment"],
        condition_names=[],
    )
    assert EVENT_METRICS_GROUP_NAME not in result
    assert TELEMETRY_METRICS_GROUP_NAME in result


def test_telemetry_policy_disabled(fs_importer: FilesystemImporter) -> None:
    aligner_ir = _compile(
        "aligner_rg_telemetry_disabled",
        "policy AlignerTelemetryMetricsPolicy for TestAligner { enabled = false; }",
        fs_importer,
    )
    module = aligner_ir.module
    result = generate_aligner_metrics_report_groups(
        module=module,
        parent_scope=module.inner_scope,
        aligner=aligner_ir,
        input_names=[],
        output_names=["alignment"],
        condition_names=[],
    )
    assert TELEMETRY_METRICS_GROUP_NAME not in result
    assert EVENT_METRICS_GROUP_NAME in result


def test_both_policies_disabled_returns_empty(fs_importer: FilesystemImporter) -> None:
    policies = (
        "policy AlignerEventMetricsPolicy for TestAligner { enabled = false; }\n"
        "policy AlignerTelemetryMetricsPolicy for TestAligner { enabled = false; }"
    )
    aligner_ir = _compile("aligner_rg_both_disabled", policies, fs_importer)
    module = aligner_ir.module
    result = generate_aligner_metrics_report_groups(
        module=module,
        parent_scope=module.inner_scope,
        aligner=aligner_ir,
        input_names=[],
        output_names=["alignment"],
        condition_names=[],
    )
    assert result == {}


def test_entries_reflect_inputs_outputs_conditions(fs_importer: FilesystemImporter) -> None:
    """Signal entries for inputs, outputs, and conditions appear in both groups."""
    aligner_ir = _compile("aligner_rg_entries", "", fs_importer)
    module = aligner_ir.module
    result = generate_aligner_metrics_report_groups(
        module=module,
        parent_scope=module.inner_scope,
        aligner=aligner_ir,
        input_names=["cam", "lidar"],
        output_names=["alignment"],
        condition_names=["ready"],
    )

    event_entries = result[EVENT_METRICS_GROUP_NAME].entries
    assert "cam_unseen_messages" in event_entries
    assert "lidar_unseen_messages" in event_entries
    assert "alignment_num_messages" in event_entries
    assert "ready_active" in event_entries

    telemetry_entries = result[TELEMETRY_METRICS_GROUP_NAME].entries
    assert "agg_cam_unseen_messages" in telemetry_entries
    assert "agg_alignment_num_messages" in telemetry_entries
    assert "ready_active_count" in telemetry_entries
