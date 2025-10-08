# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""A library for extracting bazel targets from a clockwork module."""

from collections.abc import Iterable
from pathlib import Path

from clockwork.dsl.bazel import targets
from clockwork.dsl.composition import systemgen
from clockwork.dsl.ir import compiler, cpp_executable, importer, system_target
from clockwork.dsl.ir.cpp_target import CppTarget
from clockwork.dsl.ir.module_id import ModuleID
from clockwork.dsl.ir.nanobind_target import NanobindTarget
from clockwork.dsl.ir.proto_target import ProtoTarget
from clockwork.dsl.ir.py_target import PyTarget


def extract_bazel_targets(
    current_repo: str,
    input_file: Path,
    relative_path: Path | None = None,
    search_paths: Iterable[Path] | None = None,
) -> list[targets.Target]:
    """Extract all bazel targets that should exist for a clockwork module."""
    # If we keep the chdir trick, then we can change this to take in a
    # module to avoid parsing the CST twice.  For now we leave it
    # assuming we'll end up doing two passes.
    import_path = relative_path or input_file
    module_id = ModuleID.from_path(current_repo, import_path)
    # Augment the search paths with the root directory. This lets us
    # find clockwork dependencies (assuming they aren't gen files or
    # from an external repo) without having a bazel dependency.  It is
    # very important that we do not augment the search paths when
    # compiling as we could end up using a file that bazel did not
    # intend us to use.
    clk_target = compiler.to_clk_target(module_id, search_paths=search_paths)

    try:
        module = compiler.compile_source_file(
            module_id,
            importer=importer.FilesystemImporter(compile_fn=compiler.compile_source_file),
        )
    except FileNotFoundError:
        # The clk-deps tool runs iteratively, so lets return the clk
        # target itself so the other clk dependencies are resolved.
        return [clk_target]

    all_targets: list[targets.Target] = [clk_target]
    all_outs: list[Path] = []
    for obj in module.inner_scope.names.values():
        if isinstance(obj, CppTarget | cpp_executable.CppExecutable | NanobindTarget | ProtoTarget | PyTarget):
            for output_target in obj.output_targets():
                if outs := output_target.generated_files:
                    all_outs.extend(outs)
                all_targets.append(output_target)

        elif isinstance(obj, system_target.UnresolvedSystemTarget):
            generated_files, output_targets, _ = systemgen.gen_system(Path(), obj.get_resolved(), False, False)
            for value in output_targets.values():
                all_outs.extend(value.simple_launch_config.generated_files)
                all_targets.append(value.simple_launch_config)
            all_outs.extend([Path(x.name) for x in generated_files.channel_allocation_report_files])
            all_outs.extend([Path(x.name) for x in generated_files.channel_spy_config_files])
            all_outs.extend([Path(x.name) for x in generated_files.diagnostics_database_config_files])
            all_outs.extend([Path(x.name) for x in generated_files.logged_channel_metadata_files])
            all_outs.extend([Path(x.name) for x in generated_files.metrics_channel_metadata_files])

    clk_target.outs = all_outs

    return all_targets
