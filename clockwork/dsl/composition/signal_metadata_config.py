# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to signal metadata configurations.

Note: You only need to import this module if you need to *instantiate* these
schemas. If you only use schema instances or need schema type annotations,
import signal_metadata_config_proto instead.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Final, final

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.composition import constants
from clockwork.dsl.ir import importer_registry
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn
from typing_extensions import override

if TYPE_CHECKING:
    from clockwork.dsl.composition import signal_metadata_config_proto


@dataclass
class Entities:
    """Signal metadata configuration types."""

    aggregation_type: type[signal_metadata_config_proto.AggregationTypeEnum]
    log_type: type[signal_metadata_config_proto.LogTypeEnum]
    report_group_type: type[signal_metadata_config_proto.ReportGroupTypeEnum]
    signal_metadata: type[signal_metadata_config_proto.SignalMetadata]
    report_group_signal_metadata: type[signal_metadata_config_proto.ReportGroupSignalMetadata]
    report_group_metadata: type[signal_metadata_config_proto.ReportGroupMetadata]
    cog_report_groups_metadata: type[signal_metadata_config_proto.CogReportGroupsMetadata]
    signal_instance_metadata: type[signal_metadata_config_proto.SignalInstanceMetadata]
    report_group_instance_metadata: type[signal_metadata_config_proto.ReportGroupInstanceMetadata]
    cog_instance_metadata: type[signal_metadata_config_proto.CogInstanceMetadata]
    report_group_channel_metadata: type[signal_metadata_config_proto.ReportGroupChannelMetadata]
    signal_metadata_config: type[signal_metadata_config_proto.SignalMetadataConfig]


@final
class Registry(Context):
    """Registry for signal metadata configuration entities."""

    def __init__(self, name: str | None, entities: Entities) -> None:
        """Create a new signal metadata config registry."""
        self.name = name
        self.entities = entities

    @override
    def import_from(self, other: Registry) -> None:
        """Merge another registry into this one."""
        if self.entities != other.entities:
            msg = f"Signal metadata config registry has conflicting entities: {self.entities} vs {other.entities}\nWhen merging {other.name} into {self.name}"
            raise RuntimeError(msg)

    def get_entities(self) -> Entities:
        """Get all signal metadata config entities."""
        return self.entities


def _load_all_entities(compiler_context: CompilerContext) -> Entities:
    """Load all signal metadata config entities by compiling the necessary modules."""
    importer_reg = compiler_context[importer_registry.IMPORTER_REGISTRY_KEY]
    if importer_reg.importer is None:
        msg = "No importer registered in compiler context"
        raise RuntimeError(msg)

    importer = importer_reg.importer
    if not isinstance(importer, FilesystemImporter):
        msg = f"Expected FilesystemImporter, got {type(importer).__name__}"
        raise TypeError(msg)

    module_id = ModuleID.from_path(CLK_REPO, Path("clockwork/common/signal_metadata_config.clk"))
    # We do not import and use the compiler directly to avoid circular dependencies.
    module = importer.compile_fn(module_id, importer)
    compiler_context.import_from(module.context)

    # Extract enum entities
    aggregation_type = tachyon_dyn.get_enum(compiler_context, module, "AggregationType")[0]
    log_type = tachyon_dyn.get_enum(compiler_context, module, "LogType")[0]
    report_group_type = tachyon_dyn.get_enum(compiler_context, module, "ReportGroupType")[0]

    # Extract dataclass entities
    signal_metadata = tachyon_dyn.get_instantiation_dataclass(
        compiler_context,
        module,
        "SignalMetadata",
        max_signal_name_size=300,
        max_aggregation_definition_size=80,
        max_pre_aggregation_types=10,
    )[0]
    report_group_signal_metadata = tachyon_dyn.get_instantiation_dataclass(
        compiler_context,
        module,
        "ReportGroupSignalMetadata",
        max_alias_size=100,
        max_post_aggregation_types=10,
    )[0]
    report_group_metadata = tachyon_dyn.get_instantiation_dataclass(
        compiler_context,
        module,
        "ReportGroupMetadata",
        max_report_group_name_size=300,
        max_num_signals=50,
    )[0]
    cog_report_groups_metadata = tachyon_dyn.get_instantiation_dataclass(
        compiler_context,
        module,
        "CogReportGroupsMetadata",
        max_num_report_groups=10,
    )[0]
    signal_instance_metadata = tachyon_dyn.get_instantiation_dataclass(
        compiler_context,
        module,
        "SignalInstanceMetadata",
    )[0]
    report_group_instance_metadata = tachyon_dyn.get_instantiation_dataclass(
        compiler_context,
        module,
        "ReportGroupInstanceMetadata",
        max_channel_name_size=constants.MAX_CHANNEL_NAME_SIZE,
        max_num_signal_instances=50,
    )[0]
    cog_instance_metadata = tachyon_dyn.get_instantiation_dataclass(
        compiler_context,
        module,
        "CogInstanceMetadata",
        max_num_report_group_instances=10,
    )[0]
    report_group_channel_metadata = tachyon_dyn.get_instantiation_dataclass(
        compiler_context,
        module,
        "ReportGroupChannelMetadata",
        max_channel_name_size=constants.MAX_CHANNEL_NAME_SIZE,
    )[0]
    signal_metadata_config = tachyon_dyn.get_instantiation_dataclass(
        compiler_context,
        module,
        "SignalMetadataConfig",
        max_num_signals=2046,
        max_num_cogs=2046,
        max_num_cog_instances=2046,
        max_num_report_group_channels=2046,
    )[0]

    return Entities(
        aggregation_type=aggregation_type,
        log_type=log_type,
        report_group_type=report_group_type,
        signal_metadata=signal_metadata,
        report_group_signal_metadata=report_group_signal_metadata,
        report_group_metadata=report_group_metadata,
        cog_report_groups_metadata=cog_report_groups_metadata,
        signal_instance_metadata=signal_instance_metadata,
        report_group_instance_metadata=report_group_instance_metadata,
        cog_instance_metadata=cog_instance_metadata,
        report_group_channel_metadata=report_group_channel_metadata,
        signal_metadata_config=signal_metadata_config,
    )


class RegistryKey(ContextKey[Registry]):
    """CompilerContext Key for Signal Metadata Config Registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> Registry:
        """Create a default instance of a Signal Metadata Config Registry."""
        return Registry(compiler_context.name, _load_all_entities(compiler_context))


REGISTRY_KEY: Final = RegistryKey("SignalMetadataConfigRegistryKey")


def get_entities(compiler_context: CompilerContext) -> Entities:
    """Get all signal metadata config entities."""
    registry = compiler_context[REGISTRY_KEY]
    return registry.get_entities()


def get_aggregation_type_enum(
    compiler_context: CompilerContext,
) -> type[signal_metadata_config_proto.AggregationTypeEnum]:
    """Get AggregationType enum."""
    return get_entities(compiler_context).aggregation_type


def get_log_type_enum(
    compiler_context: CompilerContext,
) -> type[signal_metadata_config_proto.LogTypeEnum]:
    """Get LogType enum."""
    return get_entities(compiler_context).log_type


def get_report_group_type_enum(
    compiler_context: CompilerContext,
) -> type[signal_metadata_config_proto.ReportGroupTypeEnum]:
    """Get ReportGroupType enum."""
    return get_entities(compiler_context).report_group_type


def get_signal_metadata(
    compiler_context: CompilerContext,
) -> type[signal_metadata_config_proto.SignalMetadata]:
    """Get SignalMetadata dataclass."""
    return get_entities(compiler_context).signal_metadata


def get_report_group_signal_metadata(
    compiler_context: CompilerContext,
) -> type[signal_metadata_config_proto.ReportGroupSignalMetadata]:
    """Get ReportGroupSignalMetadata dataclass."""
    return get_entities(compiler_context).report_group_signal_metadata


def get_report_group_metadata(
    compiler_context: CompilerContext,
) -> type[signal_metadata_config_proto.ReportGroupMetadata]:
    """Get ReportGroupMetadata dataclass."""
    return get_entities(compiler_context).report_group_metadata


def get_cog_report_groups_metadata(
    compiler_context: CompilerContext,
) -> type[signal_metadata_config_proto.CogReportGroupsMetadata]:
    """Get CogReportGroupsMetadata dataclass."""
    return get_entities(compiler_context).cog_report_groups_metadata


def get_signal_instance_metadata(
    compiler_context: CompilerContext,
) -> type[signal_metadata_config_proto.SignalInstanceMetadata]:
    """Get SignalInstanceMetadata dataclass."""
    return get_entities(compiler_context).signal_instance_metadata


def get_report_group_instance_metadata(
    compiler_context: CompilerContext,
) -> type[signal_metadata_config_proto.ReportGroupInstanceMetadata]:
    """Get ReportGroupInstanceMetadata dataclass."""
    return get_entities(compiler_context).report_group_instance_metadata


def get_cog_instance_metadata(
    compiler_context: CompilerContext,
) -> type[signal_metadata_config_proto.CogInstanceMetadata]:
    """Get CogInstanceMetadata dataclass."""
    return get_entities(compiler_context).cog_instance_metadata


def get_report_group_channel_metadata(
    compiler_context: CompilerContext,
) -> type[signal_metadata_config_proto.ReportGroupChannelMetadata]:
    """Get ReportGroupChannelMetadata dataclass."""
    return get_entities(compiler_context).report_group_channel_metadata


def get_signal_metadata_config(
    compiler_context: CompilerContext,
) -> type[signal_metadata_config_proto.SignalMetadataConfig]:
    """Get SignalMetadataConfig dataclass."""
    return get_entities(compiler_context).signal_metadata_config
