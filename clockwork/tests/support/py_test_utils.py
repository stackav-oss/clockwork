# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Clockwork test utilities."""

from pathlib import Path


def fix_clockwork_path(file_path: Path) -> Path:
    """Try to fix a path to support tests run from outside of the clockwork repo.

    When tests are run from an external repo the path needs to be prefixed
    with "../clockwork+/". If the file doesn't exist then try prefixing with
    "../clockwork+/" before failing to locate the file.

    Returns the original string if no file exists with either the original
    path or the fixed (prefixed) path.

    Arguments:
        file_path: Clockwork file path.

    Returns:
        File path to use to access the file.
    """
    if file_path.exists():
        return file_path
    fixed_path = Path("../clockwork+") / file_path
    if fixed_path.exists():
        return fixed_path
    return file_path
