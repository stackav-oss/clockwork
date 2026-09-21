# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for report_group_policy."""

from __future__ import annotations

from clockwork.dsl.cog import report_group_policy
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir import compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID

_SIGNAL_COG_SOURCE = """
#![generate(cpp, cpp_cog)]
#![cpp(namespace=clockwork::testing)]
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

// Cog with a signal report group.
cog SignalCog
{
  signals report_group
  {
    // Value reported by the cog.
    value: signal UInt64
    {
      post_aggregation: ["final_value"];
    }
  }

  execution
  {
    execute when: init;
  }
}

policy ReportGroupPolicy for SignalCog.report_group
{
  reporting_strategy = ReportingStrategy::post_aggregated;
  log_type = ReportGroupLogType::non_redundant_telemetry;
  max_observations = 1;
}
"""


def test_report_group_policy_entities_are_shared_across_imported_modules() -> None:
    """Report-group policy entities use their defining module's context across a diamond import."""
    fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)
    left_module = compiler.compile_source_text(
        _SIGNAL_COG_SOURCE,
        ModuleID(CLK_REPO, "report_group_policy_left"),
        fs_importer,
    )
    right_module = compiler.compile_source_text(
        _SIGNAL_COG_SOURCE,
        ModuleID(CLK_REPO, "report_group_policy_right"),
        fs_importer,
    )

    merged_context = CompilerContext("report_group_policy_parent")
    merged_context.import_from(left_module.context)
    merged_context.import_from(right_module.context)

    left_entities = report_group_policy.get_entities(left_module.context)
    right_entities = report_group_policy.get_entities(right_module.context)
    assert left_entities.reporting_strategy is right_entities.reporting_strategy
    assert left_entities.report_group_log_type is right_entities.report_group_log_type
    assert left_entities.report_group_policy_config is right_entities.report_group_policy_config
    assert left_entities.report_group_policy is right_entities.report_group_policy
