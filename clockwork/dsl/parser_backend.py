# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Clockwork parser backend selection."""

import os
from typing import Final, Literal

_BACKEND_ENV_VAR: Final = "CLOCKWORK_PARSER_BACKEND"
_PYTHON_BACKEND: Final = "python"
_RUST_BACKEND: Final = "rust"

ParserBackend = Literal["python", "rust"]


def selected_backend() -> ParserBackend:
    """Return the selected Clockwork parser backend."""
    backend = os.environ.get(_BACKEND_ENV_VAR, _RUST_BACKEND).lower()
    if backend == _PYTHON_BACKEND:
        return _PYTHON_BACKEND
    if backend == _RUST_BACKEND:
        return _RUST_BACKEND
    msg = f"Unsupported {_BACKEND_ENV_VAR} value {backend!r}; expected 'python' or 'rust'."
    raise ValueError(msg)


def use_rust_parser() -> bool:
    """Return true when Clockwork should use the Rust FLTK parser backend."""
    return selected_backend() == _RUST_BACKEND
