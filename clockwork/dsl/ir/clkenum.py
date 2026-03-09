# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Enum-related IR nodes.

This is named clkenum to avoid conflicts with Python's enum standard library module.
"""

from __future__ import annotations

import itertools
import uuid
from dataclasses import dataclass, field
from typing import TYPE_CHECKING

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.ir import clkbuiltins, expr, node, primitive, typesys
from clockwork.dsl.ir.cst_util import get_span, int_from_cst
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Container


@dataclass
class EnumHistory:
    """Track historical information about an enum."""

    version: int
    legacy_became: dict[int, int]
    removed: set[int]

    @classmethod
    def from_cst(
        cls: type[EnumHistory],
        cst_node: cst.EnumHistoryBlock,
        module: node.Module,
    ) -> EnumHistory:
        """Create an IR EnumHistory from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        # Parse version
        version_cst = cst_node.child_version_spec()
        version = int_from_cst(version_cst.child_version(), module.terminals)

        # Parse legacy_became if present
        legacy_became = {}
        new_numbers = set()
        if legacy_became_spec_cst := cst_node.maybe_legacy_became_spec():
            for became_spec_cst in legacy_became_spec_cst.children_became_spec():
                old_number_cst = became_spec_cst.child_old_number()
                old_number = int_from_cst(old_number_cst, module.terminals)
                if old_number in legacy_became:
                    msg = node.append_error_line(
                        old_number_cst, module, f"Duplicate old field number {old_number} in legacy became block"
                    )
                    raise ValueError(msg)
                new_number_cst = became_spec_cst.child_new_number()
                new_number = int_from_cst(new_number_cst, module.terminals)
                if new_number in new_numbers:
                    msg = node.append_error_line(
                        new_number_cst, module, f"Duplicate new field number {new_number} in legacy became block"
                    )
                    raise ValueError(msg)
                legacy_became[old_number] = new_number
                new_numbers.add(new_number)

        # Parse removed if present
        removed = set()
        if removed_spec_cst := cst_node.maybe_removed_spec():
            for value_num_cst in removed_spec_cst.children_num():
                value_num = int_from_cst(value_num_cst, module.terminals)
                if value_num in legacy_became:
                    msg = node.append_error_line(
                        value_num_cst, module, f"Value number {value_num} is in both legacy_became and removed"
                    )
                    raise ValueError(msg)
                removed.add(value_num)
        return cls(version=version, legacy_became=legacy_became, removed=removed)


@dataclass
class ResolvedEnum(typesys.TypeDef, node.CstNode[cst.Enum], node.DocRequiredEntity, node.NamespaceEntity):
    """Resolved version of a Clockwork enum."""

    inner_scope: node.Scope = field(repr=False)
    uuid: uuid.UUID | None
    values: dict[int, ResolvedValueDef]
    default_field_num: int
    bit_flags: bool
    underlying_type: clkbuiltins.IntegerPrimitiveType
    has_explicit_underlying_type: bool  # True if underlying type was explicitly assigned
    has_explicit_values: bool  # True if enum values were explicitly assigned
    linter_overrides: set[str]
    history: EnumHistory = field(repr=False)
    attributes: node.ClkAttributes | None = field(repr=False)
    source: ClkEnum | None = field(repr=False)

    def cur_version(self) -> int:
        """Get the current version of the enum."""
        return self.history.version

    def validate_version(self, version: int) -> None:
        """Validate that the requested version exists in history.

        Args:
            version: Version to validate

        Raises:
            ValueError: If version is not in the enum history
        """
        if version > self.history.version:
            msg = self.append_error_line(
                f"Version {version} is greater than current enum version {self.history.version}"
            )
            raise ValueError(msg)


@dataclass
class ClkEnum(typesys.TypeDef, node.CstNode[cst.Enum], node.DocRequiredEntity, node.NamespaceEntity):
    """A Clockwork enum.

    This is named "ClkEnum" to prevent name conflicts with Python's enum.Enum.
    """

    inner_scope: node.Scope = field(repr=False)
    uuid: uuid.UUID | None
    values: dict[int, ValueDef]
    default_field_num: int
    bit_flags: bool
    underlying_type: clkbuiltins.IntegerPrimitiveType | None | expr.Expr
    has_explicit_underlying_type: bool
    linter_overrides: set[str]
    history: EnumHistory | None = field(repr=False)
    attributes: node.ClkAttributes | None = field(repr=False)
    resolved: ResolvedEnum | None = field(repr=False)

    @override
    def lookup(self, name: str) -> node.NamedEntity | None:
        """Look up a value within this enum.

        Returns:
            The entity with that name, or None if not found.
        """
        return self.inner_scope.lookup(name, recursive=False)

    # We must disable C901 and PLR0912 here (function complexity, branches) because
    # we inherently have many branches, one for each type of module-level entity.
    # However, they're handled in a uniform way that isn't difficult to understand.
    @classmethod
    def from_cst(cls: type[ClkEnum], cst_node: cst.Enum, module: node.Module, scope: node.Scope) -> ClkEnum:  # noqa: C901, PLR0912
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.from_cst(cst_node.child_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        attributes = module.handle_outer_attrs(cst_node.maybe_clk_outer_attrs())
        uuid_val = (
            uuid.UUID(hex=get_span(uuid_spec.child_uuid(), module.terminals))
            if (uuid_spec := cst_node.maybe_uuid_spec())
            else None
        )
        inner_scope = scope.make_child_scope(name)
        bit_flags = False
        underlying_type: expr.Expr | None = None
        has_explicit_underlying_type = False
        linter_overrides: set[str] = set()
        if enum_options := cst_node.maybe_enum_option_block():
            for option_cst in enum_options.children_enum_option():
                if option_cst.maybe_enum_option_flags():
                    bit_flags = True
                elif underlying_type_cst := option_cst.maybe_enum_option_underlying_type():
                    if underlying_type is not None:
                        msg = node.append_error_line(
                            underlying_type_cst, module, "underlying_type can only be specified once"
                        )
                        raise ValueError(msg)
                    underlying_type = _extract_underlying_type_cst(underlying_type_cst, module)
                    has_explicit_underlying_type = True
                else:
                    lint_override = cls._extract_linter_override_cst(option_cst.child_linter_override(), module)
                    if lint_override in linter_overrides:
                        msg = node.append_error_line(
                            underlying_type_cst, module, f"Linter override {lint_override} specified more than once"
                        )
                        raise ValueError(msg)
                    linter_overrides.add(lint_override)

        values: dict[int, ValueDef] = {}
        default_field_num: int | None = None
        result = cls(
            type_info=clkbuiltins.TYPE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            uuid=uuid_val,
            name=name,
            scope=scope,
            inner_scope=inner_scope,
            values=values,
            default_field_num=-1,  # We replace this later (or raise ValueError below)
            bit_flags=bit_flags,
            underlying_type=underlying_type,
            has_explicit_underlying_type=has_explicit_underlying_type,
            linter_overrides=linter_overrides,
            history=None,
            attributes=attributes,
            resolved=None,
        )

        for enum_value_cst in cst_node.child_enum_values_block().children_enum_value():
            value = ValueDef.from_cst(result, enum_value_cst, module, inner_scope)
            inner_scope.define(value.name, ValueRef.make(value), module.terminals)
            if value.is_default:
                if default_field_num is not None:
                    msg = value.append_error_line("Multiple values tagged as default")
                    raise ValueError(msg)
                default_field_num = value.field_num
            if value.field_num in values:
                msg = value.append_error_line(f"Duplicate value number #{value.field_num}")
                raise ValueError(msg)
            values[value.field_num] = value
        if default_field_num is None:
            msg = node.append_error_line(cst_node, module, f"No value designated as default for enum {name}")
            raise ValueError(msg)
        result.default_field_num = default_field_num
        assert result.default_field_num >= 0

        if history_cst := cst_node.maybe_enum_history_block():
            result.history = EnumHistory.from_cst(history_cst, module)

        scope.define(name, result, module.terminals)
        return result

    @classmethod
    def _extract_linter_override_cst(cls: type[ClkEnum], cst_node: cst.LinterOverride, module: node.Module) -> str:
        override = primitive.Literal.from_cst(cst_node.child_linter_override_type(), module)
        if not isinstance(override, primitive.StringLiteral):
            msg = override.append_error_line(f"Expected a string literal for linter override but got {type(override)}")
            raise TypeError(msg)
        allowed_overrides = {"performance-enum-size"}
        if override.value not in allowed_overrides:
            msg = override.append_error_line(
                f"Linter override {override.value} not recognized; expected one of {allowed_overrides}"
            )
            raise ValueError(msg)
        return override.value

    def _validate_history(self) -> None:
        """Validate schema history.

        Raises:
            ValueError: If any validation rule is violated
        """
        if not self.history:
            return

        current_value_nums = set(self.values.keys())
        historical_nums = set(itertools.chain(self.history.legacy_became.keys(), self.history.removed))
        self._validate_value_number_overlap(current_value_nums, historical_nums)

    def _validate_value_number_overlap(self, current_nums: set[int], historical_nums: set[int]) -> None:
        """Check that current and historical value numbers don't overlap.

        Args:
            current_nums: Set of value numbers in current schema
            historical_nums: Set of value numbers in historical values

        Raises:
            ValueError: If there is overlap between current and historical value numbers
        """
        overlap = current_nums.intersection(historical_nums)
        if overlap:
            msg = f"Value numbers {overlap} are used in both current and historical values"
            raise ValueError(msg)

    def cur_version(self) -> int:
        """Get the current version of the schema."""
        return self.history.version if self.history else max(self.values)

    def resolve(self) -> ResolvedEnum:
        """Perform IR finalization."""
        if self.resolved:
            return self.resolved

        has_explicit_values = False
        if isinstance(self.underlying_type, expr.Expr):
            assert self.has_explicit_underlying_type
            underlying_type = self.underlying_type.evaluate()
            if not isinstance(underlying_type, clkbuiltins.IntegerPrimitiveType):
                msg = self.underlying_type.append_error_line(
                    f"Expected an integer primitive type, but got {underlying_type}"
                )
                raise TypeError(msg)
            self.underlying_type = underlying_type

        if any(isinstance(x.integer_value, expr.Expr) for x in self.values.values()):
            if not all(isinstance(x.integer_value, expr.Expr) for x in self.values.values()):
                msg = self.append_error_line("If any enum value has an underlying_value specified, then all must.")
                raise ValueError(msg)
            self._resolve_underlying_values()
            has_explicit_values = True
        else:
            self._assign_integer_values()

        if not self.history:
            self.history = EnumHistory(version=self.cur_version(), legacy_became={}, removed=set())
        self._validate_history()

        assert self.underlying_type is not None
        self.resolved = ResolvedEnum(
            doc=self.doc,
            name=self.name,
            scope=self.scope,
            module=self.module,
            cst_node=self.cst_node,
            type_info=self.type_info,
            inner_scope=self.inner_scope,
            uuid=self.uuid,
            values={val.field_num: val.get_resolved() for val in self.values.values()},
            default_field_num=self.default_field_num,
            bit_flags=self.bit_flags,
            underlying_type=self.underlying_type,
            has_explicit_underlying_type=self.has_explicit_underlying_type,
            has_explicit_values=has_explicit_values,
            linter_overrides=self.linter_overrides,
            history=self.history,
            attributes=self.attributes,
            source=self,
        )

        return self.resolved

    def _assign_integer_values(self) -> None:
        # Values are assigned in order of field_num, but they're not necessarily
        # the same as the field_nums.  The default value always gets value 0
        # instead of whatever its field num is.  The rest start at value 1.
        cur_value = 1
        for field_num, value_def in sorted(self.values.items()):
            if value_def.is_default:
                assert self.default_field_num == field_num
                value_def.integer_value = 0
            else:
                value_def.integer_value = cur_value
                cur_value += 1
            value_def.resolved = ResolvedValueDef(
                doc=value_def.doc,
                module=value_def.module,
                cst_node=value_def.cst_node,
                name=value_def.name,
                scope=value_def.scope,
                enum=value_def.enum,
                field_num=value_def.field_num,
                is_default=value_def.is_default,
                integer_value=value_def.integer_value,
                value_is_explicit=False,
                source=value_def,
            )
        _validate_underlying_type(self, 0, len(self.values) - 1)

    def _resolve_underlying_values(self) -> None:
        underlying_values = {}
        for fld_def in self.values.values():
            assert isinstance(fld_def.integer_value, expr.Expr)
            underlying_value = fld_def.integer_value.evaluate()
            if not isinstance(underlying_value, primitive.DecimalValue):
                msg = fld_def.integer_value.append_error_line(f"Expected an integer value, but got {underlying_value}")
                raise TypeError(msg)
            int_value = int(underlying_value.value)
            if int_value != underlying_value.value:
                msg = fld_def.integer_value.append_error_line(
                    f"Expected an integer value, but got {underlying_value.value}"
                )
                raise ValueError(msg)
            if int_value in underlying_values:
                msg = fld_def.integer_value.append_error_line(f"Duplicate underlying value {int_value}")
                raise ValueError(msg)
            if fld_def.is_default and int_value != 0:
                msg = fld_def.integer_value.append_error_line(
                    f"Default value must have underlying value of 0, not {int_value}"
                )
                raise ValueError(msg)
            underlying_values[int_value] = fld_def
            fld_def.integer_value = int_value
            fld_def.resolved = ResolvedValueDef(
                doc=fld_def.doc,
                module=fld_def.module,
                cst_node=fld_def.cst_node,
                name=fld_def.name,
                scope=fld_def.scope,
                enum=fld_def.enum,
                field_num=fld_def.field_num,
                is_default=fld_def.is_default,
                integer_value=fld_def.integer_value,
                value_is_explicit=True,
                source=fld_def,
            )
        min_value = min(underlying_values.keys())
        max_value = max(underlying_values.keys())
        _validate_underlying_type(self, min_value, max_value)

    def get_linter_overrides(self) -> set[str]:
        """Get the lint overrides for an enum.

        Returns: A set of overrides specified in this enum's options block.
        """
        return self.get_resolved().linter_overrides

    def get_underlying_type(self) -> clkbuiltins.IntegerPrimitiveType:
        """Get the underlying type for an enum.

        The smallest unsigned integer that can represent the enum.

        Returns: An IntegerPrimitiveType for the representation.
        """
        return self.get_resolved().underlying_type

    def get_resolved(self) -> ResolvedEnum:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)
        return self.resolved


def _extract_underlying_type_cst(cst_node: cst.EnumOptionUnderlyingType, module: node.Module) -> expr.Expr:
    underlying_type = expr.Expr.from_cst(cst_node.child_underlying_type(), module)
    typesys.unify(underlying_type.type_info, clkbuiltins.TYPE_TYPE)
    return underlying_type


def _validate_underlying_type(clkenum: ClkEnum | ResolvedEnum, min_: int, max_: int) -> None:
    smallest_type = primitive.smallest_type_to_hold_range(min_, max_)
    if clkenum.underlying_type is None:
        clkenum.underlying_type = smallest_type
        assert clkenum.has_explicit_underlying_type is False
        return
    assert isinstance(clkenum.underlying_type, clkbuiltins.IntegerPrimitiveType)
    if smallest_type.signed and not clkenum.underlying_type.signed:
        msg = clkenum.append_error_line(
            "Requested underlying type is unsigned but some values are specified as negative."
        )
        raise ValueError(msg)
    if smallest_type.bit_width > clkenum.underlying_type.bit_width:
        msg = clkenum.append_error_line(
            f"Requested underlying type has {clkenum.underlying_type.bit_width} bits but {smallest_type.bit_width} bits are required to hold all values."
        )
        raise ValueError(msg)


@dataclass
class ResolvedValueDef(node.NamedEntity, node.CstNode[cst.EnumValue], node.DocRequiredEntity):
    """A Clockwork enum value definition."""

    enum: ClkEnum = field(repr=False)
    field_num: int
    is_default: bool
    integer_value: int
    value_is_explicit: bool
    source: ValueDef | None = field(repr=False)


@dataclass
class ValueDef(node.NamedEntity, node.CstNode[cst.EnumValue], node.DocRequiredEntity):
    """A Clockwork enum value definition."""

    enum: ClkEnum
    field_num: int
    is_default: bool
    integer_value: int | expr.Expr
    resolved: ResolvedValueDef | None = field(repr=False)

    @classmethod
    def from_cst(
        cls: type[ValueDef],
        enum: ClkEnum,
        cst_node: cst.EnumValue,
        module: node.Module,
        scope: node.Scope,
    ) -> ValueDef:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.from_cst(cst_node.child_doc(), module)
        field_num = int_from_cst(cst_node.child_value_num(), module.terminals)
        name = get_span(cst_node.child_name().child_value(), module.terminals)
        is_default = cst_node.maybe_default() is not None
        # If there's still a -1 here, it will be filled in automatically by ClkEnum later
        underlying_value: expr.Expr | int = -1
        for detail in cst_node.children_enum_value_detail():
            detail.child_underlying_value_tag()  # This is a sanity check and future-proofing
            if isinstance(underlying_value, expr.Expr):
                msg = node.append_error_line(detail, module, "underlying_value may only be specified once")
                raise ValueError(msg)  # noqa: TRY004
            underlying_value = expr.Expr.from_cst(detail.child_value(), module)
        return cls(
            doc,
            module,
            cst_node,
            name,
            scope,
            enum,
            field_num,
            is_default,
            integer_value=underlying_value,
            resolved=None,
        )

    def name_resolution_fields(self) -> Container[str]:
        """Override recursion for node.resolve_names.

        Because we hold a recursive reference back to our parent, we need to
        prevent node.resolve_names from recursing on this object.
        """
        return []

    def get_resolved(self) -> ResolvedValueDef:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)
        return self.resolved


@dataclass
class ValueRef(node.NamedEntity, typesys.Value):
    """A reference to an enum value."""

    value_def: ValueDef

    @classmethod
    def make(cls: type[ValueRef], value_def: ValueDef) -> ValueRef:
        """Construct a ValueRef."""
        return cls(type_info=value_def.enum, name=value_def.name, scope=value_def.scope, value_def=value_def)

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        return f"{self.value_def.enum.value_key()}::{self.value_def.field_num}"
