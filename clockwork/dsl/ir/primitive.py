# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""IR Nodes dealing with primitive data types and values."""

from __future__ import annotations

from ast import literal_eval
from dataclasses import dataclass
from typing import TYPE_CHECKING, Final

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.ir import clkbuiltins, node, typesys, units
from clockwork.dsl.ir.cst_util import decimal_from_cst, get_span
from typing_extensions import override

if TYPE_CHECKING:
    from decimal import Decimal


@dataclass
class Literal(typesys.Value, node.CstNode[cst.Literal | cst.Number | cst.Integer | cst.NonnegativeInteger]):
    """Base class for literal values from Clockwork source."""

    @classmethod
    def from_cst(cls: type[Literal], cst_node: cst.Literal, module: node.Module) -> Literal:
        """Construct a Literal IR node from a CST node."""
        if (unit_literal := cst_node.maybe_unit_literal()) is not None:
            return UnitLiteral.from_child_cst(unit_literal, cst_node, module)
        if (number := cst_node.maybe_number()) is not None:
            return DecimalLiteral.from_child_cst(number, cst_node, module)
        if (string := cst_node.maybe_string_literal()) is not None:
            return StringLiteral.from_child_cst(string, cst_node, module)
        msg = f"Literal type not implemented: {cst_node}"
        raise NotImplementedError(msg)


@dataclass
class UnitValue(typesys.Value):
    """A decimal value with an attached unit."""

    value: Decimal
    unit: units.Unit

    @classmethod
    def make(cls: type[UnitValue], value: Decimal, unit: units.Unit) -> UnitValue:
        """Factory method to construct a UnitValue.

        Args:
            value: The value
            unit: The unit of the value
        """
        return cls(type_info=unit.value_type, value=value, unit=unit)

    def as_unit(self, unit: units.Unit) -> UnitValue:
        """Convert to a different, compatible unit."""
        if self.unit == unit:
            return self
        if unit.base_unit() != self.unit.base_unit():
            msg = f"Cannot convert unit {self.unit.base_unit()} to {unit.base_unit()}"
            raise TypeError(msg)
        magnitude_shift = self.unit.scale - unit.scale
        return UnitValue.make(unit=unit, value=self.value.scaleb(magnitude_shift))

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        canonical = self.as_unit(self.unit.canonical_unit)
        return f"{canonical.value}{canonical.unit.base_unit()}"


@dataclass
class UnitLiteral(Literal, UnitValue):
    """A literal numeric value with units from Clockwork source."""

    @classmethod
    def from_child_cst(
        cls: type[UnitLiteral],
        unit_literal: cst.UnitLiteral,
        parent_cst: cst.Literal,
        module: node.Module,
    ) -> UnitLiteral:
        """Construct an IR UnitLiteral from a CST UnitLiteral."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        value = decimal_from_cst(unit_literal.child_number(), module.terminals)
        unit = units.Unit.from_cst(unit_literal.child_unit_identifier(), module)
        return cls(module=module, cst_node=parent_cst, value=value, unit=unit, type_info=unit.value_type)


def bits_to_bytes(bits: UnitValue) -> UnitValue:
    """Convert a bits value to bytes."""
    if bits.type_info is not clkbuiltins.BITS:
        msg = f"Expected a Bit value, but got {bits.type_info}"
        raise TypeError(msg)
    bits = bits.as_unit(units.BIT)
    return UnitValue(type_info=clkbuiltins.BYTES, value=bits.value / 8, unit=units.BYTE)


@dataclass
class DecimalValue(typesys.Value):
    """A decimal value."""

    value: Decimal

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        return str(self.value)


@dataclass
class DecimalLiteral(Literal, DecimalValue):
    """A literal decimal value from Clockwork source."""

    @classmethod
    def from_child_cst(
        cls: type[DecimalLiteral],
        cst_node: cst.Number | cst.NonnegativeInteger | cst.Integer,
        parent_cst: cst.Literal | None,
        module: node.Module,
    ) -> DecimalLiteral:
        """Construct an IR UnitLiteral from a CST UnitLiteral."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        value = decimal_from_cst(cst_node, module.terminals)
        numeric_type = (
            typesys.NumericType.FLOAT
            if isinstance(cst_node, cst.Number) and cst_node.maybe_fractional_part() is not None
            else (typesys.NumericType.INTEGER if value >= 0 else typesys.NumericType.SIGNED_INTEGER)
        )
        return cls(
            module=module,
            cst_node=parent_cst or cst_node,
            value=value,
            type_info=typesys.InferenceVar.make(context=module, cst_node=cst_node, numeric_type=numeric_type),
        )


def decimal_to_int(decimal: DecimalValue, should_be_unsigned: bool = False) -> int:
    """Convert DecimalValue to Python numeric integer.

    Checks that the DecimalValue represents an integer.

    If should_be_unsigned is True, function will verify that the DecimalValue represents an unsigned integer.

    Args:
        decimal: DecimalValue to convert
        should_be_unsigned: Verifies the DecimalValue contains unsigned value

    Returns:
        Python numeric integer

    Raises:
        ValueError: If DecimalValue is not an integer
        ValueError: If should_be_unsigned=True and DecimalValue is a negative number
    """
    int_result = int(decimal.value)
    if int_result != decimal.value:
        msg = f"Expected an integer but got {decimal.value}"
        raise ValueError(msg)

    if should_be_unsigned and int_result < 0:
        msg = f"Expected an unsigned integer but got {decimal.value}"
        raise ValueError(msg)
    return int_result


def unsigned_decimal_to_int(decimal: DecimalValue) -> int:
    """Converts unsigned DecimalValue-based Expression to integer.

    Function verifies that the input value is an unsigned integer.

    Args:
        decimal: DecimalValue to convert

    Returns:
        Python numeric integer

    Raises:
        ValueError: If DecimalValue is a negative number
    """
    return decimal_to_int(decimal, True)


@dataclass
class StringValue(typesys.Value):
    """A string value."""

    value: str

    @classmethod
    def make(cls: type[StringValue], value: str) -> StringValue:
        """Construct a StringValue from a Python string."""
        return cls(type_info=clkbuiltins.STRING, value=value)

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        return self.value


@dataclass
class StringLiteral(Literal, StringValue):
    """A literal string from Clockwork source."""

    @classmethod
    def from_child_cst(
        cls: type[StringLiteral],
        cst_node: cst.StringLiteral,
        parent_cst: cst.Literal,
        module: node.Module,
    ) -> StringLiteral:
        """Construct an IR StringLiteral from a CST StringLiteral."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        literal_str = get_span(cst_node.child_value(), module.terminals)
        value = literal_eval(literal_str)
        return cls(
            module=module,
            cst_node=parent_cst,
            value=value,
            type_info=clkbuiltins.STRING,
        )


@dataclass
class IPv4Address(node.CstNode[cst.Ipv4Address], StringValue):
    """A literal IPv4 address from Clockwork source."""

    @classmethod
    def from_cst(
        cls: type[IPv4Address],
        cst_node: cst.Ipv4Address,
        module: node.Module,
    ) -> IPv4Address:
        """Construct an IR IPv4Address from a CST Literal."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        raw_octets = [cst_node.child_first(), cst_node.child_second(), cst_node.child_third(), cst_node.child_fourth()]
        octets = tuple(int(get_span(octet.child_value(), module.terminals)) for octet in raw_octets)
        max_octet: Final = 255
        if any(octet > max_octet for octet in octets):
            msg = node.append_error_line(
                cst_node,
                module,
                f"Octet in IPv4 address must be in range [0, 255].  Received: {'.'.join(map(str, octets))}",
            )
            raise ValueError(msg)
        return cls(
            module=module,
            cst_node=cst_node,
            value=".".join(map(str, octets)),
            type_info=clkbuiltins.IPV4_ADDRESS,
        )


def smallest_type_to_hold_range(min_value: int, max_value: int) -> clkbuiltins.IntegerPrimitiveType:
    """Determine an IntegerPrimitiveType which can hold any value in a range (inclusive)."""
    signed_types: Final = [
        clkbuiltins.INT8,
        clkbuiltins.INT16,
        clkbuiltins.INT32,
        clkbuiltins.INT64,
    ]
    unsigned_types: Final = [
        clkbuiltins.UINT8,
        clkbuiltins.UINT16,
        clkbuiltins.UINT32,
        clkbuiltins.UINT64,
    ]

    if min_value > max_value:
        msg = f"Internal error: {min_value} > {max_value}"
        raise RuntimeError(msg)

    if min_value >= 0:
        for typ in unsigned_types:
            if max_value < 2**typ.bit_width:
                return typ
        msg = f"No integer type is large enough to hold value {max_value}"
        raise ValueError(msg)
    for typ in signed_types:
        if min_value >= -(2 ** (typ.bit_width - 1)) and max_value < 2 ** (typ.bit_width - 1):
            return typ
    msg = f"No integer type is large enough to hold range [{min_value}, {max_value}]"
    raise ValueError(msg)


def value_to_bool(value: typesys.Value) -> bool:
    """Converts a NamedValue to a bool value.

    Function verifies that NamedValue is a known bool type

    Args:
        value: NamedValue to check and convert

    Returns:
        Python bool (True or False)

    Raises:
        ValueError: If NamedValue is not TRUE_VALUE or FALSE_VALUE
    """
    if value is clkbuiltins.TRUE_VALUE:
        return True
    if value is clkbuiltins.FALSE_VALUE:
        return False
    msg = f"Expected an bool but got {value}"
    raise ValueError(msg)
