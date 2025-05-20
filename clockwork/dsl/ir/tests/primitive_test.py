# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for the primitive module."""

import re
from decimal import Decimal
from typing import Final, cast

import pytest
from clockwork.dsl.ir import clkbuiltins, compiler, node, primitive, pubsub, typesys, units
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


class ExampleUnit(units.Unit):
    """A test unit."""

    def base_unit(self) -> str:
        """Get the base unit name, without prefix."""
        return "test"


def test_unit_conversion() -> None:
    us = primitive.UnitValue.make(Decimal(1), unit=units.MICROSECONDS)
    ns = us.as_unit(units.NANOSECONDS)
    assert ns.value == Decimal(1000)
    ms = primitive.UnitValue.make(Decimal(1), unit=units.MILLISECONDS)
    ns = ms.as_unit(units.NANOSECONDS)
    assert ns.value == Decimal(1000000)
    test_unit = ExampleUnit(value_type=clkbuiltins.DURATION, scale=0, canonical_unit=cast("units.Unit", None))
    with pytest.raises(TypeError, match="Cannot convert unit second to test"):
        ms.as_unit(test_unit)


def test_bits_to_bytes() -> None:
    bits = primitive.UnitValue.make(Decimal(1), unit=units.BIT)
    bytes_ = primitive.bits_to_bytes(bits)
    assert bytes_.unit is units.BYTE
    assert bytes_.value == Decimal("0.125")

    with pytest.raises(TypeError, match=re.escape("Expected a Bit value, but got TypeDef(name='Bytes'")):
        primitive.bits_to_bytes(primitive.UnitValue.make(Decimal(1), unit=units.BYTE))


def test_decimal_to_int() -> None:
    # Nominal signed case
    decimal_n100 = primitive.DecimalValue(clkbuiltins.UINT64, Decimal(-100))
    num_n100 = primitive.decimal_to_int(decimal_n100)
    assert isinstance(num_n100, int)
    assert num_n100 == -100

    # Nominal unsigned case
    decimal_100 = primitive.DecimalValue(clkbuiltins.UINT64, Decimal(100))
    num_100 = primitive.decimal_to_int(decimal_100, should_be_unsigned=True)
    assert isinstance(num_100, int)
    assert num_100 == 100

    # Not an integer
    decimal_99_9 = primitive.DecimalValue(clkbuiltins.FLOAT64, Decimal("99.9"))
    with pytest.raises(ValueError, match=re.escape("Expected an integer but got 99.9")):
        primitive.decimal_to_int(decimal_99_9)

    # Fail unsigned check
    decimal_n1 = primitive.DecimalValue(clkbuiltins.UINT64, Decimal(-1))
    with pytest.raises(ValueError, match=re.escape("Expected an unsigned integer but got -1")):
        primitive.decimal_to_int(decimal_n1, True)


def test_unsigned_decimal_to_int() -> None:
    # Nominal case
    decimal_100 = primitive.DecimalValue(clkbuiltins.UINT64, Decimal(100))
    num_100 = primitive.unsigned_decimal_to_int(decimal_100)
    assert isinstance(num_100, int)
    assert num_100 == 100

    # Not an unsigned value
    decimal_n1 = primitive.DecimalValue(clkbuiltins.UINT64, Decimal(-1))
    with pytest.raises(ValueError, match=re.escape("Expected an unsigned integer but got -1")):
        primitive.unsigned_decimal_to_int(decimal_n1)


def test_string_literal(fs_importer: FilesystemImporter) -> None:
    # As of the time of this writing, channel names are the only place string
    # literals are allowed syntactically. So to test this, we unfortunately
    # need to depend on the Channel implementation.
    source: Final = r"""
use clockwork::dsl::tests::support::hellomsg
// Chan1
channel Chan1
{
  name: "Chan\"1\n'";
  message_type: Tachyon<hellomsg::HelloMsg>;
  max_num_messages: 1;
}
// Chan2
channel Chan2
{
  name: 'Chan\'2\n"';
  message_type: Tachyon<hellomsg::HelloMsg>;
  max_num_messages: 1;
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "module"), fs_importer)
    chan1 = module.inner_scope.lookup("Chan1")
    chan2 = module.inner_scope.lookup("Chan2")
    assert isinstance(chan1, pubsub.Channel)
    assert isinstance(chan2, pubsub.Channel)
    assert isinstance(chan1.channel_name, primitive.StringLiteral)
    assert isinstance(chan2.channel_name, primitive.StringLiteral)
    assert chan1.channel_name.value == """Chan"1\n'"""
    assert chan2.channel_name.value == '''Chan'2\n"'''


def test_smallest_range() -> None:
    with pytest.raises(RuntimeError, match=re.escape("Internal error: 1 > -1")):
        primitive.smallest_type_to_hold_range(1, -1)
    assert primitive.smallest_type_to_hold_range(0, 0) is clkbuiltins.UINT8
    assert primitive.smallest_type_to_hold_range(0, (1 << 8) - 1) is clkbuiltins.UINT8
    assert primitive.smallest_type_to_hold_range(0, 1 << 8) is clkbuiltins.UINT16
    assert primitive.smallest_type_to_hold_range(0, (1 << 16) - 1) is clkbuiltins.UINT16
    assert primitive.smallest_type_to_hold_range(0, 1 << 16) is clkbuiltins.UINT32
    assert primitive.smallest_type_to_hold_range(0, (1 << 32) - 1) is clkbuiltins.UINT32
    assert primitive.smallest_type_to_hold_range(0, 1 << 32) is clkbuiltins.UINT64
    assert primitive.smallest_type_to_hold_range(0, (1 << 64) - 1) is clkbuiltins.UINT64
    with pytest.raises(
        ValueError, match=re.escape("No integer type is large enough to hold value 18446744073709551616")
    ):
        primitive.smallest_type_to_hold_range(0, 1 << 64)

    assert primitive.smallest_type_to_hold_range(-1, 0) is clkbuiltins.INT8
    assert primitive.smallest_type_to_hold_range(-1, (1 << 7) - 1) is clkbuiltins.INT8
    assert primitive.smallest_type_to_hold_range(-(1 << 7), (1 << 7) - 1) is clkbuiltins.INT8
    assert primitive.smallest_type_to_hold_range(-1, 1 << 7) is clkbuiltins.INT16
    assert primitive.smallest_type_to_hold_range(-1, (1 << 15) - 1) is clkbuiltins.INT16
    assert primitive.smallest_type_to_hold_range(-(1 << 15), (1 << 15) - 1) is clkbuiltins.INT16
    assert primitive.smallest_type_to_hold_range(-1, 1 << 15) is clkbuiltins.INT32
    assert primitive.smallest_type_to_hold_range(-1, (1 << 31) - 1) is clkbuiltins.INT32
    assert primitive.smallest_type_to_hold_range(-(1 << 31), (1 << 31) - 1) is clkbuiltins.INT32
    assert primitive.smallest_type_to_hold_range(-1, 1 << 31) is clkbuiltins.INT64
    assert primitive.smallest_type_to_hold_range(-1, (1 << 63) - 1) is clkbuiltins.INT64
    assert primitive.smallest_type_to_hold_range(-(1 << 63), (1 << 63) - 1) is clkbuiltins.INT64
    with pytest.raises(
        ValueError, match=re.escape("No integer type is large enough to hold range [-1, 9223372036854775808]")
    ):
        primitive.smallest_type_to_hold_range(-1, 1 << 63)


def test_value_to_bool() -> None:
    # Nominal case
    assert primitive.value_to_bool(clkbuiltins.TRUE_VALUE) is True
    assert primitive.value_to_bool(clkbuiltins.FALSE_VALUE) is False

    # Not a bool
    scope = node.Scope(parent=None, uniq_path="", module_id_for_errors=None)
    false_false = typesys.NamedValue(scope=scope, name="false", type_info=clkbuiltins.BOOL)
    with pytest.raises(ValueError, match=re.escape("Expected an bool but got NamedValue(name='false')")):
        primitive.value_to_bool(false_false)
