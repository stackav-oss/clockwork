# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to cog metrics policy configurations.

Note: You only need to import this module if you need to *instantiate* these
schemas.  If you only use schema instances or need schema type annotations,
import cog_metrics_policy_proto instead.
"""

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


@dataclass
class Entities:
    """Cog metrics policy configuration types and policies."""

    cog_event_metrics_policy_config: type[cog_metrics_policy_proto.CogEventMetricsPolicyConfig]
    cog_event_metrics_policy: policy.PolicyClass
    cog_telemetry_metrics_policy_config: type[cog_metrics_policy_proto.CogTelemetryMetricsPolicyConfig]
    cog_telemetry_metrics_policy: policy.PolicyClass


@final
class Registry(Context):
    """Registry for cog metrics policy configuration entities."""

    def __init__(self, name: str | None, entities: Entities) -> None:
        """Create a new cog metrics policy config registry."""
        self.name = name
        self.entities = entities

    @override
    def import_from(self, other: Registry) -> None:
        """Merge another registry into this one."""
        if self.entities != other.entities:
            msg = f"Cog metrics policy config registry has conflicting entities: {self.entities} vs {other.entities}\nWhen merging {other.name} into {self.name}"
            raise RuntimeError(msg)

    def get_entities(self) -> Entities:
        """Get all cog metrics policy config entities."""
        return self.entities


def _load_all_entities(compiler_context: CompilerContext) -> Entities:
    """Load all cog metrics policy config entities by compiling the necessary modules."""
    module = load_policy_module(compiler_context, Path("std/cog_metrics_policy.clk"))
    policy_context = module.context

    cog_event_metrics_policy_config = tachyon_dyn.get_instantiation_dataclass(
        policy_context,
        module,
        "CogEventMetricsPolicyConfig",
    )[0]
    cog_event_metrics_policy = extract_policy_class(module, "CogEventMetricsPolicy")

    cog_telemetry_metrics_policy_config = tachyon_dyn.get_instantiation_dataclass(
        policy_context,
        module,
        "CogTelemetryMetricsPolicyConfig",
    )[0]
    cog_telemetry_metrics_policy = extract_policy_class(module, "CogTelemetryMetricsPolicy")

    return Entities(
        cog_event_metrics_policy_config=cog_event_metrics_policy_config,
        cog_event_metrics_policy=cog_event_metrics_policy,
        cog_telemetry_metrics_policy_config=cog_telemetry_metrics_policy_config,
        cog_telemetry_metrics_policy=cog_telemetry_metrics_policy,
    )


class RegistryKey(ContextKey[Registry]):
    """CompilerContext Key for Cog Metrics Policy Config Registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> Registry:
        """Create a default instance of a Cog Metrics Policy Config Registry."""
        return Registry(compiler_context.name, _load_all_entities(compiler_context))


REGISTRY_KEY: Final = RegistryKey("CogMetricsPolicyConfigRegistryKey")


def get_entities(compiler_context: CompilerContext) -> Entities:
    """Get all cog metrics policy config entities."""
    registry = compiler_context[REGISTRY_KEY]
    return registry.get_entities()


def get_cog_event_metrics_policy_config(
    compiler_context: CompilerContext,
) -> type[cog_metrics_policy_proto.CogEventMetricsPolicyConfig]:
    """Get CogEventMetricsPolicyConfig dataclass."""
    return get_entities(compiler_context).cog_event_metrics_policy_config


def get_cog_event_metrics_policy(compiler_context: CompilerContext) -> policy.PolicyClass:
    """Get the CogEventMetricsPolicy definition."""
    return get_entities(compiler_context).cog_event_metrics_policy


def get_cog_telemetry_metrics_policy_config(
    compiler_context: CompilerContext,
) -> type[cog_metrics_policy_proto.CogTelemetryMetricsPolicyConfig]:
    """Get CogTelemetryMetricsPolicyConfig dataclass."""
    return get_entities(compiler_context).cog_telemetry_metrics_policy_config


def get_cog_telemetry_metrics_policy(compiler_context: CompilerContext) -> policy.PolicyClass:
    """Get the CogTelemetryMetricsPolicy definition."""
    return get_entities(compiler_context).cog_telemetry_metrics_policy
