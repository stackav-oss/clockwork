# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to TCP Bridge Config schemas.

Note: You only need to import this module if you need to *instantiate* these
schemas.  If you only use schema instances or need schema type annotations,
import tcp_bridge_config_proto instead.
"""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING, Final

from clockwork.dsl.ir import compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn

if TYPE_CHECKING:
    from clockwork.dsl.composition import system_metadata_proto

_MODULE: Final = compiler.compile_source_file(
    ModuleID.from_path(CLK_REPO, Path("clockwork/common/system_metadata.clk")),
    FilesystemImporter(compile_fn=compiler.compile_source_file),
)


SystemMetadata: Final[type[system_metadata_proto.SystemMetadata]] = tachyon_dyn.get_instantiation_dataclass(
    _MODULE.context, _MODULE, "SystemMetadata"
)[0]
