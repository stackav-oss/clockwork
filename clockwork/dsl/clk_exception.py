# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Exception formatting utils for Clockwork tools."""

import logging
from types import TracebackType

from rich.console import Console
from rich.logging import RichHandler
from typing_extensions import override


class ClkExceptionFormatter(logging.Formatter):
    """Class for nicely formatting clockwork exceptions."""

    @override
    def formatException(
        self, ei: tuple[type[BaseException], BaseException, TracebackType | None] | tuple[None, None, None]
    ) -> str:
        """Format exception info."""
        # Strip any quote and new-line literals from the exceptions string
        return str(ei[1]).strip("'").replace("\\n", "\n")


def get_logger(name: str) -> logging.Logger:
    """Get a logger object to use with clockwork tools."""
    logger = logging.getLogger(name)
    sh = RichHandler(console=Console(force_terminal=True), enable_link_path=False, show_time=False)
    sh.setFormatter(ClkExceptionFormatter())
    logger.addHandler(sh)
    return logger
