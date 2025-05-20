# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Test the Schema IR module."""

from textwrap import dedent

from clockwork.dsl.ir import (
    clkbuiltins,
    compiler,
    importer,
    strongtypes,
)
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


def test_strong_type_parsing() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Docs
        strong_type MyStrongType
        {
          underlying_type: Float32;
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "my_strong_type"), importer=fs_importer)

    my_strong_type = module.inner_scope.lookup("MyStrongType")
    assert isinstance(my_strong_type, strongtypes.StrongType)
    assert my_strong_type.typespec == clkbuiltins.FLOAT32


def test_tag_types() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Docs
        tag TestTag;
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "my_test_tag"), importer=fs_importer)

    test_tag = module.inner_scope.lookup("TestTag")
    assert isinstance(test_tag, strongtypes.Tag)
    assert test_tag.fqn == f"@{CLK_REPO}::my_test_tag.TestTag"
