# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to channel spy configurations.

Note: You only need to import this module if you need to *instantiate* these
schemas.  If you only use schema instances or need schema type annotations,
import channel_spy_config_proto instead.
"""

from pathlib import Path
from typing import Final

from clockwork.dsl.composition import channel_spy_config_proto, constants
from clockwork.dsl.ir import compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn

_fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)

CSC_MODULE: Final = compiler.compile_source_file(
    ModuleID.from_path(CLK_REPO, Path("clockwork/tools/channel_spy/channel_spy_config.clk")),
    _fs_importer,
)


PublishedChannelMetadata: Final[type[channel_spy_config_proto.PublishedChannelMetadata]] = (
    tachyon_dyn.get_instantiation_dataclass(
        CSC_MODULE.context,
        CSC_MODULE,
        "PublishedChannelMetadata",
        max_channel_name_size=constants.MAX_CHANNEL_NAME_SIZE,
        max_schema_name_size=511,
        max_schema_definition_size=constants.MAX_SCHEMA_DEFINITION_SIZE,
    )[0]
)


ChannelSpyConfig: Final[type[channel_spy_config_proto.ChannelSpyConfig]] = tachyon_dyn.get_instantiation_dataclass(
    CSC_MODULE.context,
    CSC_MODULE,
    "ChannelSpyConfig",
    max_channel_name_size=constants.MAX_CHANNEL_NAME_SIZE,
    max_schema_name_size=511,
    max_schema_definition_size=constants.MAX_SCHEMA_DEFINITION_SIZE,
    max_num_channels=2048,
)[0]
