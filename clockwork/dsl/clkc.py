# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Executable module for clkc, the Clockwork Compiler."""

import cProfile
import json
import os
import time
from pathlib import Path

import click
from clockwork.dsl import parser_backend
from clockwork.dsl.aligner.gen.search_codegen import render_aligner_impl
from clockwork.dsl.aligner.pipeline import AlignerAnalysis, analyze_aligner, compute_codegen_plan
from clockwork.dsl.aligner.type_check import type_check_aligner
from clockwork.dsl.bazel import extract_targets
from clockwork.dsl.composition import systemgen
from clockwork.dsl.cpp.context import CppModuleChunks
from clockwork.dsl.ir import compiler, cpp_executable, importer, node, system_target
from clockwork.dsl.ir.aligner import Aligner

# pyrefly: ignore[implicit-reexport] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
from clockwork.dsl.ir.cpp_target import CppCog, CppTarget
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


def _compile_module(
    input_file: Path,
    root_dir: Path,
    repo: str,
    write_json_files: bool,
    timing: compiler.CompileTiming | None = None,
) -> node.Module:
    """Compile a Clockwork file and export generated outputs."""
    root_dir.mkdir(parents=True, exist_ok=True)
    try:
        relative_path = input_file.relative_to(root_dir)
    except ValueError:
        relative_path = input_file

    module_id = ModuleID.from_path(repo, relative_path)
    out_dir = root_dir / BazelPathResolver().to_buildtime_path(module_id).parent
    out_dir.mkdir(parents=True, exist_ok=True)

    def compile_import(module_id: ModuleID, importer_instance: node.Importer) -> node.Module:
        """Compile an imported module with the same timing accumulator."""
        return compiler.compile_source_file(module_id, importer_instance, timing=timing)

    filesystem_importer = importer.FilesystemImporter(compile_fn=compile_import)
    module = compiler.compile_source_file(
        module_id,
        importer=filesystem_importer,
        timing=timing,
    )

    # Type-check and analyze aligner bodies before codegen.
    # Analyses captured here to break a dependency cycle between cpp_target and aligner codegen.
    analyses: dict[str, AlignerAnalysis] = {}
    for entity in module.inner_scope.names.values():
        if isinstance(entity, Aligner) and entity.resolved is not None:
            type_check_aligner(entity, module.context)
            analyses[entity.resolved.name] = analyze_aligner(entity)

    def _render_search_impl(aligner_ir: Aligner, cpp_cog: CppCog, target: CppTarget) -> CppModuleChunks:
        """Callback for CppTarget: generate C++ search impl for one aligner."""
        assert aligner_ir.resolved is not None
        analysis = analyses[aligner_ir.resolved.name]
        plan = compute_codegen_plan(analysis, aligner_ir.resolved)
        return render_aligner_impl(
            plan=plan,
            resolved=aligner_ir.resolved,
            cpp_cog=cpp_cog,
            target=target,
        )

    for entity in module.inner_scope.names.values():
        if isinstance(entity, CppTarget):
            entity.search_impl_renderer = _render_search_impl

    export_language_targets(root_dir, module, write_json_files)
    return module


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
    type=click.Path(exists=False, file_okay=False, dir_okay=True, path_type=Path),
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
@click.option(
    "--marker-output",
    "marker_output",
    type=click.Path(exists=False, file_okay=True, dir_okay=False, path_type=Path),
    help="Optional non-cache marker output for compile-only Bazel actions.",
)
def compile_module(
    input_file: Path,
    root_dir: Path,
    repo: str,
    write_json_files: bool,
    marker_output: Path | None,
) -> None:
    """Compile the clockwork file."""
    _compile_module(input_file, root_dir, repo, write_json_files)
    if marker_output is not None:
        marker_output.parent.mkdir(parents=True, exist_ok=True)
        marker_output.write_text(f"compiled with {parser_backend.selected_backend()} parser\n")


@clkc.command(name="benchmark-compile-module")
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
    type=click.Path(exists=False, file_okay=False, dir_okay=True, path_type=Path),
    help="Path to the root directory where output files will be written.",
)
@click.option(
    "--repo",
    required=True,
    type=str,
    help="Repository for this clockwork file.",
)
@click.option(
    "--timing-output",
    "timing_output",
    required=True,
    type=click.Path(exists=False, file_okay=True, dir_okay=False, path_type=Path),
    help="Path to write benchmark timing JSON.",
)
@click.option(
    "--nonce",
    default="",
    type=str,
    help="Opaque value used to force Bazel action reruns.",
)
@click.option(
    "--profile-output",
    "profile_output",
    type=click.Path(exists=False, file_okay=True, dir_okay=False, path_type=Path),
    help="Optional path to write a cProfile profile for the benchmarked compile.",
)
@click.option(
    "--write-json-files",
    is_flag=True,
    help="Enable writing configuration files in JSON format.",
)
def benchmark_compile_module(  # noqa: PLR0913 This is a click CLI callback.
    input_file: Path,
    root_dir: Path,
    repo: str,
    timing_output: Path,
    nonce: str,
    profile_output: Path | None,
    write_json_files: bool,
) -> None:
    """Compile a Clockwork module and write a timing record."""
    timing = compiler.CompileTiming()
    profiler: cProfile.Profile | None = None
    if profile_output is not None:
        profile_output.parent.mkdir(parents=True, exist_ok=True)
        profiler = cProfile.Profile()

    start_ns = time.perf_counter_ns()
    try:
        if profiler is not None:
            profiler.enable()
        _compile_module(input_file, root_dir, repo, write_json_files, timing=timing)
    finally:
        if profiler is not None:
            profiler.disable()
        elapsed_ns = time.perf_counter_ns() - start_ns
        if profiler is not None:
            profiler.dump_stats(str(profile_output))

    non_parse_elapsed_ns = elapsed_ns - timing.parse_elapsed_ns

    timing_output.parent.mkdir(parents=True, exist_ok=True)
    timing_output.write_text(
        json.dumps(
            {
                "backend": parser_backend.selected_backend(),
                "elapsed_ns": elapsed_ns,
                "elapsed_s": elapsed_ns / 1_000_000_000,
                "input": str(input_file),
                "non_parse_elapsed_ns": non_parse_elapsed_ns,
                "non_parse_elapsed_s": non_parse_elapsed_ns / 1_000_000_000,
                "nonce": nonce,
                "parse_elapsed_ns": timing.parse_elapsed_ns,
                "parse_elapsed_s": timing.parse_elapsed_ns / 1_000_000_000,
                "parsed_module_count": timing.parsed_module_count,
                "profile_output": str(profile_output) if profile_output is not None else None,
                "repo": repo,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n"
    )


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
