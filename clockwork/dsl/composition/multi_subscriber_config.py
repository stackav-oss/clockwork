# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to configurations for subscribers to multi-publisher channels.

Note: You only need to import this module if you need to *instantiate* these
schemas.  If you only use schema instances or need schema type annotations,
import multi_subscriber_config_proto instead.
"""

from pathlib import Path
from typing import Final

from clockwork.dsl.composition import multi_subscriber_config_proto
from clockwork.dsl.ir import compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn

_fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)

MSC_MODULE: Final = compiler.compile_source_file(
    ModuleID.from_path(CLK_REPO, Path("clockwork/common/multi_subscriber_config.clk")),
    _fs_importer,
)

MultiSubscriberConfig: Final[type[multi_subscriber_config_proto.MultiSubscriberConfig]] = (
    tachyon_dyn.get_instantiation_dataclass(
        MSC_MODULE.context,
        MSC_MODULE,
        "MultiSubscriberConfig",
        max_publishers=256,
    )[0]
)
