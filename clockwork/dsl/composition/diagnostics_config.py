# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to configuration for diagnostics database.

Note: You only need to import this module if you need to *instantiate* these
schemas.  If you only use schema instances or need schema type annotations,
import diagnostics_config_proto instead.
"""

from pathlib import Path
from typing import Final

from clockwork.dsl.composition import diagnostics_config_proto
from clockwork.dsl.ir import compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn

_fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)

CLK_MODULE: Final = compiler.compile_source_file(
    ModuleID.from_path(CLK_REPO, Path("clockwork/diagnostics/database_config.clk")),
    _fs_importer,
)

max_reporter_name = 512

max_reporters = 400

ReporterInfo: Final[type[diagnostics_config_proto.ReporterInfo]] = tachyon_dyn.get_instantiation_dataclass(
    CLK_MODULE.context,
    CLK_MODULE,
    "ReporterInfo",
)[0]

DatabaseInfo: Final[type[diagnostics_config_proto.DatabaseInfo]] = tachyon_dyn.get_instantiation_dataclass(
    CLK_MODULE.context,
    CLK_MODULE,
    "DatabaseInfo",
)[0]
