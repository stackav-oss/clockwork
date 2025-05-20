# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to TCP Bridge Configuration schemas."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from clockwork.serialization.py.protocol import Tachyon

if TYPE_CHECKING:
    from uuid import UUID

    from clockwork.dsl.composition import pdfproto
    from clockwork.dsl.composition.platform_diagnostics_config_proto import (
        PlatformDiagnosticsConfig,
    )


@dataclass(kw_only=True)
class TcpBridgeClientConfig(Tachyon["TcpBridgeClientConfig"]):
    """Describes the configuration of a TCP bridge client."""

    publisher_endpoint: pdfproto.PublishEndpoint
    server_address: str
    server_port: int
    channel_name: str


@dataclass(kw_only=True)
class TcpBridgeServerConfig(Tachyon["TcpBridgeServerConfig"]):
    """Describes the configuration of a TCP bridge server."""

    publisher_id: UUID
    buffer_layout: pdfproto.PinionBufferLayout
    listen_address: str
    listen_port: int
    num_clients: int
    channel_name: str


@dataclass(kw_only=True)
class TcpBridgeConfig(Tachyon["TcpBridgeConfig"]):
    """Describes of the TCP bridge clients and servers that should be running on a node."""

    bridge_clients: list[TcpBridgeClientConfig]
    bridge_servers: list[TcpBridgeServerConfig]
    diagnostics_config: PlatformDiagnosticsConfig
    host_name: str
    status_publish_endpoint: pdfproto.PublishEndpoint
