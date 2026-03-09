# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""IR Type System and Type Inference.

See typesys.md for more discussion of the type system and type inference.
"""

from __future__ import annotations

import itertools
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import Enum
from typing import TYPE_CHECKING, Any

from typing_extensions import override

if TYPE_CHECKING:  # pragma: no cover
    from collections.abc import Iterable, Mapping, Sequence

    from clockwork.dsl import clockwork_cst as cst

from clockwork.dsl.ir import node


@dataclass(eq=False)
class Value(ABC):
    """Represents any kind of value in the DSL.

    Attributes:
        type_info: Either a known type value or, if the type is not initially known, an inference variable.
    """

    type_info: TypeVal | InferenceVar = field(repr=False)

    @abstractmethod
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""


class ObjectIdentityValue(Value):
    """Base class for values that do not have stable ID beyond their object identity."""

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        return str(id(self))


class NamedAttribute(Value, node.NamedEntity):
    """Base class for values that are named.  The name is used as the stable ID."""

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        return f"{self.scope.uniq_path}.{self.name}"


@dataclass(eq=False)
class NamedValue(Value, node.NamedEntity):
    """Base class for values that are named.  The name is used as the stable ID."""

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        return f"{self.scope.uniq_path}::{self.name}"


@dataclass
class TypeVal(Value):
    """IR Node representing a type value.

    In the DSL, and the IR, types are first-class, meaning that types are represented as values which can be assigned to
    names, passed as parameters, etc.  Each type, whether built-in or user-defined, is represented in the IR as an
    instance of a subclass of TypeVal.  TypeVal can also be the result of an expression which produces a type when evaluated.
    """

    def satisfies(self, constraint: NumericType) -> bool:
        """Check if this type satisfies a constraint.

        At present, we have a very simple constraint system that is special-cased for integer and floating point
        numbers.  By default, no types satisfy these constraints, but the child classes for these types override this
        appropriately.
        """
        return constraint == NumericType.NONE

    def generic_parameters(self) -> Sequence[Parameter] | None:
        """Get the generic parameters for the type.

        Returns:
            The generic parameters, or None if the type is not generic.
        """
        return None


@dataclass
class TypeDef(TypeVal, NamedValue):
    """IR Node representing the canonical definition of a type.

    The primary difference between this class and TypeVal is that this only holds a resolved type; it cannot hold a type expression, and it has a name within a scope.
    """


@dataclass(frozen=True, slots=True)
class Parameter:
    """Represents a generic parameter.

    In Clockwork, types are values, so there is no real difference between a "type" parameter and a "non-type" parameter
    (in C++ terms); the type_bound will be "Type", the type of type values, if it's a type parameter, and something else
    (e.g., "Int64") if it's a non-type parameter.

    Attributes:
        name: The parameter name
        type_bound: The expected type of the parameter
        default: The default value, or None if the parameter is required
    """

    name: str
    type_bound: TypeVal
    default: Value | None


@dataclass(slots=True)
class DeferrableType(Value):
    """Represents a type bound to an unresolved Parameter."""


@dataclass(frozen=True, slots=True)
class Argument:
    """Represents an argument to bind to a parameter during generic type instantiation.

    Attributes:
        name: The parameter name
        value: The value for the parameter
    """

    name: str
    value: Value


@dataclass
class GenericTypeVal(TypeVal):
    """Represents a parameterized generic type.

    Attributes:
        parameters: Ordered sequence of parameters
    """

    parameters: Sequence[Parameter]

    @override
    def generic_parameters(self) -> Sequence[Parameter] | None:
        """Get the generic parameters for the type.

        Returns:
            The generic parameters, or None if the type is not generic.
        """
        return self.parameters


@dataclass
class GenericTypeDef(TypeDef, GenericTypeVal):
    """Base class for generic TypeDefs."""


@dataclass
class Instantiation(TypeVal):
    """Represents an instantiation of a generic type.

    Attributes:
        instantiates: The generic type being instantiated
        arguments: The instantiation arguments
    """

    instantiates: TypeVal
    arguments: Mapping[str, Value]

    @override
    def value_key(self) -> str:
        """Generate a unique, comparable, hashable type key for this type."""
        params = self.instantiates.generic_parameters()
        assert params is not None
        arg_str = ",".join(f"{param.name}={self.arguments[param.name].value_key()}" for param in params)
        return f"{self.instantiates.value_key()}<{arg_str}>"


class NumericType(Enum):
    """Used to partially constrain numeric literals during type inference.

    Attributes:
        NONE: There is no numeric type constraint; the type may or may not be numeric
        SIGNED_INTEGER: The type must be a signed integer type
        INTEGER: The type must be an integer type (possibly signed or unsigned)
        FLOAT: The type must be a floating point type
    """

    NONE = 0
    SIGNED_INTEGER = 1
    INTEGER = 2
    FLOAT = 3

    def intersect_with(self, other: NumericType) -> NumericType:
        """Create a new constraint combining this with another.

        Returns: The combined constraint

        Raises:
            TypeError if the intersection is nil
        """
        if self == NumericType.NONE:
            return other
        if other == NumericType.NONE:
            return self
        if (self == NumericType.SIGNED_INTEGER and other == NumericType.INTEGER) or (
            self == NumericType.INTEGER and other == NumericType.SIGNED_INTEGER
        ):
            return NumericType.SIGNED_INTEGER
        if self != other:
            msg = "Cannot unify integer and floating-point types without explicit casting."
            raise TypeError(msg)
        return self


class InferenceVar:
    """Represents a type variable."""

    _ID_SEQ = itertools.count()

    def __init__(
        self,
        uniq_id: int,
        numeric_type: NumericType = NumericType.NONE,
        cst_node: Any | None = None,  # noqa: ANN401 (Any is required for flexibility)
    ) -> None:
        """Create a new InferenceVar.

        Other than for unit testing, use the make() factory method instead of directly creating instances.

        Args:
            uniq_id: Unique identifier for this type variable
            numeric_type: Numeric type constraint
            cst_node: CST node associated with this inference var, if any
        """
        self._id = uniq_id
        self.numeric_type = numeric_type
        # If a TypeVal, then that is the concrete type; if another InferenceVar, then we are equated to that var (and,
        # transitively, its resolution if any); if None then no resolution yet.
        self._resolution: TypeVal | InferenceVar | None = None
        self.cst_node = cst_node

    @override
    def __repr__(self) -> str:
        """Return a string representation of the var."""
        return f"InferenceVar({self._id}, {self.numeric_type}, {self.resolution()})"

    @classmethod
    def make(
        cls: type[InferenceVar],
        context: node.Module,  # noqa: ARG003 (Argument required by parent method signature)
        cst_node: Any | None = None,  # noqa: ANN401 (Any is required for flexibility)
        numeric_type: NumericType = NumericType.NONE,
    ) -> InferenceVar:
        """Construct a new InferenceVar in a given context.

        Args:
            context: The inference context (currently unused)
            cst_node: The CST node corresponding to the entity whose type is inferred (currently unused)
            numeric_type: The numeric type constraint
        """
        return cls(uniq_id=next(InferenceVar._ID_SEQ), numeric_type=numeric_type, cst_node=cst_node)

    def resolution(self) -> TypeVal | InferenceVar:
        """Find the last node in the solution chain for this InferenceVar.

        Returns: The current best resolution for this inference variable.

        Post-conditions:
            The return will either be a concrete TypeVal or an InferenceVar whose resolution is None.
        """
        if self._resolution is None:
            return self
        if isinstance(self._resolution, TypeVal):
            return self._resolution
        return self._resolution.resolution()

    def unify(self, target: TypeVal | InferenceVar) -> TypeVal | InferenceVar:
        """Unify the target with this Var.

        Args:
            target: The target type or variable to unify with this variable.

        Returns:
            The unification result.
        """
        lhs_final = self.resolution()
        rhs_final = target.resolution() if isinstance(target, InferenceVar) else target
        if isinstance(lhs_final, TypeVal):
            if isinstance(rhs_final, TypeVal):
                if rhs_final is not lhs_final:
                    msg = f"Type inference failed: {lhs_final} != {rhs_final}"
                    raise TypeError(msg)
                return lhs_final
            return rhs_final.conform(lhs_final)
        if isinstance(rhs_final, TypeVal):
            return self.conform(rhs_final)
        if lhs_final is rhs_final:
            # Already unified
            return lhs_final
        lhs_final.numeric_type = lhs_final.numeric_type.intersect_with(rhs_final.numeric_type)
        rhs_final._resolution = lhs_final  # noqa: SLF001 (rhs_final is also a TypeVal)
        return lhs_final

    def conform(self, to: TypeVal) -> TypeVal:
        """Enforce that this InferenceVar has a specific concrete type.

        Args:
            to: The type to conform to

        Returns:
            The result type (always equal to the type provided but potentially a different instance)

        Raises:
            TypeError on attempt to conform non-equal types.
        """
        resolution = self.resolution()
        if isinstance(resolution, TypeVal):
            if resolution is not to:
                msg = f"Type inference failed: {resolution} != {to}"
                raise TypeError(msg)
            return to
        if not to.satisfies(resolution.numeric_type):
            msg = f"Attempt to unify {resolution.numeric_type} type with {to}"
            raise TypeError(msg)
        resolution._resolution = to  # noqa: SLF001 (_resolution is also a TypeVal)
        return to

    @override
    def __str__(self) -> str:
        """Create a human-readable representation of this InferenceVar."""
        resolution = self.resolution()
        if resolution is self:
            return f"T{self._id}"
        return f"T{self._id}._resolution={self._resolution} -> {self.resolution()}"


def unify(lhs: TypeVal | DeferrableType | InferenceVar, rhs: TypeVal | InferenceVar) -> TypeVal | InferenceVar:
    """Unify two types or inference variables.

    Args:
        lhs: The left-hand side of unification; given slight preference in priority
        rhs: The right-hand side of unification

    Returns:
        The unification result

    Raises:
        TypeError on failed unification.
    """
    if isinstance(lhs, DeferrableType):
        lhs = lhs.type_info
    if isinstance(lhs, InferenceVar):
        return lhs.unify(rhs)
    if isinstance(rhs, InferenceVar):
        return rhs.unify(lhs)
    if lhs.value_key() != rhs.value_key():
        msg = f"Type inference failed: {lhs.value_key()} != {rhs.value_key()}"
        raise TypeError(msg)
    return lhs


def bind_arg(parameter: Parameter, argument: Value) -> Value:
    """Bind an argument to a parameter with type unification.

    Args:
        parameter: Parameter to bind
        argument: Value to bind to it

    Returns:
        The bound argument value, which may have been transformed by type coercion.

    Raises:
        TypeError if the value does not conform to the parameter's type bound.
    """
    unify(parameter.type_bound, argument.type_info)

    return argument


def bind_args(parameters: Sequence[Parameter], args: Sequence[tuple[str | None, Value]]) -> dict[str, Value]:
    """Bind arguments to parameters with type unification.

    Args:
        parameters: Parameters to bind
        args: A sequence of (name, value) tuples representing arguments

    Returns:
        A mapping of {name: value} of bound arguments.

    Raises:
        TypeError if type unification fails
        ValueError if argument form is incorrect (e.g., missing args, positional after keyword, etc.)
    """
    pos_args = []
    kw_args = {}
    for i in range(len(args)):
        name, value = args[i]
        if name is None:
            pos_args.append(value)
            continue
        for j in range(i, len(args)):
            name, value = args[j]
            if name is None:
                msg = f"Non-keyword argument at {j} follows keyword argument {value}"
                raise ValueError(msg)
            if name in kw_args:
                msg = f"Keyword argument {name} specified multiple times"
                raise ValueError(msg)
            kw_args[name] = value
        break
    result = {}
    for i, pos_arg in enumerate(pos_args):
        param = parameters[i]
        result[param.name] = bind_arg(param, pos_arg)
    for param in parameters[len(pos_args) :]:
        try:
            result[param.name] = bind_arg(param, kw_args[param.name])
        except KeyError:
            if param.default is None:
                msg = f"No value specified for parameter {param.name}"
                raise ValueError(msg) from None
            result[param.name] = bind_arg(param, param.default)
    return result


class MembershipEntity:
    """Base class for things which support attribute lookup syntax (foo.bar)."""

    def attribute(self, name: str) -> Value | None:  # noqa: ARG002 (Argument required for parent method signature)
        """Look up a definition in the membership entity.

        Returns:
            The entity with that name, or None if not found.
        """
        return None


class InstantiatableEntity(ABC):
    """Base class for things which can have instances (i.e., which support the new operator)."""

    # We have to suppress PLR0913 (too many args) because these args are needed to create objects.
    # We have made the args kwonly to minimize the risk of mixups.
    @abstractmethod
    def make_instance(  # noqa: PLR0913 (see above)
        self,
        *,
        cst_node: cst.NewStmt | None,
        module: node.Module,
        source_module: node.Module | None = None,
        scope: node.Scope,
        name: str,
        doc: node.Doc | None,
    ) -> node.NamedEntity:
        """Create an instance of the entity."""

    @abstractmethod
    def get_module(self) -> node.Module:
        """Access the entity's module."""


class CallableEntity(ABC):
    """Base class for things which support call syntax."""

    @abstractmethod
    def evaluate_call(
        self, *, ir_node: node.CstNode[cst.Expr], module: node.Module, args: Sequence[tuple[str | None, Value]]
    ) -> Value:
        """Evaluate the call operation."""


class SubscriptableEntity(ABC):
    """Base class for things which support subscript syntax (e.g., signal[instance])."""

    @abstractmethod
    def evaluate_subscript(self, *, index: Value, cst_node: node.CstNode[cst.Expr], module: node.Module) -> Value:
        """Evaluate the subscript operation.

        Args:
            index: The subscript index (use WildcardValue for wildcard '*')
            cst_node: The CST node for error reporting
            module: The module for context

        Returns:
            The result of the subscript operation
        """


def extract_kwargs(parameters: Iterable[str], args: Sequence[tuple[str | None, Value]]) -> dict[str, Value]:
    """Extract keyword arguments from a sequence of arguments."""
    params = set(parameters)
    result = {}
    for name, value in args:
        if not name:
            msg = "Only keyword arguments supported"
            raise ValueError(msg)
        if name not in params:
            msg = f"Parameter {name} not in expected set {params}"
            raise ValueError(msg)
        if name in result:
            msg = f"Parameter {name} specified more than once."
            raise ValueError(msg)
        result[name] = value
    return result
