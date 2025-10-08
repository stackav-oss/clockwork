#!/usr/bin/env python3
# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0


"""Sort BUILD files given on the command line."""

import functools
import os
import subprocess
import sys
from pathlib import Path


@functools.cache
def workspace_root() -> Path:
    """Get the Bazel workspace root."""
    path = Path().absolute()
    while True:
        if (path / "WORKSPACE").is_file():
            return path
        if path == Path("/"):
            msg = "No WORKSPACE file found"
            raise FileNotFoundError(msg)
        path = path.parent


def sort_build_file(
    file: Path,
    buildifier: Path = Path("buildifier"),  # pyright: ignore[reportCallInDefaultInitializer] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
    buildozer: Path = Path("buildozer"),  # pyright: ignore[reportCallInDefaultInitializer] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
) -> None:
    """Sort the given BUILD.bazel file."""
    if file.name != "BUILD.bazel":
        return
    if file.is_absolute():
        file = file.relative_to(workspace_root())

    package_path = f"//{file.parent}" if file.parent != Path() else "//"
    names = subprocess.check_output([buildozer, "print name", f"{package_path}:*"], text=True).splitlines()
    sorted_names = sorted(names)
    if names == sorted_names:
        return

    package_name = file.parent.name

    names_to_delete = sorted_names
    while "" in names_to_delete:
        # If there's an unnamed macro, like exports_files, we'll get an empty name
        names_to_delete.remove("")

    # A target with the same name as the package should float to the top.
    if (count := names_to_delete.count(package_name)) == 1:
        names_to_delete.pop(names_to_delete.index(package_name))

        # Buildozer has some crazy behavior when you try to print out names:
        # https://github.com/bazelbuild/buildtools/blob/v7.3.1/build/rule.go#L127-L130
        # So there's actually a reasonable chance that the package-named target doesn't exist.
        # To confirm, we try to refer to it explicitly and see if that fails.
        target_to_check = f"{package_path}:{package_name}"
        completed_process = subprocess.run(
            [buildozer, "print name", target_to_check], capture_output=True, text=True, check=False
        )
        if completed_process.returncode != 0:
            error_message = f"error while executing commands [{{[print name]}}] on target {target_to_check}: rule '{package_name}' not found"
            if error_message in completed_process.stderr:
                # Target is fake - drop it
                pass
            else:
                # We had some other error - raise it
                raise subprocess.CalledProcessError(
                    completed_process.returncode,
                    completed_process.args,
                    completed_process.stdout,
                    completed_process.stderr,
                )
        else:
            # Target is real - add it back at the beginning
            names_to_delete.insert(0, package_name)
    elif count > 1:
        # If we have multiple targets with the same name as the package, this means there's a nested list comprehension.
        # Buildozer can't handle these, so skip them. They'll float to the top.
        names_to_delete = [name for name in names_to_delete if name != package_name]

    target_specifiers = [f"{package_path}:{name}" for name in names_to_delete]

    # Store the rules (in order) and then delete them from the file
    rules = subprocess.check_output([buildozer, "print rule", *target_specifiers], text=True)
    subprocess.check_output([buildozer, "delete", *target_specifiers], stderr=subprocess.PIPE)

    # Write them back to the file in order
    with file.open("a") as file_to_write:
        # Write an extra newline so that any file comment doesn't now attach to the first rule we're writing.
        file_to_write.write("\n")
        file_to_write.write(rules)

    # Reformat, because buildozer mangles stuff. Capture output to hide the "fixed foo/BUILD.bazel" message.
    subprocess.check_output([buildifier, file], stderr=subprocess.PIPE)


def main(files: list[str]) -> None:
    """Sort the given BUILD files by target name."""
    # Buildozer requires us to be in the workspace in order to work
    os.chdir(os.getenv("BUILD_WORKING_DIRECTORY", "."))

    for file in files:
        sort_build_file(Path(file))


if __name__ == "__main__":
    main(sys.argv[1:])
