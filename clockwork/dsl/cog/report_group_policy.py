# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to report group policy configurations.

Note: You only need to import this module if you need to *instantiate* these
schemas.  If you only use schema instances or need schema type annotations,
import report_group_policy_proto instead.
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
    from clockwork.dsl.cog import report_group_policy_proto
    from clockwork.dsl.ir import policy


@dataclass
class Entities:
    """Report group policy configuration types and policies."""

    reporting_strategy: type[report_group_policy_proto.ReportingStrategyEnum]
    report_group_log_type: type[report_group_policy_proto.ReportGroupLogTypeEnum]
    report_group_policy_config: type[report_group_policy_proto.ReportGroupPolicyConfig]
    report_group_policy: policy.PolicyClass


@final
class Registry(Context):
    """Registry for report group policy configuration entities."""

    def __init__(self, name: str | None, entities: Entities) -> None:
        """Create a new report group policy config registry."""
        self.name = name
        self.entities = entities

    @override
    def import_from(self, other: Registry) -> None:
        """Merge another registry into this one."""
        if self.entities != other.entities:
            msg = f"Report group policy config registry has conflicting entities: {self.entities} vs {other.entities}\nWhen merging {other.name} into {self.name}"
            raise RuntimeError(msg)

    def get_entities(self) -> Entities:
        """Get all report group policy config entities."""
        return self.entities


def _load_all_entities(compiler_context: CompilerContext) -> Entities:
    """Load all report group policy config entities by compiling the necessary modules."""
    module = load_policy_module(compiler_context, Path("std/signals.clk"))
    policy_context = module.context

    reporting_strategy = tachyon_dyn.get_enum(policy_context, module, "ReportingStrategy")[0]
    report_group_log_type = tachyon_dyn.get_enum(policy_context, module, "ReportGroupLogType")[0]
    report_group_policy_config = tachyon_dyn.get_instantiation_dataclass(
        policy_context,
        module,
        "ReportGroupPolicyConfig",
    )[0]
    report_group_policy = extract_policy_class(module, "ReportGroupPolicy")

    return Entities(
        reporting_strategy=reporting_strategy,
        report_group_log_type=report_group_log_type,
        report_group_policy_config=report_group_policy_config,
        report_group_policy=report_group_policy,
    )


class RegistryKey(ContextKey[Registry]):
    """CompilerContext Key for Report Group Policy Config Registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> Registry:
        """Create a default instance of a Report Group Policy Config Registry."""
        return Registry(compiler_context.name, _load_all_entities(compiler_context))


REGISTRY_KEY: Final = RegistryKey("ReportGroupPolicyConfigRegistryKey")


def get_entities(compiler_context: CompilerContext) -> Entities:
    """Get all report group policy config entities."""
    registry = compiler_context[REGISTRY_KEY]
    return registry.get_entities()


def get_reporting_strategy_enum(
    compiler_context: CompilerContext,
) -> type[report_group_policy_proto.ReportingStrategyEnum]:
    """Get ReportingStrategy enum."""
    return get_entities(compiler_context).reporting_strategy


def get_report_group_log_type_enum(
    compiler_context: CompilerContext,
) -> type[report_group_policy_proto.ReportGroupLogTypeEnum]:
    """Get ReportGroupLogType enum."""
    return get_entities(compiler_context).report_group_log_type


def get_report_group_policy_config(
    compiler_context: CompilerContext,
) -> type[report_group_policy_proto.ReportGroupPolicyConfig]:
    """Get ReportGroupPolicyConfig dataclass."""
    return get_entities(compiler_context).report_group_policy_config


def get_report_group_policy(compiler_context: CompilerContext) -> policy.PolicyClass:
    """Get the ReportGroupPolicy definition."""
    return get_entities(compiler_context).report_group_policy
