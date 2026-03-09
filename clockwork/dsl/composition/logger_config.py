# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to logger configurations.

Note: You only need to import this module if you need to *instantiate* these
schemas.  If you only use schema instances or need schema type annotations,
import logger_config_proto instead.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Final, final

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.composition import constants
from clockwork.dsl.ir import compiler, importer_registry, policy
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn
from typing_extensions import override

if TYPE_CHECKING:
    from clockwork.dsl.composition import logger_config_proto
    from clockwork.dsl.ir import node


@dataclass
class Entities:
    """Logger configuration types and policies."""

    channel_type: type[logger_config_proto.ChannelTypeEnum]
    message_encoding: type[logger_config_proto.MessageEncodingEnum]
    schema_encoding: type[logger_config_proto.SchemaEncodingEnum]
    log_type: type[logger_config_proto.LogTypeEnum]
    logged_channel_config: type[logger_config_proto.LoggedChannelConfig]
    log_writer_config: type[logger_config_proto.LogWriterConfig]
    channel_logging_policy: policy.PolicyClass
    log_reader_policy: policy.PolicyClass


@final
class Registry(Context):
    """Registry for logger configuration entities."""

    def __init__(self, name: str | None, entities: Entities) -> None:
        """Create a new logger config registry."""
        self.name = name
        self.entities = entities

    @override
    def import_from(self, other: Registry) -> None:
        """Merge another registry into this one."""
        if self.entities != other.entities:
            msg = f"Logger config registry has conflicting entities: {self.entities} vs {other.entities}\nWhen merging {other.name} into {self.name}"
            raise RuntimeError(msg)

    def get_entities(self) -> Entities:
        """Get all logger config entities, compiling modules if needed."""
        return self.entities


def _load_all_entities(compiler_context: CompilerContext) -> Entities:
    """Load all logger config entities by compiling the necessary modules."""
    importer_reg = compiler_context[importer_registry.IMPORTER_REGISTRY_KEY]
    if importer_reg.importer is None:
        msg = "No importer registered in compiler context"
        raise RuntimeError(msg)

    # Compile all modules
    ct_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/logging/channel_type.clk")),
        importer_reg.importer,
    )
    compiler_context.import_from(ct_module.context)

    me_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/logging/message_encoding.clk")),
        importer_reg.importer,
    )
    compiler_context.import_from(me_module.context)

    se_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/logging/schema_encoding.clk")),
        importer_reg.importer,
    )
    compiler_context.import_from(se_module.context)

    cp_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/logging/channel_policy.clk")),
        importer_reg.importer,
    )
    compiler_context.import_from(cp_module.context)

    lwc_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/logging/log_writer_config.clk")),
        importer_reg.importer,
    )
    compiler_context.import_from(lwc_module.context)

    # Extract all entities
    channel_type = tachyon_dyn.get_enum(compiler_context, ct_module, "ChannelType")[0]
    message_encoding = tachyon_dyn.get_enum(compiler_context, me_module, "MessageEncoding")[0]
    schema_encoding = tachyon_dyn.get_enum(compiler_context, se_module, "SchemaEncoding")[0]
    log_type = tachyon_dyn.get_enum(compiler_context, cp_module, "LogType")[0]

    logged_channel_config = tachyon_dyn.get_instantiation_dataclass(
        compiler_context,
        lwc_module,
        "LoggedChannelConfig",
        max_channel_name_size=constants.MAX_CHANNEL_NAME_SIZE,
        max_schema_name_size=511,
        max_schema_definition_size=30000,
    )[0]

    log_writer_config = tachyon_dyn.get_instantiation_dataclass(
        compiler_context,
        lwc_module,
        "LogWriterConfig",
        max_channel_name_size=constants.MAX_CHANNEL_NAME_SIZE,
        max_schema_name_size=511,
        max_schema_definition_size=30000,
        max_num_channels=2046,
    )[0]

    channel_logging_policy = _extract_policy(cp_module, "ChannelLoggingPolicy")
    log_reader_policy = _extract_policy(cp_module, "LogReaderPolicy")

    return Entities(
        channel_type=channel_type,
        message_encoding=message_encoding,
        schema_encoding=schema_encoding,
        log_type=log_type,
        logged_channel_config=logged_channel_config,
        log_writer_config=log_writer_config,
        channel_logging_policy=channel_logging_policy,
        log_reader_policy=log_reader_policy,
    )


def _extract_policy(module: node.Module, policy_name: str) -> policy.PolicyClass:
    """Extract a policy definition from a module."""
    policy_def = module.inner_scope.lookup(policy_name)
    if policy_def is None:
        msg = f"Logger policy '{policy_name}' not found in module {module.module_id}"
        raise RuntimeError(msg)
    if not isinstance(policy_def, policy.PolicyDef):
        msg = f"Entity '{policy_name}' in module {module.module_id} is not a PolicyDef"
        raise RuntimeError(msg)  # noqa: TRY004 (RuntimeError is correct for a compiler internal error)
    return policy_def.get_resolved()


class RegistryKey(ContextKey[Registry]):
    """CompilerContext Key for Logger Config Registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> Registry:
        """Create a default instance of a Logger Config Registry."""
        return Registry(compiler_context.name, _load_all_entities(compiler_context))


REGISTRY_KEY: Final = RegistryKey("LoggerConfigRegistryKey")


# Module-level convenience functions
def get_entities(compiler_context: CompilerContext) -> Entities:
    """Get all logger config entities."""
    registry = compiler_context[REGISTRY_KEY]
    return registry.get_entities()


def get_channel_type_enum(compiler_context: CompilerContext) -> type[logger_config_proto.ChannelTypeEnum]:
    """Get ChannelType enum."""
    return get_entities(compiler_context).channel_type


def get_message_encoding_enum(compiler_context: CompilerContext) -> type[logger_config_proto.MessageEncodingEnum]:
    """Get MessageEncoding enum."""
    return get_entities(compiler_context).message_encoding


def get_schema_encoding_enum(compiler_context: CompilerContext) -> type[logger_config_proto.SchemaEncodingEnum]:
    """Get SchemaEncoding enum."""
    return get_entities(compiler_context).schema_encoding


def get_log_type_enum(compiler_context: CompilerContext) -> type[logger_config_proto.LogTypeEnum]:
    """Get LogType enum."""
    return get_entities(compiler_context).log_type


def get_logged_channel_config(compiler_context: CompilerContext) -> type[logger_config_proto.LoggedChannelConfig]:
    """Get LoggedChannelConfig dataclass."""
    return get_entities(compiler_context).logged_channel_config


def get_log_writer_config(compiler_context: CompilerContext) -> type[logger_config_proto.LogWriterConfig]:
    """Get LogWriterConfig dataclass."""
    return get_entities(compiler_context).log_writer_config


def get_channel_logging_policy(compiler_context: CompilerContext) -> policy.PolicyClass:
    """Get the ChannelLoggingPolicy definition."""
    return get_entities(compiler_context).channel_logging_policy


def get_log_reader_policy(compiler_context: CompilerContext) -> policy.PolicyClass:
    """Get the LogReaderPolicy definition."""
    return get_entities(compiler_context).log_reader_policy
