# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to TCP Bridge Configuration schemas."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from clockwork.serialization.py.protocol import Tachyon

if TYPE_CHECKING:
    from uuid import UUID

    from clockwork.dsl.composition import logger_config_proto, pdfproto
    from clockwork.dsl.composition.channel_config_proto import ChannelType
    from clockwork.dsl.composition.platform_diagnostics_config_proto import (
        PlatformDiagnosticsConfig,
    )


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class TcpBridgeClientConfig(Tachyon["TcpBridgeClientConfig"]):
# fmt: on
    """Describes the configuration of a TCP bridge client."""

    publisher_endpoint: pdfproto.PublishEndpoint
    server_address: str
    server_port: int
    publisher_keys: list[str]


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class TcpBridgeServerConfig(Tachyon["TcpBridgeServerConfig"]):
# fmt: on
    """Describes the configuration of a TCP bridge server."""

    publisher_id: UUID
    buffer_layout: pdfproto.PinionBufferLayout
    listen_address: str
    listen_port: int
    num_clients: int
    channel_name: str
    is_bulk_data: bool
    schema_encoding: logger_config_proto.SchemaEncoding
    schema_definition: list[int]
    channel_type: ChannelType
    subscriber_key: str


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class TcpBridgeConfig(Tachyon["TcpBridgeConfig"]):
# fmt: on
    """Describes of the TCP bridge clients and servers that should be running on a node."""

    bridge_clients: list[TcpBridgeClientConfig]
    bridge_servers: list[TcpBridgeServerConfig]
    diagnostics_config: PlatformDiagnosticsConfig
    host_name: str
    status_publish_endpoint: pdfproto.PublishEndpoint
