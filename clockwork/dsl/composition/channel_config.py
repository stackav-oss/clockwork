# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to configuration for channels.

Note: You only need to import this module if you need to *instantiate* these
schemas.  If you only use schema instances or need schema type annotations,
import channel_config_proto instead.
"""

from pathlib import Path
from typing import Final

from clockwork.dsl.composition import channel_config_proto
from clockwork.dsl.ir import compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn

_fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)

CLK_MODULE: Final = compiler.compile_source_file(
    ModuleID.from_path(CLK_REPO, Path("clockwork/pinion/channel_config.clk")),
    FilesystemImporter(compile_fn=compiler.compile_source_file),
)

ChannelType: Final[type[channel_config_proto.ChannelTypeEnum]] = tachyon_dyn.get_enum(
    CLK_MODULE.context, CLK_MODULE, "ChannelType"
)[0]
