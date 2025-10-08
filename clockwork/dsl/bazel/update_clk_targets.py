# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Update the targets in the given BUILD file based on the given clk file."""

import fnmatch
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from tempfile import TemporaryDirectory

import click
import runfiles
from clockwork.dsl import clk_exception
from clockwork.dsl.bazel import cc_targets, extract_targets, targets

from tools.sort_build_file import sort_build_file


def fetch_attr(buildozer: Path, attr: str, target_specifier: str, repo_root: Path) -> str | None:
    """Get an attribute from buildozer output."""
    popen = subprocess.run(
        [buildozer, f"print {attr}", target_specifier],
        cwd=repo_root,
        capture_output=True,
        text=True,
        check=False,
    )
    if popen.returncode != 0:
        return None

    attr_value = popen.stdout.strip()
    if attr_value == "(missing)":
        return None

    return attr_value


def format_attr(buildozer: Path, attr: str, target_specifier: str, repo_root: Path) -> str | None:
    """Format a list attribute from buildozer output to be suitable for putting in a BUILD file."""
    attr_value = fetch_attr(buildozer, attr, target_specifier, repo_root)
    if not attr_value:
        return None

    # Example output from Buildozer:
    #  $ buildozer 'print visibility' //clockwork/pinion:bidirectional_udp
    #  [//hardware:__subpackages__ //clockwork:__subpackages__]
    # We need to add a quote mark after every '[' and before every ']',
    # and we need to add quotes and a comma for every space.
    attr_value = attr_value.replace("[", '["').replace("]", '"]').replace(" ", '", "')
    return f"{attr} = {attr_value},"


def _find_prefix(logical_path: Path, physical_path: Path) -> Path:
    """Find the bazel build prefix if present in the output path."""
    common = 0
    for p1, p2 in zip(reversed(logical_path.parent.parts), reversed(physical_path.parent.parts), strict=False):
        if p1 != p2:
            break
        common += 1

    if not common:
        return Path()
    return Path(*physical_path.parts[: -common - 1])


def _ensure_target_name_uniqueness(all_targets: list[targets.Target], relative_path: Path) -> None:
    """Ensure that all target names are unique."""
    target_names: dict[str, str] = {}
    for target in all_targets:
        if target.name in target_names:
            msg = f"\nERROR: Processing the clk file {relative_path} would result in two targets both named {target.name}."
            msg += (
                f"\nOne of these is a {target.kind} target and the other is a {target_names[target.name]} target.\n\n"
            )
            raise ValueError(msg)
        target_names[target.name] = target.kind


def _workspace_dir() -> Path:
    """Get the root of the workspace."""
    # Walk upwards from the root so that we find the outer workspace if we're in a nested workspace
    this_file = Path(__file__).resolve()
    # Walk up until we find MODULE.bazel
    for parent in reversed(this_file.parents):
        if (parent / "MODULE.bazel").exists():
            return parent
    msg = "Unable to determine the root of the workspace."
    raise RuntimeError(msg)

_SKIPPED_MODULE_PATTERNS = [
]

_BAZEL_OUT_BIN_DIR_REGEX = r"^bazel-out/[^/]+/bin/"


def _strip_bazel_out_bin_dir(some_path: str) -> str:
    """Strips leading 'bazel-out/k8-*/bin/' if it exists (needed for genrules)."""
    return re.sub(_BAZEL_OUT_BIN_DIR_REGEX, "", some_path)


def _should_skip(clk_file: Path) -> bool:
    """Check if we should skip this clk module."""
    module_path = _strip_bazel_out_bin_dir(str(clk_file))
    return any(fnmatch.fnmatch(module_path, pattern) for pattern in _SKIPPED_MODULE_PATTERNS)


@click.command()
@click.argument("buildifier", type=click.Path(exists=True, dir_okay=False, path_type=Path))
@click.argument("buildozer", type=click.Path(exists=True, dir_okay=False, path_type=Path))
@click.argument("input_build_file", type=click.Path(path_type=Path))
@click.argument("clk_file", type=click.Path(exists=True, dir_okay=False, path_type=Path))
@click.argument("output_build_file", type=click.Path(path_type=Path))
@click.option(
    "--repo",
    required=True,
    type=str,
    help="Repository for this clockwork file.",
)
def update_targets_for_clk_file(  # noqa: PLR0913 This is a CLI tool, cannot change the args.
    buildifier: Path, buildozer: Path, input_build_file: Path, clk_file: Path, output_build_file: Path, repo: str
) -> None:
    """Calculate the targets for the given clk file and spit out an updated BUILD file to match."""
    build_prefix = _find_prefix(logical_path=input_build_file, physical_path=clk_file)
    relative_path = clk_file.relative_to(build_prefix)

    # We run commands in different dirs so make sure these are absolute.
    normalized_args = NormalizedCliArgs(
        buildifier=buildifier.absolute(),
        buildozer=buildozer.absolute(),
        input_build_file=input_build_file.absolute(),
        clk_file=clk_file,
        output_build_file=output_build_file.absolute(),
        repo=repo,
    )

    # If the clk file is in a skipped package, just copy the
    # input build file to the output build file without changes.
    if _should_skip(clk_file):
        with normalized_args.output_build_file.open("w") as output_handle:
            output_handle.write(normalized_args.input_build_file.read_text())
        sys.exit(0)

    all_targets = _get_all_targets(normalized_args.clk_file, normalized_args.repo, relative_path)

    _ensure_target_name_uniqueness(all_targets, relative_path)

    with TemporaryDirectory() as tempdir:
        _update_build_file(
            tempdir,
            normalized_args,
            all_targets,
        )


def _get_all_targets(
    clk_file: Path,
    repo: str,
    relative_path: Path | None = None,
) -> list[targets.Target]:
    """Get the list of which targets should exist for this clk file.

    Unfortunately clkc has to be able to resolve potentially *all*
    clk files in the repo, including those that are not listed in
    `deps` anywhere (that's the point here, that we can add missing
    files to our deps). To get around this issue, we provide the
    root directory of the repository and use that to augment search
    paths only for resolving `use` statements.  When compiling the
    module to gather other dependencies, everything is within the
    sandbox to use only what bazel intends us to use.
    """
    root = _workspace_dir()
    search_paths = []
    search_paths.append(root)
    return extract_targets.extract_bazel_targets(
        repo,
        clk_file,
        relative_path=relative_path,
        search_paths=search_paths,
    )


def copy_buildifier_config(directory: Path) -> None:
    """Copy the buildifier config file to the given directory."""
    # Make sure we copy our buildifier config and tables to the tempdir so that buildifier formats properly.
    rfiles = runfiles.Create()
    if not rfiles:
        msg = "Runfiles not found. This script must be run in a Bazel environment."
        raise RuntimeError(msg)
    buildifier_config = None
    buildifier_tables = None
    for module in ["clockwork+", "_main"]:
        buildifier_config = rfiles.Rlocation(f"{module}/.buildifier.json")
        buildifier_tables = rfiles.Rlocation(f"{module}/.buildifier_tables.json")
        if (
            buildifier_config
            and Path(buildifier_config).exists()
            and buildifier_tables
            and Path(buildifier_tables).exists()
        ):
            break
    else:
        msg = "Unable to find .buildifier.json or .buildifier_tables.json in runfiles."
        raise RuntimeError(msg)
    shutil.copy(Path(buildifier_config), directory / ".buildifier.json")
    shutil.copy(Path(buildifier_tables), directory / ".buildifier_tables.json")


@dataclass
class NormalizedCliArgs:
    """CLI arguments with [some appropriate] paths made absolute."""

    buildifier: Path
    buildozer: Path
    input_build_file: Path
    clk_file: Path
    output_build_file: Path
    repo: str


def _update_build_file(
    tempdir: str,
    args: NormalizedCliArgs,
    all_targets: list[targets.Target],
) -> None:
    tempdir_path = Path(tempdir)

    kinds = {target.kind for target in all_targets}

    # Add the parent directory in the path so that the package name is correct for sorting targets later.
    updated_build_file = tempdir_path / args.clk_file.parent.name / "BUILD.bazel"
    updated_build_file.parent.mkdir(exist_ok=True)

    # Write a new file instead of copying to avoid permissions issues.
    updated_build_file.write_text(args.input_build_file.read_text())

    # We need this file for buildozer to work.
    (tempdir_path / "WORKSPACE").touch()

    # Read existing visibility, etc specifiers and then delete the existing copies of the rules, if any.
    # We do a separate buildozer call per target so that we don't have to do any parsing.
    target_visibilities: dict[str, str | None] = {}
    target_diags: dict[str, targets.Label | None] = {}
    target_data: dict[str, str | None] = {}
    for target_obj in all_targets:
        target_specifier = f"//{args.clk_file.parent.name}:{target_obj.name}"
        target_visibilities[target_obj.name] = format_attr(args.buildozer, "visibility", target_specifier, tempdir_path)
        target_data[target_obj.name] = format_attr(args.buildozer, "data", target_specifier, tempdir_path)
        target_dep_str = fetch_attr(args.buildozer, "deps", target_specifier, tempdir_path)
        if target_dep_str:
            target_deps = target_dep_str.strip("[]").split()
            target_diags[target_obj.name] = next((targets.Label(i) for i in target_deps if i.endswith("_diags")), None)

        # This is allowed to fail in case the target doesn't actually exist yet.
        subprocess.run([args.buildozer, "delete", target_specifier], cwd=tempdir_path, capture_output=True, check=False)

    # Add new loads at the beginning so that they will correctly sort to the top instead of after file comments.
    new_loads = ""
    for kind in kinds:
        if kind.startswith("clk"):
            load = f'load("{targets.get_bazel_label_for_clk_label(args.repo, "//clockwork:rules.bzl")}"  , "{kind}")'
        else:
            load = {
                "cc_binary": 'load("//tools/rules:cc.bzl", "cc_binary")',
                "cc_binary_with_embedded_py": 'load("//tools/rules:python.bzl", "cc_binary_with_embedded_py")',
                "cc_library": 'load("//tools/rules:cc.bzl", "cc_library")',
                "merge_simplelaunch_config": f'load("{targets.get_bazel_label_for_clk_label(args.repo, "//jewels/simplelaunch:rules.bzl")}"  , "merge_simplelaunch_config")',
                "py_library": 'load("//tools/rules:python.bzl", "py_library")',
                "py_cc_binding": 'load("//tools/rules:python.bzl", "py_cc_binding")',
                "proto_cc_library": 'load("@build_stack_rules_proto//rules/cc:proto_cc_library.bzl", "proto_cc_library")',
                "proto_compile": 'load("@build_stack_rules_proto//rules/py:proto_py_library.bzl", "proto_py_library")',
                "proto_library": 'load("@rules_proto//proto:defs.bzl", "proto_library")',
                "proto_py_library": 'load("@build_stack_rules_proto//rules:proto_compile.bzl", "proto_compile")',
                "proto_go_library": 'load("@build_stack_rules_proto//rules/go:proto_go_library.bzl", "proto_go_library")',
            }[kind]
        new_loads += f"{load}\n"
    updated_build_file.write_text(updated_build_file.read_text() + new_loads)

    # Add all new rules at the end.
    with updated_build_file.open("a") as b:
        package = f"//{_strip_bazel_out_bin_dir(str(args.clk_file.parent))}"
        for target_obj in all_targets:
            if (diags := target_diags.get(target_obj.name)) is not None and isinstance(
                target_obj, cc_targets.CcLibrary
            ):
                target_obj.deps.append(diags)

            target_with_short_labels = str(target_obj).replace(f"'{package}:", "':")

            if (vis := target_visibilities.get(target_obj.name)) is not None:
                target_with_short_labels = target_with_short_labels.removesuffix(")") + vis + ")"
            if (
                target_obj.kind in ["cc_binary", "cc_binary_with_embedded_py"]
                and (data := target_data.get(target_obj.name)) is not None
            ):
                target_with_short_labels = target_with_short_labels.removesuffix(")") + data + ")"
            # TODO(DX-1394): Remove this workaround
            if target_obj.kind == "cc_library" and package.startswith(
                "//platforms/visualization/vizlog_converter/visualizers"
            ):
                target_with_short_labels = target_with_short_labels.removesuffix(")") + "alwayslink = True,)"
            b.write(target_with_short_labels + "\n")

    # And sort them.
    copy_buildifier_config(tempdir_path)
    os.chdir(tempdir_path)
    sort_build_file(updated_build_file, args.buildifier, args.buildozer)

    # If the targets were coincidentally already in order, sort_build_file will exit early and won't run buildifier.
    # Make sure to run buildifier manually.
    subprocess.check_output(
        [args.buildifier, "--add_tables=.buildifier_tables.json", updated_build_file], stderr=subprocess.PIPE
    )

    # Finally, move the new BUILD file to the specified output path.
    shutil.move(updated_build_file, args.output_build_file)


if __name__ == "__main__":
    logger = clk_exception.get_logger(Path(__file__).name)
    try:
        update_targets_for_clk_file()
    except (ValueError, TypeError, KeyError, SyntaxError, FileNotFoundError):
        logger.exception("update_clk_targets -- Parsing Exception:")
