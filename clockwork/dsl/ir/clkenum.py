# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Enum-related IR nodes.

This is named clkenum to avoid conflicts with Python's enum standard library module.
"""

from __future__ import annotations

import uuid
from dataclasses import dataclass, field
from typing import TYPE_CHECKING, cast

from clockwork.dsl import cst
from clockwork.dsl.ir import clkbuiltins, expr, node, primitive, typesys
from clockwork.dsl.ir.cst_util import get_span, int_from_cst
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Container, Iterable


@dataclass
class HistoricalValueDef(node.DocableEntity, node.CstNode[cst.EnumValueHistory]):
    """Historical information about a value that no longer exists or has been replaced by another."""

    enum: ClkEnum
    field_num: int
    name: str
    is_default: bool
    integer_value: int | expr.Expr | None
    removed_in_version: int | None = None
    became_field_num: int | None = None
    resolved: ResolvedHistoricalValueDef | None = field(repr=False, default=None)

    def name_resolution_fields(self) -> Container[str]:
        """Override recursion for node.resolve_names.

        Because we hold a recursive reference back to our parent, we need to
        prevent node.resolve_names from recursing on this object.
        """
        return ["integer_value"]

    @classmethod
    def from_cst(
        cls: type[HistoricalValueDef],
        cst_node: cst.EnumValueHistory,
        module: node.Module,
        enum: ClkEnum,
    ) -> HistoricalValueDef:
        """Create an IR HistoricalValueDef from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        value_def = ValueDef.from_cst(
            enum, cast("cst.EnumValue", cst_node.child_enum_history_value()), module, enum.inner_scope
        )
        value_change = cst_node.child_enum_value_change()

        hist_value = cls(
            module=module,
            cst_node=cst_node,
            doc=value_def.doc,
            enum=enum,
            field_num=value_def.field_num,
            name=value_def.name,
            is_default=value_def.is_default,
            integer_value=value_def.integer_value if isinstance(value_def.integer_value, expr.Expr) else None,
            removed_in_version=None,
            became_field_num=None,
            resolved=None,
        )

        changed_version = int_from_cst(value_change.child_changed_version(), module.terminals)
        if value_change.maybe_removed():
            hist_value.removed_in_version = changed_version
        elif value_change.maybe_became():
            hist_value.became_field_num = changed_version

        return hist_value

    def resolve(self) -> ResolvedHistoricalValueDef:
        """Perform finalization of the field IR."""
        if self.resolved:
            return self.resolved

        int_value = self.integer_value
        if isinstance(int_value, expr.Expr):
            value = int_value.evaluate()
            if not isinstance(value, primitive.DecimalValue):
                msg = int_value.append_error_line(f"Expected an integer value, but got {value}")
                raise TypeError(msg)
            int_value = int(value.value)

        self.resolved = ResolvedHistoricalValueDef(
            field_num=self.field_num,
            name=self.name,
            is_default=self.is_default,
            integer_value=int_value,
            removed_in_version=self.removed_in_version,
            became_field_num=self.became_field_num,
            source=self,
        )
        return self.resolved


@dataclass
class ResolvedHistoricalValueDef:
    """Resolved version of historical value information."""

    field_num: int
    name: str
    is_default: bool
    integer_value: int | None
    removed_in_version: int | None = None
    became_field_num: int | None = None
    source: HistoricalValueDef | None = field(repr=False, default=None)


@dataclass
class EnumHistoryOptions:
    """Options that apply to a historical version of an enum."""

    version: int
    bit_flags: bool
    underlying_type: expr.Expr | clkbuiltins.IntegerPrimitiveType | None


@dataclass
class ResolvedEnumHistoryOptions:
    """Resolved options that apply to a historical version of an enum."""

    version: int
    bit_flags: bool
    underlying_type: clkbuiltins.IntegerPrimitiveType | None
    source: EnumHistoryOptions | None = field(repr=False, default=None)


@dataclass
class ResolvedEnumHistory:
    """Resolved version of enum history tracking."""

    versions: list[int]
    pseudoversions: list[int]
    values: dict[int, ResolvedHistoricalValueDef]
    options: list[ResolvedEnumHistoryOptions] | None
    source: EnumHistory | None = field(repr=False, default=None)


@dataclass
class EnumHistory:
    """Track historical information about an enum."""

    versions: list[int]
    pseudoversions: list[int]
    values: dict[int, HistoricalValueDef]
    options: list[EnumHistoryOptions] | None
    resolved: ResolvedEnumHistory | None = field(repr=False, default=None)

    @classmethod
    def from_cst(
        cls: type[EnumHistory],
        cst_node: cst.EnumHistoryBlock,
        module: node.Module,
        enum: ClkEnum,
    ) -> EnumHistory:
        """Create an IR EnumHistory from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        versions = []
        historical_values = {}
        historical_options = None

        version_spec = cst_node.child_version_spec()
        versions = [int_from_cst(version_cst, module.terminals) for version_cst in version_spec.children_version()]
        if len(versions) != len(set(versions)):
            msg = node.append_error_line(version_spec, module, "Duplicate version numbers")
            raise ValueError(msg)

        if version_pseudofields_cst := version_spec.maybe_version_pseudofields():
            pseudoversions = [
                int_from_cst(version_cst, module.terminals)
                for version_cst in version_pseudofields_cst.children_version()
            ]
            if len(pseudoversions) != len(set(pseudoversions)):
                msg = node.append_error_line(version_pseudofields_cst, module, "Duplicate pseudofield version numbers")
                raise ValueError(msg)
        else:
            pseudoversions = []

        if values_history := cst_node.maybe_enum_values_history():
            for value_history in values_history.children_enum_value_history():
                hist_value = HistoricalValueDef.from_cst(value_history, module, enum)
                if hist_value.field_num in historical_values:
                    msg = node.append_error_line(
                        value_history, module, f"Duplicate field number {hist_value.field_num} in history block"
                    )
                    raise ValueError(msg)
                historical_values[hist_value.field_num] = hist_value

        historical_options = cls._parse_enum_options_history(cst_node, module)

        return cls(
            versions=versions, pseudoversions=pseudoversions, values=historical_values, options=historical_options
        )

    @staticmethod
    def _parse_enum_options_history(
        cst_node: cst.EnumHistoryBlock,
        module: node.Module,
    ) -> list[EnumHistoryOptions] | None:
        """Parse enum options history from the CST node.

        Args:
            cst_node: The CST node containing the history block
            module: The current module
            enum: The enum being parsed

        Returns:
            A list of EnumHistoryOptions if options history is present, None otherwise
        """
        if not (enum_history := cst_node.maybe_enum_history()):
            return None

        if not (options_block := enum_history.maybe_enum_history_options_block()):
            return None

        assert module.terminals is not None
        historical_options = []

        for history_options in options_block.children_enum_history_options():
            version = int_from_cst(history_options.child_version(), module.terminals)
            bit_flags = False
            underlying_type = None

            for option_cst in history_options.children_enum_option():
                if option_cst.maybe_enum_option_flags():
                    bit_flags = True
                elif underlying_type_cst := option_cst.maybe_enum_option_underlying_type():
                    underlying_type = _extract_underlying_type_cst(underlying_type_cst, module)

            historical_options.append(
                EnumHistoryOptions(
                    version=version,
                    bit_flags=bit_flags,
                    underlying_type=underlying_type,
                )
            )

        return historical_options if historical_options else None

    def resolve(self) -> ResolvedEnumHistory:
        """Perform finalization of the history IR."""
        if self.resolved:
            return self.resolved

        resolved_values = {}
        for num, value in self.values.items():
            resolved_values[num] = value.resolve()

        resolved_options = None
        if self.options:
            resolved_options = []
            for option in self.options:
                underlying_type = option.underlying_type
                if isinstance(underlying_type, expr.Expr):
                    evaluated_type = underlying_type.evaluate()
                    if not isinstance(evaluated_type, clkbuiltins.IntegerPrimitiveType):
                        msg = underlying_type.append_error_line(
                            f"Expected an integer primitive type, but got {evaluated_type}"
                        )
                        raise TypeError(msg)
                    underlying_type = evaluated_type

                resolved_options.append(
                    ResolvedEnumHistoryOptions(
                        version=option.version,
                        bit_flags=option.bit_flags,
                        underlying_type=underlying_type,
                        source=option,
                    )
                )

        self.resolved = ResolvedEnumHistory(
            versions=self.versions,
            pseudoversions=self.pseudoversions,
            values=resolved_values,
            options=resolved_options,
            source=self,
        )
        return self.resolved

    def get_resolved(self) -> ResolvedEnumHistory:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)
        return self.resolved

    def _validate_value_changes(
        self,
        historical_values: Iterable[HistoricalValueDef],
        all_field_nums: set[int],
    ) -> dict[int, int]:
        """Validate changes to historical values.

        Args:
            historical_values: Iterator of historical value definitions
            all_field_nums: Set of all field numbers (current and historical)

        Returns:
            Dict mapping target field numbers to source field numbers for 'became' transitions

        Raises:
            RuntimeError: If a value has no change version
            ValueError: If change version is invalid
        """
        became_targets: dict[int, int] = {}

        for hist_value in historical_values:
            if hist_value.removed_in_version is None and hist_value.became_field_num is None:
                msg = hist_value.append_error_line(f"Historical value {hist_value.field_num} has no change version")
                raise RuntimeError(msg)

            if hist_value.removed_in_version is not None:
                if hist_value.removed_in_version <= hist_value.field_num:
                    msg = hist_value.append_error_line(
                        f"Historical value {hist_value.field_num} cannot be removed in version {hist_value.removed_in_version}"
                    )
                    raise ValueError(msg)

                if hist_value.removed_in_version not in all_field_nums:
                    msg = hist_value.append_error_line(
                        f"Historical value {hist_value.field_num} references non-existent version {hist_value.removed_in_version}"
                    )
                    raise ValueError(msg)

            if hist_value.became_field_num is not None:
                if hist_value.became_field_num not in all_field_nums:
                    msg = hist_value.append_error_line(
                        f"Historical value {hist_value.field_num} references non-existent version {hist_value.became_field_num}"
                    )
                    raise ValueError(msg)

                became_targets[hist_value.became_field_num] = hist_value.field_num

        return became_targets


@dataclass
class _HistoricalValueInfo:
    """Temporary value information used during historical enum construction."""

    doc: node.Doc
    name: str
    is_default: bool
    integer_value: int | None
    field_num: int


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
    history: ResolvedEnumHistory = field(repr=False)
    source: ClkEnum | None = field(repr=False)
    _historical_versions: dict[int, ResolvedEnum] = field(
        repr=False, default_factory=dict
    )  # Cache for historical versions

    def cur_version(self) -> int:
        """Get the current version of the enum."""
        max_value = max(self.values)
        max_pseudoversion = max(self.history.pseudoversions) if self.history.pseudoversions else -1
        return max(max_value, max_pseudoversion)

    def _validate_version(self, version: int) -> None:
        """Validate that the requested version exists in history.

        Args:
            version: Version to validate

        Raises:
            ValueError: If version is not in the enum history
        """
        if version not in self.history.versions:
            msg = f"Version {version} not found in enum history"
            raise ValueError(msg)

    def _collect_historical_values(self, version: int) -> tuple[dict[int, _HistoricalValueInfo], int | None, bool]:
        """Collect values that existed at the given version.

        Args:
            version: The historical version to collect values for

        Returns:
            A tuple of (values_dict, default_field_num, has_explicit_values)
            where values_dict maps field_num to _HistoricalValueInfo,
            default_field_num is the field number of the default value (or None if not found),
            and has_explicit_values indicates if any values have explicit integer assignments
        """
        current_values, current_default_field_num, has_explicit_current_values = (
            self._collect_current_values_at_version(version)
        )

        historical_values, historical_default_field_num, has_explicit_historical_values = (
            self._collect_historical_values_at_version(version)
        )

        merged_values = {**current_values, **historical_values}
        default_field_num = (
            current_default_field_num if current_default_field_num is not None else historical_default_field_num
        )
        has_explicit_values = has_explicit_current_values or has_explicit_historical_values

        return merged_values, default_field_num, has_explicit_values

    def _collect_current_values_at_version(
        self, version: int
    ) -> tuple[dict[int, _HistoricalValueInfo], int | None, bool]:
        """Collect current values that existed at the given version.

        Args:
            version: The historical version to collect values for

        Returns:
            A tuple of (values_dict, default_field_num, has_explicit_values)
        """
        historical_values: dict[int, _HistoricalValueInfo] = {}
        default_field_num: int | None = None
        has_explicit_values = False

        # Include current values with field_num <= version
        for field_num, value in self.values.items():
            if field_num > version:
                continue
            if value.is_default:
                if default_field_num is not None:
                    msg = value.append_error_line(
                        f"Multiple values tagged as default {default_field_num} and {field_num}"
                    )
                    raise ValueError(msg)
                default_field_num = field_num

            historical_values[field_num] = _HistoricalValueInfo(
                doc=value.doc,
                name=value.name,
                is_default=value.is_default,
                integer_value=value.integer_value if value.value_is_explicit else None,
                field_num=field_num,
            )
            if value.value_is_explicit:
                has_explicit_values = True

        return historical_values, default_field_num, has_explicit_values

    def _collect_historical_values_at_version(
        self, version: int
    ) -> tuple[dict[int, _HistoricalValueInfo], int | None, bool]:
        """Collect historical values that existed at the given version.

        Args:
            version: The historical version to collect values for

        Returns:
            A tuple of (values_dict, default_field_num, has_explicit_values)
        """
        historical_values: dict[int, _HistoricalValueInfo] = {}
        default_field_num: int | None = None
        has_explicit_values = False

        for field_num, hist_value in self.history.values.items():
            if (
                field_num <= version
                and (hist_value.removed_in_version is None or hist_value.removed_in_version > version)
                and (hist_value.became_field_num is None or hist_value.became_field_num > version)
            ):
                if hist_value.is_default:
                    if default_field_num is not None:
                        msg = self.append_error_line(
                            f"Multiple values tagged as default {default_field_num} and {field_num}"
                        )
                        raise ValueError(msg)
                    default_field_num = field_num

                assert field_num not in historical_values
                historical_values[field_num] = _HistoricalValueInfo(
                    doc=node.Doc(
                        module=self.module,
                        cst_node=None,
                        value=f"Historical value '{hist_value.name}' from version {field_num}",
                    ),
                    name=hist_value.name,
                    is_default=hist_value.is_default,
                    integer_value=hist_value.integer_value,
                    field_num=field_num,
                )

                if hist_value.integer_value is not None:
                    has_explicit_values = True

        return historical_values, default_field_num, has_explicit_values

    def _auto_assign_values(self, values: dict[int, _HistoricalValueInfo], has_explicit_values: bool) -> None:
        """Auto-assign integer values to values that don't have them.

        Args:
            values: Dictionary mapping field numbers to value info objects
            has_explicit_values: Whether any values have explicit integer assignments

        Raises:
            ValueError: If there's a mix of explicit and implicit integer values
        """
        need_auto_assign = False
        for info in values.values():
            if info.integer_value is None:
                need_auto_assign = True
                break

        if need_auto_assign and has_explicit_values:
            msg = f"Enum {self.name} has a mix of explicit and implicit integer values"
            raise ValueError(msg)

        if need_auto_assign:
            cur_value = 1  # Start auto-assignment at 1 because default is always 0
            for _, info in sorted(values.items()):
                if info.is_default:
                    final_value = 0
                else:
                    # Other values get assigned sequentially
                    final_value = cur_value
                    cur_value += 1
                info.integer_value = final_value

    def _get_options_for_version(self, version: int) -> tuple[bool, clkbuiltins.IntegerPrimitiveType | None, bool]:
        """Get the enum options (bit_flags, underlying_type) applicable for a given version.

        This is subtle: We want the options that are specified for the lowest
        version that's greater than or equal to the target version.  This is
        because the options are specified in the file after the fact: when you
        change the options, you record the options in effect in the version
        prior to your change.

        For example: If there are no historical options, then that means the
        schema's current options apply to all historical versions (they've
        never changed).

        But if current version is 10 and there's a historical options spec at
        version 5, that means that the options changed in version 6. Therefore
        the current options apply to versions 6-10, and the historical options
        apply to versions 5 and earlier.

        Therefore, you take the lowest options version greater than or equal
        to the target version.

        Args:
            version: The version to get options for

        Returns:
            A tuple of (bit_flags, underlying_type, has_explicit_underlying_type)
        """
        # Start with current version's options
        bit_flags = self.bit_flags
        underlying_type = self.underlying_type if self.has_explicit_underlying_type else None
        has_explicit_underlying_type = self.has_explicit_underlying_type

        # Replace those with the lowest historical options version >= target, if any
        if self.history.options:
            applicable_options = [opt for opt in self.history.options if opt.version >= version]
            if applicable_options:
                # Get the lowest version >= our target
                closest_option = min(applicable_options, key=lambda opt: opt.version)
                bit_flags = closest_option.bit_flags
                underlying_type = closest_option.underlying_type
                # If the historical option has an explicit underlying type, then we're explicit
                has_explicit_underlying_type = underlying_type is not None

        return bit_flags, underlying_type, has_explicit_underlying_type

    def _create_historical_value_defs(
        self, values: dict[int, _HistoricalValueInfo], need_auto_assign: bool
    ) -> dict[int, ResolvedValueDef]:
        """Create ResolvedValueDef instances for historical values.

        Args:
            values: Dictionary mapping field numbers to value info objects
            need_auto_assign: Whether values were auto-assigned

        Returns:
            Dictionary mapping field numbers to resolved value definitions
        """
        final_values: dict[int, ResolvedValueDef] = {}
        for field_num, info in values.items():
            assert info.integer_value is not None
            final_values[field_num] = ResolvedValueDef(
                doc=info.doc,
                module=self.module,
                cst_node=None,
                name=info.name,
                scope=self.scope,
                enum=self.source or cast("ClkEnum", self),
                field_num=field_num,
                is_default=info.is_default,
                integer_value=info.integer_value,
                value_is_explicit=(not need_auto_assign),
                source=None,
            )
        return final_values

    def get_enum_at_version(self, version: int) -> ResolvedEnum:
        """Generate a ResolvedEnum instance representing this enum at a specific historical version.

        Args:
            version: The historical version to generate

        Returns:
            A new ResolvedEnum instance representing this enum at the specified version

        Raises:
            ValueError: If the requested version is not in the enum history versions list
        """
        self._validate_version(version)

        if version == self.cur_version():
            return self

        if version in self._historical_versions:
            return self._historical_versions[version]

        historical_values, default_field_num, has_explicit_values = self._collect_historical_values(version)

        if default_field_num is None:
            msg = f"No default value found for enum {self.name} at version {version}"
            raise ValueError(msg)

        need_auto_assign = any(info.integer_value is None for info in historical_values.values())
        self._auto_assign_values(historical_values, has_explicit_values)

        final_historical_values = self._create_historical_value_defs(historical_values, need_auto_assign)

        bit_flags, underlying_type, has_explicit_underlying_type = self._get_options_for_version(version)

        int_values = [v.integer_value for v in final_historical_values.values()]
        min_value = min(int_values)
        max_value = max(int_values)

        if underlying_type is None:
            underlying_type = primitive.smallest_type_to_hold_range(min_value, max_value)
            has_explicit_underlying_type = False

        result = ResolvedEnum(
            doc=self.doc,
            name=self.name,
            scope=self.scope,
            module=self.module,
            cst_node=self.cst_node,
            type_info=self.type_info,
            inner_scope=self.inner_scope,
            uuid=self.uuid,
            values=final_historical_values,
            default_field_num=default_field_num,
            bit_flags=bit_flags,
            underlying_type=underlying_type,
            has_explicit_underlying_type=has_explicit_underlying_type,
            has_explicit_values=(not need_auto_assign),
            linter_overrides=self.linter_overrides,
            history=self.history,
            source=self.source,
        )
        _validate_underlying_type(result, min_value, max_value)

        self._historical_versions[version] = result
        return result


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
    resolved: ResolvedEnum | None = field(repr=False)

    @override
    def lookup(self, name: str) -> node.NamedEntity | None:
        """Look up a value within this enum.

        Returns:
            The entity with that name, or None if not found.
        """
        return self.inner_scope.lookup(name, recursive=False)

    @classmethod
    def from_cst(cls: type[ClkEnum], cst_node: cst.Enum, module: node.Module, scope: node.Scope) -> ClkEnum:  # noqa: C901, PLR0912
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.from_cst(cst_node.child_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
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
            result.history = EnumHistory.from_cst(history_cst, module, result)

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
        """Validate enum history.

        Raises:
            ValueError: If any validation rule is violated
        """
        if not self.history:
            return

        current_field_nums = set(self.values.keys())
        historical_nums = set(self.history.values.keys())
        all_field_nums = current_field_nums.union(historical_nums)

        pseudo_set = set(self.history.pseudoversions)
        overlap = pseudo_set.intersection(all_field_nums)
        if overlap:
            msg = self.append_error_line(
                f"Pseudoversions {overlap} conflict with value numbers\n"  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
                "Please note: pseudofield numbers are reserved after creation and cannot be reused as value numbers.\n"
                "If you're seeing this error after adding a value, renumber the value(s)\n"
                "to not conflict with any already-reserved pseudofield numbers."
            )
            raise ValueError(msg)

        # Include pseudoversions in field numbers set for validation
        all_field_nums |= pseudo_set

        self._validate_field_number_overlap(current_field_nums, historical_nums)
        became_targets = self._validate_value_changes(self.history.values.values(), all_field_nums)
        self._validate_duplicate_became_targets(self.history.values.values(), became_targets)
        self._validate_version_list(self.history.versions, all_field_nums)

    def _validate_field_number_overlap(self, current_nums: set[int], historical_nums: set[int]) -> None:
        """Check that current and historical field numbers don't overlap.

        Args:
            current_nums: Set of field numbers in current enum
            historical_nums: Set of field numbers in historical values

        Raises:
            ValueError: If there is overlap between current and historical field numbers
        """
        overlap = current_nums.intersection(historical_nums)
        if overlap:
            msg = f"Value numbers {overlap} are used in both current and historical values"
            raise ValueError(msg)

    def _validate_value_changes(
        self,
        historical_values: Iterable[HistoricalValueDef],
        all_field_nums: set[int],
    ) -> dict[int, int]:
        """Validate changes to historical values.

        Args:
            historical_values: Iterator of historical value definitions
            all_field_nums: Set of all field numbers (current and historical)

        Returns:
            Dict mapping target field numbers to source field numbers for 'became' transitions

        Raises:
            RuntimeError: If a value has no change version
            ValueError: If change version is invalid
        """
        became_targets: dict[int, int] = {}

        for hist_value in historical_values:
            change_version = hist_value.removed_in_version or hist_value.became_field_num
            if change_version is None:
                msg = hist_value.append_error_line(f"Historical value {hist_value.field_num} has no change version")
                raise RuntimeError(msg)

            if change_version <= hist_value.field_num:
                msg = hist_value.append_error_line(
                    f"Historical value {hist_value.field_num} cannot be changed in version {change_version}"
                )
                raise ValueError(msg)

            if change_version not in all_field_nums:
                msg = hist_value.append_error_line(
                    f"Historical value {hist_value.field_num} references non-existent version {change_version}"
                )
                raise ValueError(msg)

            if hist_value.became_field_num is not None:
                became_targets[hist_value.became_field_num] = hist_value.field_num

        return became_targets

    def _validate_duplicate_became_targets(
        self,
        historical_values: Iterable[HistoricalValueDef],
        became_targets: dict[int, int],
    ) -> None:
        """Check that no two values become the same value.

        Args:
            historical_values: Iterator of historical value definitions
            became_targets: Dict mapping target field numbers to source field numbers

        Raises:
            ValueError: If multiple values become the same value
        """
        for hist_value in historical_values:
            if hist_value.became_field_num is not None and (
                hist_value.became_field_num in became_targets
                and became_targets[hist_value.became_field_num] != hist_value.field_num
            ):
                msg = (
                    f"Historical values {became_targets[hist_value.became_field_num]} and {hist_value.field_num} "
                    f"cannot both become value {hist_value.became_field_num}"
                )
                raise ValueError(msg)

    def _validate_version_list(self, versions: list[int], all_field_nums: set[int]) -> None:
        """Validate the version list.

        Args:
            versions: List of versions from history block
            all_field_nums: Set of all field numbers (current and historical) and pseudoversions

        Raises:
            ValueError: If version list contains invalid versions or is missing current version
        """
        for version in versions:
            if version not in all_field_nums:
                msg = f"Version {version} is listed in version history but not defined by any value or pseudoversion"
                raise ValueError(msg)

        if self.cur_version() not in versions:
            msg = f"Historical version does not include current version {self.cur_version()}"
            raise ValueError(msg)

    def cur_version(self) -> int:
        """Get the current version of the enum."""
        max_value = max(self.values)
        if self.history and self.history.pseudoversions:
            max_pseudoversion = max(self.history.pseudoversions)
            return max(max_value, max_pseudoversion)
        return max_value

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

        if self.history:
            resolved_history = self.history.resolve()
            self._validate_history()
        else:
            resolved_history = EnumHistory(
                versions=[self.cur_version()], pseudoversions=[], values={}, options=None
            ).resolve()

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
            history=resolved_history,
            source=self,
        )

        # Validate that all historical versions can be constructed
        # Skip current version as we already validated it
        for version in resolved_history.versions:
            if version != self.cur_version():
                try:
                    # This will cache the result internally
                    self.resolved.get_enum_at_version(version)
                except (ValueError, TypeError) as e:
                    msg = self.append_error_line(f"Error validating historical version {version}: {e}")
                    raise ValueError(msg) from e

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
