# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to the journal topology file configuration schema."""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING, Final

from clockwork.dsl.ir import compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn

if TYPE_CHECKING:
    from clockwork.dsl.composition import journal_topology_file_config_proto

_MODULE: Final = compiler.compile_source_file(
    ModuleID.from_path(CLK_REPO, Path("clockwork/journal/journal_topology_file_config.clk")),
    FilesystemImporter(compile_fn=compiler.compile_source_file),
)

JournalTopologyFileConfig: Final[type[journal_topology_file_config_proto.JournalTopologyFileConfig]] = (
    tachyon_dyn.get_instantiation_dataclass(_MODULE.context, _MODULE, "JournalTopologyFileConfig")[0]
)
