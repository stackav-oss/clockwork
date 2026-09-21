# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Helpers for reading/writing topology files associated with topology targets."""

import os
import subprocess
from pathlib import Path
from typing import Final

from clockwork.tools.topology import topology

_WORKING_DIR_VAR: Final = "BUILD_WORKING_DIRECTORY"


def _get_build_working_dir() -> str:
    """Get the build working directory from bazel.

    This maps to the root of the bazel repository where `bazel run`
    was run from.
    """
    var = os.getenv(_WORKING_DIR_VAR)
    if not var:
        msg = f"Environment variable '{_WORKING_DIR_VAR}' is not set.  Bazel should set this when using `bazel run`."
        raise RuntimeError(msg)
    return var


def _update_topology_information(topology_label: str) -> None:
    """Make sure the topology_summary target is up-to-date."""
    subprocess.run(
        ["bazel", "build", "--remote_download_toplevel", topology_label],
        check=True,
        cwd=_get_build_working_dir(),
    )


def _to_topology_file(topology_label: str) -> Path:
    """Convert the label to the expected output file."""
    working_dir = _get_build_working_dir()
    prefix = Path(working_dir) / "bazel-bin"
    repo, label = topology_label.split("//")
    if repo:
        prefix /= f"external/{repo[1:]}+"
    return prefix / (label.replace(":", "/") + ".pkl")


def target_topology(topology_label: str) -> topology.System:
    """Automatically update and load the topology for a given target label."""
    _update_topology_information(topology_label)
    topology_file = _to_topology_file(topology_label)
    if not topology_file.exists():
        msg = f"Target {topology_label} did not produce a topology_file.  Is it created by the 'topology_summary' rule?"
        raise ValueError(msg)
    with topology_file.open("rb") as f:
        return topology.load_system(f)
