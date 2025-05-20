# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for cpp_target."""

from __future__ import annotations

from pathlib import Path

import pytest
from clockwork.dsl.bazel.cc_targets import CcBinary
from clockwork.dsl.bazel.targets import Label
from clockwork.dsl.ir import (
    compiler,
    cpp_executable,
)
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_cpp_executable_output_targets(fs_importer: FilesystemImporter) -> None:
    source = """
cpp_executable output_targets_exe
{
    casing
    {
    }
}
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "bar"), fs_importer)

    default_deps = [
        Label("//clockwork/scaffolding:abstract_casing"),
        Label("//clockwork/scaffolding:casing"),
        Label("//jewels/memory:memory_resource"),
        Label("//jewels/memory:pmr_shared_ptr"),
        Label("//clockwork/scaffolding:online_main"),
    ]

    cpp_exe_ir = module.inner_scope.lookup("output_targets_exe", recursive=False)
    assert isinstance(cpp_exe_ir, cpp_executable.CppExecutable)
    assert cpp_exe_ir.output_targets() == [
        CcBinary(
            name="output_targets_exe",
            srcs=[Path("output_targets_exe.hh"), Path("output_targets_exe.inl"), Path("output_targets_exe.cc")],
            deps=default_deps,
            data=[],
        )
    ]
