# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Built-in for formatted string literals.

This module provides support for the fmt() builtin in Clockwork, which enables
Python-style string formatting at compile time. Field substitutions are resolved
from the lexical scope when the format string is evaluated during compilation.
"""

from __future__ import annotations

import re
import string
from copy import copy
from dataclasses import dataclass
from typing import TYPE_CHECKING, Any, Final

from clockwork.dsl.ir import clkbuiltins, node, primitive, typesys
from clockwork.dsl.ir.primitive import DecimalValue, StringLiteral, StringValue
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Sequence

    from clockwork.dsl import clockwork_cst_protocol as cst


SYSTEM_FILE_PATH_BASE_STR: Final = "system_file_path_base()"

SYSTEM_INSTANCE_NAME_STR: Final = "system_instance_name()"

NAME_REGEX: Final = re.compile(r"^name\(([_a-zA-Z][_a-zA-Z0-9]*)\)")

CAMEL_CASE_NAME_REGEX: Final = re.compile(r"^camel_case_name\(([_a-zA-Z][_a-zA-Z0-9]*)\)")

SNAKE_CASE_NAME_REGEX: Final = re.compile(r"^snake_case_name\(([_a-zA-Z][_a-zA-Z0-9]*)\)")

SHORT_NAME_REGEX: Final = re.compile(r"^short_name\(([_a-zA-Z][_a-zA-Z0-9]*)\)")

NULLISH_COALESCING_REGEX: Final = re.compile(r"^([_a-zA-Z][_a-zA-Z0-9]*)\s*\?\?\s*([']*[_a-zA-Z0-9]*[']*)$")
"""Matches strings in the form of fmt!("{field_name ?? 'default_value'}") or fmt!("{field_name ?? scoped_variable}") for providing a default value if the field is not found or falsy."""


def _canonical(value: str) -> str:
    """Replace anything but 'a-z', 'A-Z' and '0-9' with '_'."""
    return re.sub(r"[^a-zA-Z0-9]", "_", value)


def _snake_to_camel_case(snake_str: str) -> str:
    """Convert from snake_case to CamelCase."""
    return "".join(x[:1].upper() + x[1:] for x in _canonical(snake_str).split("_"))


def _camel_to_snake_case(value: str) -> str:
    """Convert from CamelCase to snake_case."""
    value = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", _canonical(value))
    value = re.sub(r"(_+)", "_", value)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", value).lower()


@dataclass
class UnevaluatedFmtString(typesys.ObjectIdentityValue):
    """An unevaluated format string that holds a template with placeholders.

    This value is created when fmt() is called in Clockwork source. It must be
    evaluated by resolving substitution fields to their values before becoming
    a usable StringValue.

    Attributes:
        format_string_cst: The StringLiteral containing the format template with
                          {field} placeholders following Python format string syntax.
    """

    format_string_cst: StringLiteral

    def _lookup_entity(self, scope: node.Scope, name: str) -> typesys.Value:
        """Lookup a named entity in the node scope."""
        # At this point, we have no idea what we will get when we look this up.
        entity: Any = scope.lookup(name)
        if entity is None:
            msg = node.enrich_error_if_possible(
                self.format_string_cst,
                f"Substitution field '{name}' not found in scope",
            )
            raise KeyError(msg)

        if isinstance(entity, node.NamedBinding):
            entity = entity.bound_value()
        if isinstance(entity, UnevaluatedFmtString):
            value = entity.evaluate_from_scope(scope)
        elif isinstance(entity, typesys.Value):
            value = entity
        else:
            msg = node.enrich_error_if_possible(
                self.format_string_cst,
                f"Substitution entity '{name}' is not a Value: {entity}",
            )
            raise TypeError(msg)
        return value

    def _lookup_current_system_target(self, scope: node.Scope) -> node.CstNode[Any] | typesys.NamedValue | None:
        """Lookup the current system target from the scope."""
        try:
            system_target = self._lookup_entity(scope, node.CURRENT_SYSTEM_TARGET_SCOPE_KEY)
        except KeyError:
            return None
        if not isinstance(system_target, node.CstNode) or not isinstance(system_target, typesys.NamedValue):
            msg = node.enrich_error_if_possible(
                self.format_string_cst,
                f"Expected a named CstNode for the system target, got {type(system_target).__name__}",
            )
            raise TypeError(msg)
        return system_target

    def _get_object_name(self, scope: node.Scope, name: str) -> str:
        """Get the name of a named object from the scope."""
        obj = self._lookup_entity(scope, name)
        if not isinstance(obj, node.NamedEntity):
            msg = node.enrich_error_if_possible(
                self.format_string_cst,
                f"Expected named entity for '{name}', got {type(obj).__name__}",
            )
            raise TypeError(msg)
        return obj.name

    def _process_name_substitution(self, scope: node.Scope, field_name: str) -> str | int | float | bool:
        """Handle substitutions by looking up a field name in the scope."""
        # At this point, we have no idea what we will get when we look this up.
        value = self._lookup_entity(scope, field_name)
        python_value = value_to_python(value)
        if python_value is None:
            msg = node.enrich_error_if_possible(
                self.format_string_cst,
                f"Substitution field '{field_name}' must be a primitive type (string, number, or bool), got {type(value).__name__}",
            )
            raise ValueError(msg)
        return python_value

    def _process_substitution(self, scope: node.Scope, substitution: str) -> str | int | float | bool:  # noqa: C901, PLR0912 (Complexity is from pattern matching, reads better as one method)
        """Extract the value for a substitution string."""
        if substitution == SYSTEM_FILE_PATH_BASE_STR:
            system_target = self._lookup_current_system_target(scope)
            if not system_target:
                result = "NO_SYSTEM_TARGET"
            else:
                assert isinstance(system_target, node.CstNode)
                module_id = system_target.module.module_id
                result = str(module_id.get_base_path().parent / module_id.name.split("::")[-1])
        elif substitution == SYSTEM_INSTANCE_NAME_STR:
            system_target = self._lookup_current_system_target(scope)
            if not system_target:
                result = "NO_SYSTEM_TARGET"
            else:
                assert isinstance(system_target, typesys.NamedValue)
                result = system_target.name
        elif (result := re.search(NAME_REGEX, substitution)) is not None:
            result = self._get_object_name(scope, result[1])
        elif (result := re.search(SNAKE_CASE_NAME_REGEX, substitution)) is not None:
            name = self._get_object_name(scope, result[1])
            result = _camel_to_snake_case(name)
        elif (result := re.search(CAMEL_CASE_NAME_REGEX, substitution)) is not None:
            name = self._get_object_name(scope, result[1])
            result = _snake_to_camel_case(name)
        elif (result := re.search(SHORT_NAME_REGEX, substitution)) is not None:
            name = self._get_object_name(scope, result[1])
            result = _camel_to_snake_case(name).split("_")[-1]
        elif (result := re.search(NULLISH_COALESCING_REGEX, substitution)) is not None:
            try:
                lhs = self._lookup_entity(scope, result[1])
            except KeyError:
                lhs = None

            rhs_raw = result[2]
            start_char, end_char = rhs_raw[0], rhs_raw[-1] if len(rhs_raw) > 1 else (None, None)

            if (start_char == "'" or end_char == "'") and (start_char != end_char):
                raise SyntaxError(
                    node.enrich_error_if_possible(
                        self.format_string_cst,
                        f"Syntax error in nullish coalescing substitution: mismatched single quotes '{rhs_raw}'",
                    )
                )

            match lhs, start_char, end_char:
                # start from most specific cases to general
                case None, None, None | (typesys.AbsentOptionalValue(), None, None):
                    # replacement is an empty string
                    result = ""
                case (typesys.AbsentOptionalValue(), "'", "'") | (None, "'", "'"):
                    # field is in scope but is an optional with no value and replacement is a literal
                    result = str(rhs_raw[1:-1])
                case (typesys.AbsentOptionalValue(), _, _) | (None, _, _):
                    # field is in scope but is an optional with no value and replacement is not a literal
                    result = self._process_name_substitution(scope, rhs_raw)
                case (typesys.Value(), "'", "'"):
                    # field is in scope and has a value and is not none or absent optional so it must be truthy
                    result = str(value_to_python(lhs))
                case (typesys.Value(), _, _):
                    # field is in scope and has a value which is a variable
                    result = self._process_name_substitution(scope, result[1])
                # NOTE: we intentionally rely on static analysis to catch un-handled patterns here
        else:
            result = self._process_name_substitution(scope, substitution)
        # pyrefly: ignore[bad-return] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return result

    def evaluate_from_scope(self, scope: node.Scope) -> StringValue:
        """Evaluate the format string by looking up substitutions in scope.

        For each {field} placeholder in the format string, this method looks up
        the corresponding identifier in the provided scope, retrieves its resolved
        value, converts it to a Python primitive, and substitutes it into the
        format string.

        Args:
            scope: The scope in which to look up substitution field names.

        Returns:
            A StringValue with the formatted result.
        """
        substitutions = extract_substitutions(self.format_string_cst)

        substitution_values: dict[str, str | int | float | bool] = {}
        for field_name in substitutions:
            substitution_values[field_name] = self._process_substitution(scope, field_name)
        return self.evaluate_from_dict(substitution_values)

    def evaluate_from_dict(self, substitution_values: dict[str, str | int | float | bool]) -> StringValue:
        """Evaluate the format string using provided substitution values.

        This is a lower-level method for when substitution values are already
        available as Python primitives. It bypasses scope lookup and directly
        formats the string. Most callers should use evaluate_from_scope() instead.

        Args:
            substitution_values: Dictionary mapping field names to their primitive
                                values (str, int, float, or bool).

        Returns:
            A StringValue with the formatted result.
        """
        try:
            formatted_string = self.format_string_cst.value.format(**substitution_values)
        except (KeyError, IndexError, ValueError) as e:
            msg = node.enrich_error_if_possible(
                self.format_string_cst,
                f"Error formatting string: {e}",
            )
            raise ValueError(msg) from e

        return StringValue.make(formatted_string)


@dataclass
class FmtStringFactory(node.NamedEntity, typesys.MacroCallableEntity, typesys.ObjectIdentityValue):
    """Factory for creating UnevaluatedFmtString values from Clockwork source.

    This is the builtin callable registered as 'fmt' in the Clockwork language.
    When called in source code like fmt("hello/{name}"), it creates an
    UnevaluatedFmtString IR node that will be evaluated during compilation.
    """

    @override
    def evaluate_call(
        self,
        *,
        ir_node: node.CstNode[cst.Expr] | None,
        module: node.Module,
        args: Sequence[tuple[str | None, typesys.Value]],
    ) -> UnevaluatedFmtString:
        """Construct an UnevaluatedFmtString from a fmt() call in source code.

        Args:
            ir_node: The CST node for the call expression (for error reporting).
            module: The module containing this call.
            args: The arguments to fmt(). Must be exactly one StringLiteral argument.

        Returns:
            An UnevaluatedFmtString that will be evaluated later during resolution.
        """
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        if len(args) != 1:
            msg = node.enrich_error_if_possible(ir_node, "fmt takes exactly one argument.")
            raise ValueError(msg)
        format_string_cst = args[0][1]
        if not isinstance(format_string_cst, primitive.StringLiteral):
            msg = node.enrich_error_if_possible(ir_node, "fmt argument must be a string.")
            raise TypeError(msg)

        return UnevaluatedFmtString(type_info=clkbuiltins.STRING, format_string_cst=format_string_cst)


def extract_substitutions(format_string_cst: StringLiteral) -> set[str]:
    """Extract field names from format string using Python's string.Formatter.

    Parses the format string to find all {field_name} placeholders and returns
    the list of field names. Empty placeholders {} are ignored

    Args:
        format_string_cst: The StringLiteral containing the format template.

    Returns:
        List of field names used in the format string.
    """
    formatter = string.Formatter()
    substitutions = set()

    try:
        for _, field_name, _, _ in formatter.parse(format_string_cst.value):
            if field_name is not None and field_name:
                substitutions.add(field_name)
    except ValueError as e:
        msg = node.enrich_error_if_possible(
            format_string_cst,
            f"Invalid format string: {e}",
        )
        raise ValueError(msg) from e

    return substitutions


def value_to_python(value: typesys.Value) -> str | int | float | bool | None:
    """Convert a Clockwork primitive value to a Python primitive for formatting.

    Handles conversion of Clockwork IR values to Python primitives suitable for
    use in string formatting:
    - StringValue -> str
    - DecimalValue -> int (if whole number) or float
    - Boolean values (TRUE_VALUE/FALSE_VALUE) -> bool

    Args:
        value: The Clockwork IR value to convert.

    Returns:
        The Python primitive equivalent (str, int, float, or bool), or None if
        the value is not a supported primitive type.
    """
    if isinstance(value, StringValue):
        return value.value
    if isinstance(value, DecimalValue):
        int_value = int(value.value)
        if int_value == value.value:
            return int_value
        return float(value.value)
    if value in (clkbuiltins.TRUE_VALUE, clkbuiltins.FALSE_VALUE):
        return primitive.value_to_bool(value)
    return None


clkbuiltins.BUILTINS_SCOPE.define(
    "fmt",
    FmtStringFactory(type_info=clkbuiltins.TYPE_TYPE, name="Fmt", scope=clkbuiltins.BUILTINS_SCOPE),
    None,
)


def evaluate_instantiation_strings(instantiation: typesys.Instantiation, scope: node.Scope) -> typesys.Instantiation:
    """Resolve the format string arguments in an instantiation.

    Args:
        instantiation: Instantiation to resolve.
        scope: Scope to use when evaluating format strings.

    Return:
        Copy of the instantiation with any format string arguments evaluated.
    """
    result = copy(instantiation)
    result.arguments = {}
    for arg_name, arg_value in instantiation.arguments.items():
        if isinstance(arg_value, UnevaluatedFmtString):
            result.arguments[arg_name] = arg_value.evaluate_from_scope(scope)
        elif isinstance(arg_value, typesys.Instantiation):
            result.arguments[arg_name] = evaluate_instantiation_strings(arg_value, scope)
        else:
            result.arguments[arg_name] = arg_value
    return result
