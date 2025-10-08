# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Wrapper around 'uv' so that we can move input and output files around to please Bazel."""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Final

import click


@click.command(help=__doc__)
@click.argument("req_in", type=click.Path(path_type=Path))
@click.argument("existing_req_out", type=click.Path(path_type=Path))
@click.argument("overrides", type=click.Path(path_type=Path))
@click.argument("uv_path", type=click.Path(path_type=Path))
@click.argument("toml_path", type=click.Path(path_type=Path))
@click.argument("new_req_out", type=click.Path(path_type=Path))
@click.option(
    "--strip-extras/--no-strip-extras",
    default=False,
    help=(
        "Extras must be stripped to generate constraints files but should be preserved in requirements files to be "
        "used with rules_python to avoid losing dependency relationships "
        "(see https://rules-python.readthedocs.io/en/latest/pypi-dependencies.html#extras-dependencies)."
    ),
)
@click.option(
    "--emit-index-url/--no-emit-index-url",
    default=False,
    help=(
        "Whether or not to include `--index-url` and `--extra-index-url` entries in the generated output file. This "
        "is probably desired for requirements files but may not be for constraints files."
    ),
)
@click.option(
    "--generate-hashes/--no-generate-hashes",
    default=False,
    help=(
        "Whether or not to include hashes in the generated output file. We don't want hashes in the constraints file "
        "because they break pip."
    ),
)
@click.option(
    "--python-version",
    type=str,
    default="3.10",
    help=(
        "The Python version to use for the uv command. This is used to determine which pip version to use when "
        "compiling the requirements. The default is 3.10, which is the version used in the av repo. "
        "This is only used for the uv command and does not affect the Python version used to run this script."
    ),
)
def main(  # noqa: PLR0913 kwargs are required to pass CLI args.
    req_in: Path,
    existing_req_out: Path,
    overrides: Path,
    uv_path: Path,
    toml_path: Path,
    new_req_out: Path,
    strip_extras: bool,
    emit_index_url: bool,
    generate_hashes: bool,
    python_version: str,
) -> None:
    """Run `uv`."""
    # We play a little shell game here.
    # We want to specify the pre-existing requirements.out on the command-line, but it has to be writable.
    # So we copy it to a temp dir, read from and write to it, and then copy the result to where it needs to go.
    with tempfile.NamedTemporaryFile() as temp:
        temp_path = Path(temp.name)

        # Remove the index URL from the requirements file to eliminate the initial unauthenticated request by UV,
        # ensuring all requests are properly authenticated from the outset. See details at
        # https://github.com/astral-sh/uv/issues/14264.
        if existing_req_out.exists():
            existing_content = existing_req_out.read_text()
            modified_content = re.sub(r"^--index-url\s.*\n", "", existing_content, flags=re.MULTILINE)
            temp_path.write_text(modified_content)

        base_compile_command = [
            str(uv_path),
            "pip",
            "compile",
            str(req_in),
            f"--output-file={temp_path}",
            f"--override={overrides}",
            f"--config-file={toml_path}",
            f"--python={python_version}",
            "--quiet",
        ]
        env = os.environ | {
            # UV uses the reqwest-netrc library, which has trouble finding ~/.netrc if HOME is unset.
            # So we tell it where to find the .netrc ourselves.
            # See https://github.com/gribouille/netrc/blob/master/src/lib.rs#L85-L93
            "NETRC": str(Path.home() / ".netrc"),
            # Some pypi servers have issues with too many simultaneous connections.
            # See https://github.com/astral-sh/uv/issues/12054
            "UV_CONCURRENT_DOWNLOADS": "4",
        }

        # Compile once without restricting binaries and stripping most output just to learn the list of all packages
        popen = subprocess.run(
            [*base_compile_command, "--strip-extras"],
            env=env,
            check=False,
        )
        if popen.returncode != 0:
            # Exit like this to avoid the traceback. `uv` already prints a good error message.
            sys.exit(popen.returncode)

        # Collect the list of all packages to lock
        packages_to_lock = set(re.findall(r"^(.*)==", temp_path.read_text(), flags=re.MULTILINE))

        # Construct the list of packages for which we can use pre-built wheels
        packages_with_binaries = packages_to_lock
        # Handle --only-binary and --no-binary being mutually exclusive (pypa/pip#12348) by compiling again but now
        # with specifying --only-binary for as many packages as possible
        popen = subprocess.run(
            [
                *base_compile_command,
                *(["--emit-index-url"] if emit_index_url else []),
                *(["--generate-hashes"] if generate_hashes else []),
                "--strip-extras" if strip_extras else "--no-strip-extras",
                # TODO(DX-1421): Fix so we don't have to block considering un-buildable source distributions
                "--emit-build-options",
                *sorted(f"--only-binary={package}" for package in packages_with_binaries),
            ],
            env=env,
            check=False,
        )
        if popen.returncode != 0:
            # Exit like this to avoid the traceback. `uv` already prints a good error message.
            sys.exit(popen.returncode)

        shutil.copy(temp_path, new_req_out)


if __name__ == "__main__":
    main()
