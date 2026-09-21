# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to TCP Bridge Configuration schemas."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from clockwork.serialization.py.protocol import Tachyon

if TYPE_CHECKING:
    from clockwork.dsl.composition import pdfproto
    from clockwork.dsl.composition.platform_diagnostics_config_proto import (
        PlatformDiagnosticsConfig,
    )


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class SimplelaunchRunnerConfig(Tachyon["SimplelaunchRunnerConfig"]):
# fmt: on
    """Describes the Simplelaunch runner configuration for a node."""

    host_name: str
    diagnostics_config: PlatformDiagnosticsConfig
    status_publish_endpoint: pdfproto.PublishEndpoint
