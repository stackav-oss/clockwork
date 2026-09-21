# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for tachyon_reg module."""

from decimal import Decimal
from typing import Final
from unittest.mock import MagicMock

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir import clkbuiltins, clkenum, compiler, importer, primitive, schema, typesys
from clockwork.dsl.ir.module_id import ModuleID
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
        attributes=None,
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
        history=clkenum.EnumHistory(version=1, legacy_became={}, removed=set()),
        attributes=None,
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


@pytest.mark.parametrize(
    ("bit_size", "expected_size"),
    [(1, 1), (8, 1), (9, 2), (64, 8), (65, 9)],
)
def test_constraint_for_bitset(context: CompilerContext, bit_size: int, expected_size: int) -> None:
    bitset = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.BITSET,
        arguments={"size": primitive.DecimalValue(type_info=clkbuiltins.UINT64, value=Decimal(bit_size))},
    )
    assert tachyon_reg.constraint_for_type(context, bitset) == tachyon_reg.FieldConstraint(
        size=expected_size, alignment=1
    )


def test_constraint_for_zero_size_bitset_is_rejected(context: CompilerContext) -> None:
    bitset = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.BITSET,
        arguments={"size": primitive.DecimalValue(type_info=clkbuiltins.UINT64, value=Decimal(0))},
    )
    with pytest.raises(ValueError, match="Bitset size must be greater than zero"):
        tachyon_reg.constraint_for_type(context, bitset)


def test_zero_size_bitset_schema_is_rejected(fs_importer: importer.FilesystemImporter) -> None:
    source = """
// Schema with an invalid bitset.
schema InvalidBitset
{
  fields
  {
    // Bits.
    #0 bits: Bitset<0>;
  }
}
"""
    with pytest.raises(ValueError, match="Bitset size must be greater than zero"):
        compiler.compile_source_text(source, ModuleID("test", "invalid_bitset"), fs_importer)


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


def test_soa_size_field_uint8() -> None:
    """Test that VarSoa with max_size <= 255 uses UInt8 for size field."""
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = """
    // Point3f
    schema Point3f
    {
      options
      {
        soa_enabled: true;
      }
      fields
      {
        // X coordinate
        #1 x: Float32;
        // Y coordinate
        #2 y: Float32;
        // Z coordinate
        #3 z: Float32;
      }
    }
    """
    module = compiler.compile_source_text(source, ModuleID("test", "point3f"), importer=fs_importer)
    point3f_schema = module.inner_scope.lookup("Point3f")
    assert isinstance(point3f_schema, schema.Schema)
    point3f_resolved = point3f_schema.get_resolved()

    # VarSoa<Point3f, 100>: should use UInt8 size field (1 byte)
    var_soa_100 = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.VAR_SOA,
        arguments={
            "type": schema.InstantiatedSchema.from_typespec(point3f_resolved),
            "max_size": primitive.DecimalValue(clkbuiltins.UINT64, Decimal(100)),
        },
    )
    constraint = tachyon_reg.constraint_for_type(module.context, var_soa_100)
    assert constraint is not None
    # 3 fields * 100 elements * 4 bytes = 1200 bytes + 1 byte size field = 1201 bytes
    # Aligned to max alignment (4): 1204 bytes
    assert constraint.size == 1204
    assert constraint.alignment == 4


def test_soa_size_field_uint16() -> None:
    """Test that VarSoa with 256 <= max_size <= 65535 uses UInt16 for size field."""
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = """
    // Point3f
    schema Point3f
    {
      options
      {
        soa_enabled: true;
      }
      fields
      {
        // X coordinate
        #1 x: Float32;
        // Y coordinate
        #2 y: Float32;
        // Z coordinate
        #3 z: Float32;
      }
    }
    """
    module = compiler.compile_source_text(source, ModuleID("test", "point3f"), importer=fs_importer)
    point3f_schema = module.inner_scope.lookup("Point3f")
    assert isinstance(point3f_schema, schema.Schema)
    point3f_resolved = point3f_schema.get_resolved()

    # VarSoa<Point3f, 1000>: should use UInt16 size field (2 bytes)
    var_soa_1000 = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.VAR_SOA,
        arguments={
            "type": schema.InstantiatedSchema.from_typespec(point3f_resolved),
            "max_size": primitive.DecimalValue(clkbuiltins.UINT64, Decimal(1000)),
        },
    )
    constraint = tachyon_reg.constraint_for_type(module.context, var_soa_1000)
    assert constraint is not None
    # 3 fields * 1000 elements * 4 bytes = 12000 bytes
    # + 2 byte UInt16 size field = 12002 bytes
    # Aligned to max alignment (4): 12004 bytes
    assert constraint.size == 12004
    assert constraint.alignment == 4


def test_soa_size_field_uint32() -> None:
    """Test that VarSoa with 65536 <= max_size <= 4294967295 uses UInt32 for size field."""
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = """
    // Point3f
    schema Point3f
    {
      options
      {
        soa_enabled: true;
      }
      fields
      {
        // X coordinate
        #1 x: Float32;
        // Y coordinate
        #2 y: Float32;
        // Z coordinate
        #3 z: Float32;
      }
    }
    """
    module = compiler.compile_source_text(source, ModuleID("test", "point3f"), importer=fs_importer)
    point3f_schema = module.inner_scope.lookup("Point3f")
    assert isinstance(point3f_schema, schema.Schema)
    point3f_resolved = point3f_schema.get_resolved()

    # VarSoa<Point3f, 100000>: should use UInt32 size field (4 bytes)
    var_soa_100000 = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.VAR_SOA,
        arguments={
            "type": schema.InstantiatedSchema.from_typespec(point3f_resolved),
            "max_size": primitive.DecimalValue(clkbuiltins.UINT64, Decimal(100000)),
        },
    )
    constraint = tachyon_reg.constraint_for_type(module.context, var_soa_100000)
    assert constraint is not None
    # 3 fields * 100000 elements * 4 bytes = 1200000 bytes
    # Aligned to 4 for UInt32 size field: 1200000 bytes (already aligned)
    # + 4 byte size field = 1200004 bytes
    # Aligned to max alignment (4): 1200004 bytes (already aligned)
    assert constraint.size == 1200004
    assert constraint.alignment == 4


def test_soa_field_alignment() -> None:
    """Test that SoA properly aligns fields with different alignment requirements."""
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = """
    // MixedAlignment - fields intentionally out of alignment order
    schema MixedAlignment
    {
      options
      {
        soa_enabled: true;
      }
      fields
      {
        // 2-byte field first
        #1 small: UInt16;
        // 8-byte field second
        #2 large: Int64;
        // 1-byte field third
        #3 tiny: UInt8;
        // 4-byte field fourth
        #4 medium: Int32;
      }
    }
    """
    module = compiler.compile_source_text(source, ModuleID("test", "mixed"), importer=fs_importer)
    mixed_schema = module.inner_scope.lookup("MixedAlignment")
    assert isinstance(mixed_schema, schema.Schema)
    mixed_resolved = mixed_schema.get_resolved()

    # FixedSoa<MixedAlignment, 7> - using odd size to force padding
    # Fields sorted by alignment (descending): large (8), medium (4), small (2), tiny (1)
    # large: 7 * 8 = 56 bytes (starts at 0)
    # medium: 7 * 4 = 28 bytes (starts at 56, already 4-byte aligned)
    # small: 7 * 2 = 14 bytes (starts at 84, already 2-byte aligned)
    # tiny: 7 * 1 = 7 bytes (starts at 98, no alignment needed)
    # Total: 105 bytes, aligned to max (8)
    fixed_soa = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.FIXED_SOA,
        arguments={
            "type": schema.InstantiatedSchema.from_typespec(mixed_resolved),
            "size": primitive.DecimalValue(clkbuiltins.UINT64, Decimal(7)),
        },
    )
    constraint = tachyon_reg.constraint_for_type(module.context, fixed_soa)
    assert constraint is not None
    # Total: 105 bytes, aligned to max alignment (8): 112 bytes
    assert constraint.size == 112
    assert constraint.alignment == 8


def test_soa_field_alignment_with_padding() -> None:
    """Test SoA with fields that require inter-field padding due to misalignment."""
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = """
    // RequiresPadding
    schema RequiresPadding
    {
      options
      {
        soa_enabled: true;
      }
      fields
      {
        // 1-byte field first
        #1 byte_field: UInt8;
        // 8-byte field second
        #2 big_field: Int64;
        // 2-byte field third
        #3 word_field: UInt16;
        // 4-byte field fourth
        #4 dword_field: Int32;
      }
    }
    """
    module = compiler.compile_source_text(source, ModuleID("test", "padding"), importer=fs_importer)
    requires_padding_schema = module.inner_scope.lookup("RequiresPadding")
    assert isinstance(requires_padding_schema, schema.Schema)
    requires_padding_resolved = requires_padding_schema.get_resolved()

    # VarSoa<RequiresPadding, 65537> - Forcing padding before size field
    # Fields sorted by alignment (descending): big_field (8), dword_field (4), word_field (2), byte_field (1)
    # big_field: 65537 * 8 = 524296 bytes (starts at 0)
    # dword_field: 65537 * 4 = 262148 bytes (starts at 524296, already 4-byte aligned)
    # word_field: 65537 * 2 = 131074 bytes (starts at 786444, already 2-byte aligned)
    # byte_field: 65537 * 1 = 65537 bytes (starts at 917518, no alignment needed)
    # Total SoA data: 983055 bytes
    # Align to 4 for UInt32 size field: 983056 bytes (needs 1 byte padding)
    # + 4 byte UInt32 size field = 983060 bytes
    fixed_soa = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.VAR_SOA,
        arguments={
            "type": schema.InstantiatedSchema.from_typespec(requires_padding_resolved),
            "max_size": primitive.DecimalValue(clkbuiltins.UINT64, Decimal(65537)),
        },
    )
    constraint = tachyon_reg.constraint_for_type(module.context, fixed_soa)
    assert constraint is not None
    # Total: 983060 bytes, aligned to max alignment (8): 983064 bytes
    assert constraint.size == 983064
    assert constraint.alignment == 8


def test_empty_soa() -> None:
    """Test that SoA with size 0 has minimal constraint."""
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = """
    // Point3f
    schema Point3f
    {
      options
      {
        soa_enabled: true;
      }
      fields
      {
        // X coordinate
        #1 x: Float32;
        // Y coordinate
        #2 y: Float32;
        // Z coordinate
        #3 z: Float32;
      }
    }
    """
    module = compiler.compile_source_text(source, ModuleID("test", "point3f"), importer=fs_importer)
    point3f_schema = module.inner_scope.lookup("Point3f")
    assert isinstance(point3f_schema, schema.Schema)
    point3f_resolved = point3f_schema.get_resolved()

    # FixedSoa<Point3f, 0>
    fixed_soa_empty = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.FIXED_SOA,
        arguments={
            "type": schema.InstantiatedSchema.from_typespec(point3f_resolved),
            "size": primitive.DecimalValue(clkbuiltins.UINT64, Decimal(0)),
        },
    )
    constraint = tachyon_reg.constraint_for_type(module.context, fixed_soa_empty)
    assert constraint is not None
    assert constraint.size == 0
    assert constraint.alignment == 1
