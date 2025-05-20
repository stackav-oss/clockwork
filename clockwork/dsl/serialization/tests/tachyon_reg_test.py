# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for tachyon_reg module."""

from typing import Final
from unittest.mock import MagicMock

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir import clkbuiltins, clkenum, compiler, importer, typesys
from clockwork.dsl.serialization import tachyon_reg


@pytest.fixture()
def context() -> CompilerContext:
    """Create a compiler context for tests."""
    return CompilerContext()


@pytest.fixture()
def fs_importer() -> importer.FilesystemImporter:
    return importer.FilesystemImporter(compile_fn=compiler.compile_source_file)


@pytest.mark.parametrize(
    ("size", "alignment", "expected_stride"),
    [
        (1, 1, 1),
        (4, 1, 4),
        (1, 4, 4),
        (8, 4, 8),
        (4, 8, 8),
        (8, 8, 8),
        (11, 8, 16),  # Checks rounding
        (16, 8, 16),
        (32, 16, 32),
        (320, 16, 320),
    ],
)
def test_field_constraint_array_stride(size: int, alignment: int, expected_stride: int) -> None:
    constraint = tachyon_reg.FieldConstraint(size=size, alignment=alignment)
    assert constraint.array_stride() == expected_stride


def test_constraint_for_type_not_found(context: CompilerContext) -> None:
    assert tachyon_reg.constraint_for_type(context, clkbuiltins.TYPE_TYPE) is None


def test_constraint_for_type_found(context: CompilerContext) -> None:
    typ = clkbuiltins.INT32
    constraint = tachyon_reg.FieldConstraint(size=4, alignment=4)
    assert tachyon_reg.constraint_for_type(context, typ) == constraint


def test_constraint_for_enum(context: CompilerContext) -> None:
    values: dict[int, clkenum.ValueDef] = {}
    typ = clkenum.ClkEnum(
        doc=MagicMock(),
        module=MagicMock(),
        cst_node=MagicMock(),
        name="TestEnum",
        scope=MagicMock(),
        inner_scope=MagicMock(),
        type_info=MagicMock(),
        uuid=None,
        values=values,
        default_field_num=1,
        bit_flags=False,
        underlying_type=clkbuiltins.UINT8,
        resolved=None,
        history=None,
        linter_overrides=set(),
        has_explicit_underlying_type=False,
    )
    value = clkenum.ValueDef(
        doc=MagicMock(),
        module=MagicMock(),
        cst_node=MagicMock(),
        name="TestValue",
        scope=MagicMock(),
        enum=typ,
        field_num=1,
        is_default=True,
        integer_value=0,
        resolved=None,
    )
    values[1] = value
    value.resolved = clkenum.ResolvedValueDef(
        doc=value.doc,
        module=value.module,
        cst_node=value.cst_node,
        name=value.name,
        scope=value.scope,
        enum=typ,
        field_num=1,
        is_default=True,
        integer_value=0,
        source=value,
        value_is_explicit=False,
    )
    typ.resolved = clkenum.ResolvedEnum(
        doc=typ.doc,
        name=typ.name,
        scope=typ.scope,
        module=typ.module,
        cst_node=typ.cst_node,
        type_info=typ.type_info,
        inner_scope=typ.inner_scope,
        uuid=typ.uuid,
        values={1: value.get_resolved()},
        default_field_num=typ.default_field_num,
        bit_flags=False,
        underlying_type=clkbuiltins.UINT8,
        source=typ,
        history=clkenum.ResolvedEnumHistory(
            versions=[],
            pseudoversions=[],
            values={},
            options=None,
        ),
        linter_overrides=set(),
        has_explicit_values=False,
        has_explicit_underlying_type=False,
    )
    constraint = tachyon_reg.FieldConstraint(size=1, alignment=1)
    assert tachyon_reg.constraint_for_type(context, typ.resolved) == constraint


def test_constraint_for_uuid(context: CompilerContext) -> None:
    typ = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE, instantiates=clkbuiltins.UUID, arguments={"tag": clkbuiltins.TYPE_TYPE}
    )
    constraint = tachyon_reg.FieldConstraint(size=16, alignment=8)
    assert tachyon_reg.constraint_for_type(context, typ) == constraint


def test_register_type_raises_for_generic(context: CompilerContext) -> None:
    with pytest.raises(RuntimeError):
        tachyon_reg.register_type(context, clkbuiltins.FIXED_ARRAY, tachyon_reg.FieldConstraint(size=4, alignment=4))


def test_generic_type(context: CompilerContext) -> None:
    generic_type = typesys.GenericTypeDef(
        scope=clkbuiltins.BUILTINS_SCOPE,
        name="Test",
        parameters=(typesys.Parameter(name="test", type_bound=clkbuiltins.TYPE_TYPE, default=None),),
        type_info=clkbuiltins.TYPE_TYPE,
    )
    executed = False
    expected_result: Final = tachyon_reg.FieldConstraint(size=1, alignment=1)

    def factory(ctx: CompilerContext | None, typ: typesys.Instantiation) -> tachyon_reg.FieldConstraint:
        nonlocal executed
        executed = True
        assert ctx is context  # Verify context is properly passed through
        assert typ.instantiates is generic_type
        return expected_result

    tachyon_reg.register_generic_type(context, generic_type, factory)
    result = tachyon_reg.constraint_for_type(
        context,
        typesys.Instantiation(
            type_info=clkbuiltins.TYPE_TYPE, instantiates=generic_type, arguments={"test": clkbuiltins.INT16}
        ),
    )
    assert result == expected_result
    assert executed


def test_register_type_with_conflicting_constraint(context: CompilerContext) -> None:
    typ = clkbuiltins.INT8
    new_constraint = tachyon_reg.FieldConstraint(size=4, alignment=4)
    with pytest.raises(RuntimeError):
        tachyon_reg.register_type(context, typ, new_constraint)


@pytest.mark.parametrize(
    ("value_type", "expected_alignment", "expected_size"),
    [
        (clkbuiltins.UINT8, 1, 2),
        (clkbuiltins.UINT16, 2, 4),
        (clkbuiltins.UINT32, 4, 8),
        (clkbuiltins.UINT64, 8, 16),
    ],
)
def test_field_constraint_optional(
    context: CompilerContext, value_type: typesys.TypeDef, expected_alignment: int, expected_size: int
) -> None:
    result = tachyon_reg.constraint_for_type(
        context,
        typesys.Instantiation(
            type_info=clkbuiltins.TYPE_TYPE, instantiates=clkbuiltins.OPTIONAL, arguments={"type": value_type}
        ),
    )

    expected: Final = tachyon_reg.FieldConstraint(size=expected_size, alignment=expected_alignment)
    assert result == expected
