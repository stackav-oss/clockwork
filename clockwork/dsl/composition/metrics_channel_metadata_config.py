# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to metrics channel metadata configurations.

Note: You only need to import this module if you need to *instantiate* these
schemas.  If you only use schema instances or need schema type annotations,
import channel_spy_config_proto instead.
"""

from pathlib import Path
from typing import Final

from clockwork.dsl.composition import constants, metrics_channel_metadata_config_proto
from clockwork.dsl.ir import compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn

_fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)

CSC_MODULE: Final = compiler.compile_source_file(
    ModuleID.from_path(CLK_REPO, Path("clockwork/tools/metrics_channel_metadata/metrics_channel_metadata_config.clk")),
    _fs_importer,
)


MetricsChannelMetadata: Final[type[metrics_channel_metadata_config_proto.MetricsChannelMetadata]] = (
    tachyon_dyn.get_instantiation_dataclass(
        CSC_MODULE.context,
        CSC_MODULE,
        "MetricsChannelMetadata",
        max_channel_name_size=constants.MAX_CHANNEL_NAME_SIZE,
    )[0]
)

MetricsChannelMetadataReport: Final[type[metrics_channel_metadata_config_proto.MetricsChannelMetadataReport]] = (
    tachyon_dyn.get_instantiation_dataclass(
        CSC_MODULE.context,
        CSC_MODULE,
        "MetricsChannelMetadataReport",
        max_channel_name_size=constants.MAX_CHANNEL_NAME_SIZE,
        max_num_channels=2046,
    )[0]
)

MetricsChannelMetadataConfig: Final[type[metrics_channel_metadata_config_proto.MetricsChannelMetadataConfig]] = (
    tachyon_dyn.get_instantiation_dataclass(
        CSC_MODULE.context,
        CSC_MODULE,
        "MetricsChannelMetadataConfig",
        max_channel_name_size=constants.MAX_CHANNEL_NAME_SIZE,
        max_num_channels=2046,
        max_schema_name_size=511,
        max_schema_definition_size=constants.MAX_SCHEMA_DEFINITION_SIZE,
    )[0]
)
