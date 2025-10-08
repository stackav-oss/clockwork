# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for tachyon_layout module."""

from typing import Final

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir import compiler, importer, schema
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.serialization import tachyon_layout


@pytest.fixture()
def fs_importer() -> importer.FilesystemImporter:
    return importer.FilesystemImporter(compile_fn=compiler.compile_source_file)


@pytest.fixture()
def compiler_context() -> CompilerContext:
    """Create a clean compiler context for testing."""
    return CompilerContext()


def test_constrained_field_sort() -> None:
    constrained_fields = [
        tachyon_layout.ConstrainedField(
            size=4,
            alignment=4,
            field_num=1,
        ),
        tachyon_layout.ConstrainedField(
            size=8,
            alignment=8,
            field_num=2,
        ),
        tachyon_layout.ConstrainedField(size=8, alignment=8, field_num=3),
        tachyon_layout.ConstrainedField(size=2, alignment=2, field_num=4),
        tachyon_layout.ConstrainedField(
            size=4,
            alignment=4,
            field_num=5,
        ),
        tachyon_layout.ConstrainedField(
            size=5,
            alignment=8,
            field_num=6,
        ),
    ]

    constrained_fields.sort()

    expected_order = [
        (8, 8, 2),
        (8, 8, 3),
        (5, 8, 6),
        (4, 4, 1),
        (4, 4, 5),
        (2, 2, 4),
    ]

    sorted_fields_info = [(field.size, field.alignment, field.field_num) for field in constrained_fields]

    assert sorted_fields_info == expected_order, "ConstrainedFields did not sort as expected"


def test_gap_sort() -> None:
    gaps: list[tachyon_layout.Gap] = [
        tachyon_layout.Gap(size=10, offset=0),
        tachyon_layout.Gap(size=5, offset=16),
        tachyon_layout.Gap(size=5, offset=20),
        tachyon_layout.Gap(size=10, offset=9),
        tachyon_layout.Gap(size=5, offset=15),
    ]

    expected_order = [
        (5, 15),  # align = 1
        (5, 20),  # align = 1
        (5, 16),  # align = 16
        (10, 9),  # align = 1
        (10, 0),  # align = inf
    ]

    sorted_gaps_info = [(gap.size, gap.offset) for gap in sorted(gaps)]

    assert sorted_gaps_info == expected_order, "Gaps did not sort as expected"


def test_gap_alignment() -> None:
    # Define gaps with different offsets to test alignment calculation
    gaps = [
        tachyon_layout.Gap(size=10, offset=0),  # Infinite alignment (offset 0)
        tachyon_layout.Gap(size=10, offset=2),
        tachyon_layout.Gap(size=10, offset=16),
        tachyon_layout.Gap(size=10, offset=4),
        tachyon_layout.Gap(size=10, offset=1),
        tachyon_layout.Gap(size=10, offset=8),
    ]

    # Expected alignment values for each offset
    expected_alignments: list[float] = [
        1,
        2,
        4,
        8,
        16,
        float("inf"),  # Infinite alignment for offset 0
    ]

    calculated_alignments: list[float] = [gap.alignment() for gap in sorted(gaps)]

    assert calculated_alignments == expected_alignments, "Gap alignments did not calculate as expected"


def test_layout_fields_1() -> None:
    constrained_fields: list[tachyon_layout.ConstrainedField] = [
        tachyon_layout.ConstrainedField(size=4, alignment=4, field_num=1),
        tachyon_layout.ConstrainedField(size=8, alignment=8, field_num=2),
        tachyon_layout.ConstrainedField(size=1, alignment=1, field_num=3),
        tachyon_layout.ConstrainedField(size=2, alignment=2, field_num=4),
        tachyon_layout.ConstrainedField(size=13, alignment=8, field_num=5),
    ]

    layout = tachyon_layout._layout_fields(constrained_fields)
    result_fields = [(fld.field_num, fld.offset, fld.size) for fld in layout.fields]

    expected_layout: list[tuple[int, int, int]] = [
        # elements are (field_num, offset, size)
        (5, 0, 13),
        (2, 16, 8),
        (1, 24, 4),
        (4, 14, 2),  # Fills a gap
        (3, 13, 1),  # Fills the remaining gap
    ]

    assert result_fields == expected_layout
    assert layout.gaps == [tachyon_layout.Gap(size=4, offset=28)]
    assert layout.size == 32
    assert layout.alignment == 8


def test_layout_fields_2() -> None:
    constrained_fields: list[tachyon_layout.ConstrainedField] = [
        tachyon_layout.ConstrainedField(size=5, alignment=16, field_num=1),
        tachyon_layout.ConstrainedField(size=8, alignment=32, field_num=2),
        tachyon_layout.ConstrainedField(size=1, alignment=1, field_num=3),
        tachyon_layout.ConstrainedField(size=4, alignment=2, field_num=4),
        tachyon_layout.ConstrainedField(size=13, alignment=16, field_num=5),
    ]

    layout = tachyon_layout._layout_fields(constrained_fields)
    result_fields = [(fld.field_num, fld.offset, fld.size) for fld in layout.fields]

    expected_layout: list[tuple[int, int, int]] = [
        # elements are (field_num, offset, size)
        (2, 0, 8),
        (5, 16, 13),
        (1, 32, 5),
        (4, 8, 4),  # Fills a gap
        (3, 29, 1),  # More than one place it could fit, but it fits in the smallest gap
    ]

    assert result_fields == expected_layout
    assert layout.gaps == [
        tachyon_layout.Gap(size=2, offset=30),
        tachyon_layout.Gap(size=4, offset=12),
        tachyon_layout.Gap(size=27, offset=37),
    ]
    assert layout.size == 64
    assert layout.alignment == 32


def test_layout_fields_trailing_padding() -> None:
    constrained_fields: list[tachyon_layout.ConstrainedField] = [
        tachyon_layout.ConstrainedField(size=8, alignment=8, field_num=1),
        tachyon_layout.ConstrainedField(size=1, alignment=1, field_num=2),
    ]

    layout = tachyon_layout._layout_fields(constrained_fields)
    result_fields = [(fld.field_num, fld.offset, fld.size) for fld in layout.fields]

    expected_layout: list[tuple[int, int, int]] = [
        # elements are (field_num, offset, size)
        (1, 0, 8),
        (2, 8, 1),
    ]

    assert result_fields == expected_layout
    assert layout.gaps == [
        tachyon_layout.Gap(size=7, offset=9),
    ]
    assert layout.size == 16
    assert layout.alignment == 8


def test_layout_fields_no_trailing_padding() -> None:
    constrained_fields: list[tachyon_layout.ConstrainedField] = [
        tachyon_layout.ConstrainedField(size=8, alignment=8, field_num=1),
        tachyon_layout.ConstrainedField(size=1, alignment=1, field_num=2),
        tachyon_layout.ConstrainedField(size=7, alignment=1, field_num=3),
    ]

    layout = tachyon_layout._layout_fields(constrained_fields)
    result_fields = [(fld.field_num, fld.offset, fld.size) for fld in layout.fields]

    expected_layout: list[tuple[int, int, int]] = [
        # elements are (field_num, offset, size)
        (1, 0, 8),
        (3, 8, 7),
        (2, 15, 1),
    ]

    assert result_fields == expected_layout
    assert layout.gaps == []
    assert layout.size == 16
    assert layout.alignment == 8


def test_schema_layout(fs_importer: importer.FilesystemImporter, compiler_context: CompilerContext) -> None:
    source: Final = """
// Test
schema Test
{
  fields
  {
    // 1
    #1 a: Int32;
    // 2
    #2 b: FixedArray<Byte, 7>;
    // 3
    #3 c: Uuid<Test>;
    // 4
    #4 d: VarArray<Uuid<Test>, 3>;
    // 5
    #5 e: VarArray<Byte, 1>;
    // 6
    #6 f: FixedArray<Byte, 57>;
  }
}
"""
    module_ir = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), fs_importer)
    schema_ir = module_ir.inner_scope.lookup("Test")
    assert isinstance(schema_ir, schema.Schema)
    layout = tachyon_layout.layout_schema(compiler_context, schema.InstantiatedSchema.from_typespec(schema_ir))
    assert layout.fields == [
        tachyon_layout.FieldSpan(field_num=6, offset=0, size=57),
        tachyon_layout.FieldSpan(field_num=4, offset=64, size=56),
        tachyon_layout.FieldSpan(field_num=3, offset=120, size=16),
        tachyon_layout.FieldSpan(field_num=5, offset=136, size=16),
        tachyon_layout.FieldSpan(field_num=2, offset=57, size=7),
        tachyon_layout.FieldSpan(field_num=1, offset=152, size=4),
    ]
    assert layout.gaps == [
        tachyon_layout.Gap(offset=156, size=4),
    ]


def test_schema_layout_with_const(fs_importer: importer.FilesystemImporter, compiler_context: CompilerContext) -> None:
    source: Final = """
max_bytes: UInt64 = 42;
// Test
schema Foo
{
  fields
  {
    // 1
    #1 a: VarArray<UInt32, 3>;
    // 2
    #2 b: FixedArray<Byte, max_bytes>;
    // 3
    #3 c: FixedArray<Byte, 7>;
  }
}

// Test
schema Bar
{
  fields
  {
    // 1
    #1 a: VarArray<UInt32, 3>;
    // 2
    #2 b: FixedArray<Byte, 42>;
    // 3
    #3 c: FixedArray<Byte, 7>;
  }
}
"""
    module_ir = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), fs_importer)
    foo_ir = module_ir.inner_scope.lookup("Foo")
    assert isinstance(foo_ir, schema.Schema)
    bar_ir = module_ir.inner_scope.lookup("Bar")
    assert isinstance(bar_ir, schema.Schema)
    foo_layout = tachyon_layout.layout_schema(compiler_context, schema.InstantiatedSchema.from_typespec(foo_ir))
    bar_layout = tachyon_layout.layout_schema(compiler_context, schema.InstantiatedSchema.from_typespec(bar_ir))
    assert foo_layout == bar_layout
    assert foo_layout.gaps == bar_layout.gaps
