# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for cog_metrics_policy and cog_metrics_policy_proto."""

from __future__ import annotations

import pytest
from clockwork.dsl.cog import cog_metrics_policy
from clockwork.dsl.ir import compiler, node
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for compiling .clk files."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def _make_context(fs_importer: FilesystemImporter, name: str) -> node.Module:
    """Compile a trivial module to produce a CompilerContext with the importer registered."""
    source = f"""\
#![generate()]
// Dummy module: {name}
schema {name} {{
    fields {{
        // value
        #0 value: Bool;
    }}
}}
"""
    return compiler.compile_source_text(source, ModuleID(CLK_REPO, name), fs_importer)


class TestCogMetricsPolicyLoader:
    """Tests for the cog_metrics_policy loader module."""

    def test_get_entities_returns_entities(self, fs_importer: FilesystemImporter) -> None:
        """Test that get_entities returns an Entities instance."""
        module = _make_context(fs_importer, "CogMetricsPolicyTest1")
        entities = cog_metrics_policy.get_entities(module.context)
        assert isinstance(entities, cog_metrics_policy.Entities)

    def test_entities_has_event_policy(self, fs_importer: FilesystemImporter) -> None:
        """Test that the CogEventMetricsPolicy is loaded and has the right name."""
        module = _make_context(fs_importer, "CogMetricsPolicyTest2")
        entities = cog_metrics_policy.get_entities(module.context)
        assert entities.cog_event_metrics_policy.name == "CogEventMetricsPolicy"

    def test_entities_has_telemetry_policy(self, fs_importer: FilesystemImporter) -> None:
        """Test that the CogTelemetryMetricsPolicy is loaded and has the right name."""
        module = _make_context(fs_importer, "CogMetricsPolicyTest3")
        entities = cog_metrics_policy.get_entities(module.context)
        assert entities.cog_telemetry_metrics_policy.name == "CogTelemetryMetricsPolicy"

    def test_entities_has_event_policy_config(self, fs_importer: FilesystemImporter) -> None:
        """Test that the CogEventMetricsPolicyConfig dataclass is loaded."""
        module = _make_context(fs_importer, "CogMetricsPolicyTest4")
        entities = cog_metrics_policy.get_entities(module.context)
        assert entities.cog_event_metrics_policy_config is not None

    def test_entities_has_telemetry_policy_config(self, fs_importer: FilesystemImporter) -> None:
        """Test that the CogTelemetryMetricsPolicyConfig dataclass is loaded."""
        module = _make_context(fs_importer, "CogMetricsPolicyTest5")
        entities = cog_metrics_policy.get_entities(module.context)
        assert entities.cog_telemetry_metrics_policy_config is not None

    def test_get_entities_is_idempotent(self, fs_importer: FilesystemImporter) -> None:
        """Test that calling get_entities multiple times returns the same object."""
        module = _make_context(fs_importer, "CogMetricsPolicyTest6")
        entities1 = cog_metrics_policy.get_entities(module.context)
        entities2 = cog_metrics_policy.get_entities(module.context)
        assert entities1 is entities2

    def test_get_cog_event_metrics_policy_convenience(self, fs_importer: FilesystemImporter) -> None:
        """Test the get_cog_event_metrics_policy convenience function."""
        module = _make_context(fs_importer, "CogMetricsPolicyTest7")
        policy = cog_metrics_policy.get_cog_event_metrics_policy(module.context)
        assert policy is not None
        assert policy.name == "CogEventMetricsPolicy"

    def test_get_cog_telemetry_metrics_policy_convenience(self, fs_importer: FilesystemImporter) -> None:
        """Test the get_cog_telemetry_metrics_policy convenience function."""
        module = _make_context(fs_importer, "CogMetricsPolicyTest8")
        policy = cog_metrics_policy.get_cog_telemetry_metrics_policy(module.context)
        assert policy is not None
        assert policy.name == "CogTelemetryMetricsPolicy"

    def test_get_cog_event_metrics_policy_config_convenience(self, fs_importer: FilesystemImporter) -> None:
        """Test the get_cog_event_metrics_policy_config convenience function."""
        module = _make_context(fs_importer, "CogMetricsPolicyTest9")
        config_cls = cog_metrics_policy.get_cog_event_metrics_policy_config(module.context)
        assert config_cls is not None

    def test_get_cog_telemetry_metrics_policy_config_convenience(self, fs_importer: FilesystemImporter) -> None:
        """Test the get_cog_telemetry_metrics_policy_config convenience function."""
        module = _make_context(fs_importer, "CogMetricsPolicyTest10")
        config_cls = cog_metrics_policy.get_cog_telemetry_metrics_policy_config(module.context)
        assert config_cls is not None

    def test_policy_config_types_are_distinct(self, fs_importer: FilesystemImporter) -> None:
        """Test that event and telemetry policy config types are distinct."""
        module = _make_context(fs_importer, "CogMetricsPolicyTest11")
        entities = cog_metrics_policy.get_entities(module.context)
        assert entities.cog_event_metrics_policy_config is not entities.cog_telemetry_metrics_policy_config

    def test_policies_are_distinct(self, fs_importer: FilesystemImporter) -> None:
        """Test that event and telemetry policies are distinct."""
        module = _make_context(fs_importer, "CogMetricsPolicyTest12")
        entities = cog_metrics_policy.get_entities(module.context)
        assert entities.cog_event_metrics_policy is not entities.cog_telemetry_metrics_policy
