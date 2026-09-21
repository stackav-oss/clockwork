# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Thin wrapper around nanobind stubgen."""

import re
import sys
from pathlib import Path

import click
from nanobind.stubgen import main as stubgen_main

REPLACEMENTS = [
    # We want an NDArray, not an ArrayLike.
    # Turn this:
    # > Annotated[ArrayLike, dict(dtype='uint8', shape=(68), writable=False)]
    # into this:
    # > Annotated[numpy.typing.NDArray[np.uint8], Literal[68]]
    (
        r"Annotated\[ArrayLike, dict"
        + r"\(dtype='([^']+)'"
        + r", shape=\(([^)]+)\)"
        + r"(?:, order='[^']+')?"  # optional order
        + r"\)\]",
        r"Annotated[numpy.typing.NDArray[np.\1], Literal[\2]]",
    )
]


def get_import_path(input_module: Path, py_module: Path) -> Path:
    """Get import path.  Needed for targets in an external bzlmod."""
    input_module_parent = str(input_module)
    py_module_parent = str(py_module)
    if not input_module_parent.endswith(py_module_parent):
        msg = f"Unable to get import path for: {input_module} {py_module}"
        raise ValueError(msg)
    return Path(input_module_parent[: -len(py_module_parent)])


def wrapper(
    input_module: Path,
    output_pyi: Path,
    pattern_file: Path,
    bindir: Path,
    debug: bool,
) -> None:
    """A small wrapper around nanobind stubgen.

    This exists for two reasons:
    1. Turn 'bazel-out/k8-fastbuild/bin/path/to/foo.so' into 'path.to.foo'
    2. A different py_binary is created from this file for each extensions module. Each one has different 'deps', which
       has bazel set up the python deps correctly for us. That way we know that importlib, which is used by stubgen,
       will find all the dependencies correctly.
    """
    py_module = str(input_module).removeprefix(str(bindir) + "/")
    parts = py_module.split("/")
    external_path_parts = 2
    extra_args = []
    if len(parts) >= external_path_parts and parts[0] == "external" and parts[1].endswith("+"):
        py_module = "/".join(parts[2:])
        import_path = get_import_path(input_module, Path(py_module))
        extra_args.extend(
            [
                "--import",
                str(import_path),
            ]
        )
    py_module = py_module.replace("/", ".")
    py_module = py_module.removesuffix(".so")

    stubgen_args = [
        "--output-file",
        str(output_pyi),
        "--module",
        py_module,
        "--include-private",
        "--pattern-file",
        str(pattern_file),
        *extra_args,
    ]

    if debug is False:
        stubgen_args.append("--quiet")

    # call the stubgen tool
    stubgen_main(stubgen_args)

    # run some cleanups
    _cleanup_output(output_pyi)


def _cleanup_output(output_pyi: Path) -> None:
    """Run some regex replacements to customize some types."""
    # run some cleanups
    output_text = output_pyi.read_text()
    for pattern, replacement in REPLACEMENTS:
        output_text = re.sub(pattern, replacement, output_text)

    # If numpy is in there, also import Literal and the full numpy.typing module
    if "from numpy.typing" in output_text:
        output_text = output_text.replace(
            "from numpy.typing",
            "from typing import Literal\nimport numpy as np\nimport numpy.typing\nfrom numpy.typing",
        )

    output_pyi.write_text(output_text)


@click.command()
@click.argument(
    "input_module",
    type=click.Path(exists=True, dir_okay=False, path_type=Path),
)
@click.argument(
    "output_pyi",
    type=click.Path(exists=False, dir_okay=False, path_type=Path),
)
@click.option(
    "--pattern_file",
    type=click.Path(exists=True, dir_okay=False, path_type=Path),
    required=True,
)
@click.option(
    "--bindir",
    type=click.Path(exists=True, file_okay=False, path_type=Path),
    required=True,
)
@click.option(
    "-d",
    "--debug/--no-debug",
    help=("Print debugging information."),
)
def cli(
    input_module: Path,
    output_pyi: Path,
    pattern_file: Path,
    bindir: Path,
    debug: bool,
) -> None:
    """Run the wrapper and make errors less verbose."""
    try:
        wrapper(input_module, output_pyi, pattern_file, bindir, debug)
    except Exception as e:  # noqa: BLE001 (the point of this is to catch all exceptions)
        print(f"==============  Error generating nanobind type stubs for '{input_module}'  =================")
        print(e)
        sys.exit(1)


if __name__ == "__main__":
    cli()
