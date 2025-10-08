# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for cpp_target."""

from __future__ import annotations

from pathlib import Path

import pytest
from clockwork.dsl.ir import (
    compiler,
    system_target,
    uuid_reg,
)
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_system_target(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/simplesys.clk")),
        fs_importer,
    )
    hello_system_ir = module.inner_scope.lookup("system1")
    assert isinstance(hello_system_ir, system_target.UnresolvedSystemTarget)
    hello_system = hello_system_ir.get_resolved()
    assert hello_system.value_key() == f"@{CLK_REPO}::clockwork::dsl::composition::tests::support::simplesys::system1"
    assert (
        hello_system.box_instance.value_key()
        == f"@{CLK_REPO}::clockwork::dsl::composition::tests::support::simplesys.system1"
    )
    assert uuid_reg.lookup_uuid(module.context, hello_system_ir) != uuid_reg.lookup_uuid(
        module.context, hello_system.box_instance
    )
