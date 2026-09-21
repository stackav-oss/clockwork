# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportMissingImports=false

"""Selected Clockwork CST backend."""

from clockwork.dsl import parser_backend

if parser_backend.use_rust_parser():
    from clockwork_native.cst import *  # noqa: F403 Generated targets
else:
    from clockwork.dsl.clockwork_py_cst import *  # pyrefly: ignore[missing-import] # noqa: F403 Generated targets
