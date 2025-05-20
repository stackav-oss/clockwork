# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to logger configurations.

Note: You only need to import this module if you need to *instantiate* these
schemas.  If you only use schema instances or need schema type annotations,
import logger_config_proto instead.
"""

from pathlib import Path
from typing import Final

from clockwork.dsl.composition import logger_config_proto
from clockwork.dsl.ir import compiler, policy
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn

_fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)

CT_MODULE: Final = compiler.compile_source_file(
    ModuleID.from_path(CLK_REPO, Path("clockwork/logging/channel_type.clk")),
    _fs_importer,
)

ChannelType: Final[type[logger_config_proto.ChannelTypeEnum]] = tachyon_dyn.get_enum(
    CT_MODULE.context, CT_MODULE, "ChannelType"
)[0]

ME_MODULE: Final = compiler.compile_source_file(
    ModuleID.from_path(CLK_REPO, Path("clockwork/logging/message_encoding.clk")),
    _fs_importer,
)


MessageEncoding: Final[type[logger_config_proto.MessageEncodingEnum]] = tachyon_dyn.get_enum(
    ME_MODULE.context, ME_MODULE, "MessageEncoding"
)[0]

SE_MODULE: Final = compiler.compile_source_file(
    ModuleID.from_path(CLK_REPO, Path("clockwork/logging/schema_encoding.clk")), _fs_importer
)

SchemaEncoding: Final[type[logger_config_proto.SchemaEncodingEnum]] = tachyon_dyn.get_enum(
    SE_MODULE.context, SE_MODULE, "SchemaEncoding"
)[0]

CP_MODULE: Final = compiler.compile_source_file(
    ModuleID.from_path(CLK_REPO, Path("clockwork/logging/channel_policy.clk")), _fs_importer
)


LogType: Final[type[logger_config_proto.LogTypeEnum]] = tachyon_dyn.get_enum(CP_MODULE.context, CP_MODULE, "LogType")[0]

LWC_MODULE: Final = compiler.compile_source_file(
    ModuleID.from_path(CLK_REPO, Path("clockwork/logging/log_writer_config.clk")),
    _fs_importer,
)


LoggedChannelConfig: Final[type[logger_config_proto.LoggedChannelConfig]] = tachyon_dyn.get_instantiation_dataclass(
    LWC_MODULE.context,
    LWC_MODULE,
    "LoggedChannelConfig",
    max_channel_name_size=127,
    max_schema_name_size=511,
    max_schema_definition_size=16383,
)[0]

LogWriterConfig: Final[type[logger_config_proto.LogWriterConfig]] = tachyon_dyn.get_instantiation_dataclass(
    LWC_MODULE.context,
    LWC_MODULE,
    "LogWriterConfig",
    max_channel_name_size=127,
    max_schema_name_size=511,
    max_schema_definition_size=16383,
    max_num_channels=555,
)[0]


def get_channel_logging_policy() -> policy.PolicyClass:
    """Retrieve the global ChannelLoggingPolicy definition."""
    channel_logging_policy_def = CP_MODULE.inner_scope.lookup("ChannelLoggingPolicy")
    assert isinstance(channel_logging_policy_def, policy.PolicyDef)  # noqa: S101 (invariant)
    return channel_logging_policy_def.get_resolved()


def get_log_reader_policy() -> policy.PolicyClass:
    """Retrieve the global LogReaderPolicy definition."""
    log_reader_policy_def = CP_MODULE.inner_scope.lookup("LogReaderPolicy")
    assert isinstance(log_reader_policy_def, policy.PolicyDef)  # noqa: S101 (invariant)
    return log_reader_policy_def.get_resolved()
