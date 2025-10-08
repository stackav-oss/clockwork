# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to channel publisher configurations.

Note: You only need to import this module if you need to *instantiate* these
schemas.  If you only use schema instances or need schema type annotations,
import publisher_config_proto instead.
"""

from pathlib import Path
from typing import Final

from clockwork.dsl.composition import publisher_config_proto
from clockwork.dsl.ir import compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn

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
        max_channel_name_size=300,
        max_schema_definition_size=20000,
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
        max_channel_name_size=300,
        max_schema_definition_size=20000,
        max_module_name_size=31,
        max_path_name_size=511,
        max_class_name_size=63,
        max_num_channels=555,
    )[0]
)
