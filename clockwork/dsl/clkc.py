# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Executable module for clkc, the Clockwork Compiler."""

import os
from pathlib import Path

import click
from clockwork.dsl.bazel import extract_targets
from clockwork.dsl.composition import systemgen
from clockwork.dsl.ir import compiler, cpp_executable, importer, node, system_target
from clockwork.dsl.ir.cpp_target import CppTarget
from clockwork.dsl.ir.module_id import ModuleID
from clockwork.dsl.ir.nanobind_target import NanobindTarget
from clockwork.dsl.ir.path_resolver import BazelPathResolver
from clockwork.dsl.ir.proto_target import ProtoTarget
from clockwork.dsl.ir.py_target import PyTarget


def export_language_targets(root_dir: Path, module: node.Module, write_json_files: bool) -> None:
    """Produce gen code from each language target in the Clockwork module.

    Args:
        root_dir: Prefix needed to write to the right spot for bazel.
        include_dir: The include path for the generated cpp files.
        module: The compiled Clockwork module.
        write_json_files: Flag to write JSON versions of the generated configuration files.
    """
    for obj in module.inner_scope.names.values():
        if isinstance(obj, CppTarget | cpp_executable.CppExecutable | NanobindTarget | ProtoTarget | PyTarget):
            obj.render_and_write(root_dir)
        elif isinstance(obj, system_target.UnresolvedSystemTarget):
            systemgen.gen_system(root_dir, obj.get_resolved(), True, write_json_files)


@click.group()
@click.pass_context
def clkc(ctx: click.core.Context) -> None:
    """Entrypoint for clkc."""
    if ctx.invoked_subcommand == "bazel-targets":
        # This allows all `.clk` files to exist and enable parsing a
        # full clockwork module instead of needing to do two passes.
        os.chdir(os.environ["BUILD_WORKING_DIRECTORY"])


@clkc.command()
@click.option(
    "-i",
    "--input",
    "input_file",
    required=True,
    type=click.Path(exists=True, file_okay=True, dir_okay=False, path_type=Path),
    help="Path to the input .clk file to compile.",
)
@click.option(
    "-r",
    "--root",
    "root_dir",
    required=True,
    type=click.Path(exists=True, file_okay=False, dir_okay=True, path_type=Path),
    help="Path to the root directory where output files will be written.",
)
@click.option(
    "--repo",
    required=True,
    type=str,
    help="Repository for this clockwork file.",
)
@click.option(
    "--write-json-files",
    is_flag=True,
    help="Enable writing configuration files in JSON format.",
)
def compile_module(input_file: Path, root_dir: Path, repo: str, write_json_files: bool) -> None:
    """Compile the clockwork file."""
    try:
        relative_path = input_file.relative_to(root_dir)
    except ValueError:
        relative_path = input_file

    module_id = ModuleID.from_path(repo, relative_path)
    out_dir = root_dir / BazelPathResolver().to_buildtime_path(module_id).parent
    out_cache_file = (out_dir / input_file.name).with_suffix(".clk_pkl")
    module = compiler.compile_source_file(
        module_id,
        importer=importer.FilesystemImporter(compile_fn=compiler.compile_source_file),
        out_cache_file=out_cache_file,
    )

    export_language_targets(root_dir, module, write_json_files)


@clkc.command()
@click.option(
    "-i",
    "--input",
    "input_file",
    required=True,
    type=click.Path(exists=True, file_okay=True, dir_okay=False, path_type=Path),
    help="Path to the input .clk file to compile.",
)
@click.option(
    "--repo",
    required=True,
    type=str,
    help="Repository for this clockwork file.",
)
def bazel_targets(input_file: Path, repo: str) -> None:
    """Print out bazel targets for a clk module."""
    all_targets = extract_targets.extract_bazel_targets(repo, input_file)
    for some_target in all_targets:
        print(some_target)


if __name__ == "__main__":
    clkc()
