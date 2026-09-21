# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to aligner metrics policy configurations."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Final, final

from clockwork.dsl.cog.metrics_policy_loader import extract_policy_class, load_policy_module
from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.serialization.py import tachyon_dyn
from typing_extensions import override

if TYPE_CHECKING:
    from clockwork.dsl.cog import cog_metrics_policy_proto
    from clockwork.dsl.ir import policy

_MODULE_PATH: Final = Path("std/aligner_metrics_policy.clk")


@dataclass
class Entities:
    """Aligner metrics policy configuration types and policies."""

    aligner_event_metrics_policy_config: type[cog_metrics_policy_proto.CogEventMetricsPolicyConfig]
    aligner_event_metrics_policy: policy.PolicyClass
    aligner_telemetry_metrics_policy_config: type[cog_metrics_policy_proto.CogTelemetryMetricsPolicyConfig]
    aligner_telemetry_metrics_policy: policy.PolicyClass


@final
class Registry(Context):
    """Registry for aligner metrics policy configuration entities."""

    def __init__(self, name: str | None, entities: Entities) -> None:
        """Create a new aligner metrics policy config registry."""
        self.name = name
        self.entities = entities

    @override
    def import_from(self, other: Registry) -> None:
        """Merge another registry into this one."""
        if self.entities != other.entities:
            msg = (
                f"Aligner metrics policy config registry has conflicting entities: "
                f"{self.entities} vs {other.entities}\nWhen merging {other.name} into {self.name}"
            )
            raise RuntimeError(msg)

    def get_entities(self) -> Entities:
        """Get all aligner metrics policy config entities."""
        return self.entities


def _load_all_entities(compiler_context: CompilerContext) -> Entities:
    """Load all aligner metrics policy config entities by compiling the policy module."""
    module = load_policy_module(compiler_context, _MODULE_PATH)
    policy_context = module.context

    aligner_event_metrics_policy_config = tachyon_dyn.get_instantiation_dataclass(
        policy_context,
        module,
        "CogEventMetricsPolicyConfig",
    )[0]
    aligner_event_metrics_policy = extract_policy_class(module, "AlignerEventMetricsPolicy")

    aligner_telemetry_metrics_policy_config = tachyon_dyn.get_instantiation_dataclass(
        policy_context,
        module,
        "CogTelemetryMetricsPolicyConfig",
    )[0]
    aligner_telemetry_metrics_policy = extract_policy_class(module, "AlignerTelemetryMetricsPolicy")

    return Entities(
        aligner_event_metrics_policy_config=aligner_event_metrics_policy_config,
        aligner_event_metrics_policy=aligner_event_metrics_policy,
        aligner_telemetry_metrics_policy_config=aligner_telemetry_metrics_policy_config,
        aligner_telemetry_metrics_policy=aligner_telemetry_metrics_policy,
    )


class RegistryKey(ContextKey[Registry]):
    """CompilerContext key for the Aligner Metrics Policy Config Registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> Registry:
        """Create a default instance of an Aligner Metrics Policy Config Registry."""
        return Registry(compiler_context.name, _load_all_entities(compiler_context))


REGISTRY_KEY: Final = RegistryKey("AlignerMetricsPolicyConfigRegistryKey")


def get_entities(compiler_context: CompilerContext) -> Entities:
    """Get all aligner metrics policy config entities."""
    return compiler_context[REGISTRY_KEY].get_entities()


def get_aligner_event_metrics_policy_config(
    compiler_context: CompilerContext,
) -> type[cog_metrics_policy_proto.CogEventMetricsPolicyConfig]:
    """Get the CogEventMetricsPolicyConfig dataclass (used by aligner event policy)."""
    return get_entities(compiler_context).aligner_event_metrics_policy_config


def get_aligner_event_metrics_policy(compiler_context: CompilerContext) -> policy.PolicyClass:
    """Get the AlignerEventMetricsPolicy definition."""
    return get_entities(compiler_context).aligner_event_metrics_policy


def get_aligner_telemetry_metrics_policy_config(
    compiler_context: CompilerContext,
) -> type[cog_metrics_policy_proto.CogTelemetryMetricsPolicyConfig]:
    """Get the CogTelemetryMetricsPolicyConfig dataclass (used by aligner telemetry policy)."""
    return get_entities(compiler_context).aligner_telemetry_metrics_policy_config


def get_aligner_telemetry_metrics_policy(compiler_context: CompilerContext) -> policy.PolicyClass:
    """Get the AlignerTelemetryMetricsPolicy definition."""
    return get_entities(compiler_context).aligner_telemetry_metrics_policy
