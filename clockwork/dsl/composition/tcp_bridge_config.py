# Copyright 2025 Stack AV Co.
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
    from clockwork.dsl.composition import tcp_bridge_config_proto

_MODULE: Final = compiler.compile_source_file(
    ModuleID.from_path(CLK_REPO, Path("clockwork/pinion/tcp_bridge_config.clk")),
    FilesystemImporter(compile_fn=compiler.compile_source_file),
)


TcpBridgeClientConfig: Final[type[tcp_bridge_config_proto.TcpBridgeClientConfig]] = (
    tachyon_dyn.get_instantiation_dataclass(_MODULE.context, _MODULE, "TcpBridgeClientConfig")[0]
)


TcpBridgeServerConfig: Final[type[tcp_bridge_config_proto.TcpBridgeServerConfig]] = (
    tachyon_dyn.get_instantiation_dataclass(_MODULE.context, _MODULE, "TcpBridgeServerConfig")[0]
)


TcpBridgeConfig: Final[type[tcp_bridge_config_proto.TcpBridgeConfig]] = tachyon_dyn.get_instantiation_dataclass(
    _MODULE.context, _MODULE, "TcpBridgeConfig", max_bridge_clients=256, max_bridge_servers=256
)[0]
