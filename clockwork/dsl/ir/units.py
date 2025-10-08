# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""IR Nodes dealing with primitive data types and values."""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import TYPE_CHECKING, Final, cast

from clockwork.dsl import cst
from clockwork.dsl.ir import clkbuiltins, typesys
from clockwork.dsl.ir.cst_util import get_span
from typing_extensions import override

if TYPE_CHECKING:
    from clockwork.dsl.ir import node


@dataclass
class Unit(ABC):
    """Base class for Clockwork units.

    Note: The scale attribute is given as a power of ten, which specifies the
    factor to convert from the base unit to this unit.  Therefore if this unit
    is smaller than the base unit it will be negative.  So for example "milli-"
    is -3, and "kilo-" is +3.

    Attributes:
        value_type: The type of this value scale: The scaling factor/prefix as a
        power of 10; this is only the exponent. (See note.)
    """

    value_type: typesys.TypeDef
    scale: int
    canonical_unit: Unit

    @abstractmethod
    def base_unit(self) -> str:
        """Get the base unit name, without prefix."""
        ...

    @classmethod
    def from_cst(cls: type[Unit], cst_node: cst.UnitIdentifier, module: node.Module) -> Unit:
        """Create a Unit from a CST UnitIdentifier."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        try:
            return CST_MAP[cst_node.child()[0]]  # pyright: ignore[reportArgumentType] Assuming child node label exists
        except KeyError:
            pass
        msg = f"Unit {get_span(cst_node.span, module.terminals)} not implemented."
        raise NotImplementedError(msg)


@dataclass
class TimeUnit(Unit):
    """A time unit."""

    def __init__(self, scale: int, canonical_unit: TimeUnit) -> None:
        """Create TimeUnit."""
        super().__init__(value_type=clkbuiltins.DURATION, scale=scale, canonical_unit=canonical_unit)

    @override
    def base_unit(self) -> str:
        """Return the base unit name "second"."""
        return "second"


SECONDS: Final = TimeUnit(scale=0, canonical_unit=cast("TimeUnit", None))
SECONDS.canonical_unit = SECONDS
MILLISECONDS: Final = TimeUnit(scale=-3, canonical_unit=SECONDS)
MICROSECONDS: Final = TimeUnit(scale=-6, canonical_unit=SECONDS)
NANOSECONDS: Final = TimeUnit(scale=-9, canonical_unit=SECONDS)


@dataclass
class BytesUnit(Unit):
    """A unit of bytes."""

    def __init__(self, scale: int, canonical_unit: BytesUnit) -> None:
        """Create BytesUnit."""
        super().__init__(value_type=clkbuiltins.BYTES, scale=scale, canonical_unit=canonical_unit)

    @override
    def base_unit(self) -> str:
        """Return the base unit name "byte"."""
        return "byte"


BYTE: Final = BytesUnit(scale=0, canonical_unit=cast("BytesUnit", None))
BYTE.canonical_unit = BYTE


@dataclass
class BitsUnit(Unit):
    """A unit of bits.

    NB: bits are implicitly convertible to bytes, unlike most units.
    """

    def __init__(self, scale: int, canonical_unit: BitsUnit) -> None:
        """Create BitsUnit."""
        super().__init__(value_type=clkbuiltins.BITS, scale=scale, canonical_unit=canonical_unit)

    @override
    def base_unit(self) -> str:
        """Return the base unit name "bit"."""
        return "bit"


BIT: Final = BitsUnit(scale=0, canonical_unit=cast("BitsUnit", None))
BIT.canonical_unit = BIT

CST_MAP: Final = {
    cst.UnitIdentifier.Label.BIT: BIT,
    cst.UnitIdentifier.Label.BYTE: BYTE,
    cst.UnitIdentifier.Label.SECONDS: SECONDS,
    cst.UnitIdentifier.Label.MILLISECONDS: MILLISECONDS,
    cst.UnitIdentifier.Label.MICROSECONDS: MICROSECONDS,
}
