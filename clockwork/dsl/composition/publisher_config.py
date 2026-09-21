# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to channel publisher configurations.

Note: You only need to import this module if you need to *instantiate* these
schemas.  If you only use schema instances or need schema type annotations,
import publisher_config_proto instead.
"""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING, Final
from uuid import UUID

from clockwork.dsl.composition import constants, publisher_config_proto
from clockwork.dsl.ir import compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.serialization import tachyon_reg
from clockwork.serialization.metadata import tachyon as tachyon_metadata
from clockwork.serialization.py import tachyon_dyn

if TYPE_CHECKING:
    from clockwork.dsl.compiler_context import CompilerContext
    from clockwork.dsl.ir import representation

_fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)

CPC_MODULE: Final = compiler.compile_source_file(
    ModuleID.from_path(CLK_REPO, Path("clockwork/logging/channel_publisher_config.clk")),
    _fs_importer,
)


PublishedChannelConfig: Final[type[publisher_config_proto.PublishedChannelConfig]] = (
    tachyon_dyn.get_instantiation_dataclass(
        CPC_MODULE.context,
        CPC_MODULE,
        "PublishedChannelConfig",
        max_channel_name_size=constants.MAX_CHANNEL_NAME_SIZE,
        max_schema_definition_size=constants.MAX_SCHEMA_DEFINITION_SIZE,
        max_module_name_size=31,
        max_path_name_size=511,
        max_class_name_size=63,
    )[0]
)

ChannelPublisherConfig: Final[type[publisher_config_proto.ChannelPublisherConfig]] = (
    tachyon_dyn.get_instantiation_dataclass(
        CPC_MODULE.context,
        CPC_MODULE,
        "ChannelPublisherConfig",
        max_channel_name_size=constants.MAX_CHANNEL_NAME_SIZE,
        max_schema_definition_size=constants.MAX_SCHEMA_DEFINITION_SIZE,
        max_module_name_size=31,
        max_path_name_size=511,
        max_class_name_size=63,
        max_num_channels=555,
    )[0]
)


def make_unbuffered_published_channel_config(
    context: CompilerContext,
    channel_name: str,
    message_repr: representation.ResolvedReprInstantiation,
) -> publisher_config_proto.PublishedChannelConfig:
    """Create publisher metadata for a channel without an allocated Pinion buffer."""
    if message_repr.typespec.instantiates.value_key() != "::Tachyon":
        msg = f"Unbuffered publisher channel is not Tachyon: {channel_name}"
        raise ValueError(msg)
    message_schema = message_repr.get_schema()
    constraint = tachyon_reg.constraint_for_type(context, message_schema)
    if constraint is None:
        msg = f"Missing Tachyon constraint for channel: {channel_name}"
        raise RuntimeError(msg)
    return PublishedChannelConfig(
        uuid=UUID(int=0),
        num_slots=0,
        message_size=constraint.size,
        channel_name=channel_name,
        schema_definition=list(tachyon_metadata.get_serialized_metadata(context, message_schema)),
        module_name=message_repr.schema_ir.schema.module.module_id.repo,
        source_file_name=str(message_repr.schema_ir.schema.module.module_id.get_base_path()),
        class_name=message_repr.schema_ir.schema_name,
        is_published_once=False,
    )
