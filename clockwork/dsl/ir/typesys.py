# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""IR Type System and Type Inference.

See typesys.md for more discussion of the type system and type inference.
"""

from __future__ import annotations

import itertools
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import Enum
from typing import TYPE_CHECKING, Any, cast

from typing_extensions import override

if TYPE_CHECKING:  # pragma: no cover
    from collections.abc import Iterable, Mapping, Sequence

    from clockwork.dsl import clockwork_cst_protocol as cst

from clockwork.dsl.ir import node


@dataclass(eq=False)
class Value(ABC):
    """Represents any kind of value in the DSL.

    Attributes:
        type_info: Either a known type value or, if the type is not initially known, an inference variable.
    """

    type_info: TypeVal | InferenceVar = field(repr=False)

    def concrete_type_info(self) -> TypeVal | InferenceVar:
        """Return the concrete type info, resolving through generic indirection.

        Generic entities (e.g., parameterized cogs) have type_info=TYPE_TYPE, but their concrete
        type (e.g., COG_TYPE) is needed for policy binding. Subclasses override this to return
        the concrete type.
        """
        return self.type_info

    @abstractmethod
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""


class ObjectIdentityValue(Value):
    """Base class for values that do not have stable ID beyond their object identity."""

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        return str(id(self))


@dataclass
class AbsentOptionalValue(Value):
    """Sentinel value representing an absent optional parameter.

    Used when a parameter typed as Optional<T> is not provided at a call site.
    Consumers that process this value (e.g., fmt! string evaluation) can use it
    to trigger fallback/default behavior.
    """

    @override
    def value_key(self) -> str:
        return "::absent_optional"


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
class Values(Value):
    """A simple ordered sequence of values."""

    elements: list[Value]

    @override
    def value_key(self) -> str:
        element_keys = [element.value_key() for element in self.elements]
        return f"[{','.join(element_keys)}]"


# fmt: off
@dataclass
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class TypeVal(Value):
# fmt: on
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

    def alternatives(self) -> Sequence[TypeVal]:
        """Get the set of types represented by this TypeVal.

        For almost every kind of TypeVal this set will be a singleton. More
        complex types, like TypeUnion, must override this.
        """
        return (self,)


@dataclass
class TypeUnion(TypeVal):
    """Represents a set of types.

    When an InferenceVariable is constrained to a TypeUnion, it can be unified
    with any type that is a member of the set. Such a variable may not, however,
    be unified with another TypeUnion unless it is identical to the existing constraint.

    Use make() to construct an instance.

    Attributes:
      types: The set of types that make up the union.
    """

    types: Sequence[TypeVal]

    @classmethod
    def make(cls: type[TypeUnion], *, types: Sequence[TypeVal]) -> TypeUnion:
        """Construct a TypeUnion.

        Raises:
            TypeError: If `types` contains another TypeUnion.
        """
        for typ in types:
            if isinstance(typ, TypeUnion):
                msg = f"May not construct TypeUnion with union member {typ.value_key()}"
                raise TypeError(msg)

        # pyrefly: ignore[invalid-cast] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return TypeUnion(types=types, type_info=cast("TypeVal", None))

    @override
    def value_key(self) -> str:
        """Generate a unique, comparable, hashable type key for this type."""
        return _type_set_to_str(self.alternatives())

    @override
    def alternatives(self) -> Sequence[TypeVal]:
        return self.types

    def contains(self, element: TypeVal) -> bool:
        """Returns True if `element` is contained in this union."""
        return element.value_key() in [typ.value_key() for typ in self.types]

    def unify(self, target: TypeVal | InferenceVar) -> TypeVal | InferenceVar:
        """Unify the target with this union.

        +-----------------------+------------------------------------------+
        | target                | Outcome (return value, any side effects) |
        +-----------------------+------------------------------------------+
        | T                     | T if T is in S else TypeError            |
        +-----------------------+------------------------------------------+
        | U, where U is a union | U if U = S else TypeError                |
        +-----------------------+------------------------------------------+
        | v=InferenceVar(_)     | S, v constrained to S                    |
        +-----------------------+------------------------------------------+
        | InferenceVar(T)       | T if T is in S else TypeError            |
        +-----------------------+------------------------------------------+
        | InferenceVar(U),      | U if U = S else TypeError                |
        | where U is a union    |                                          |
        +-----------------------+------------------------------------------+

        Args:
            target: The target type or variable to unify with.

        Returns:
            The unification result.
        """
        rhs_final = target.resolution() if isinstance(target, InferenceVar) else target
        if isinstance(rhs_final, TypeUnion):
            if self.value_key() != rhs_final.value_key():
                msg = f"Type inference failed. Attempted to unify unions: {rhs_final.value_key()} != {self.value_key()}"
                raise TypeError(msg)
            return rhs_final

        if isinstance(rhs_final, TypeVal):
            if not self.contains(rhs_final):
                msg = f"Type inference failed: {rhs_final.value_key()} is not in {self.value_key()}"
                raise TypeError(msg)

            return rhs_final

        return rhs_final.conform(self)


@dataclass
class TypeDef(TypeVal, NamedValue):
    """IR Node representing the canonical definition of a type.

    The primary difference between this class and TypeVal is that this only holds a resolved type; it cannot hold a type expression, and it has a name within a scope.
    """


@dataclass
class SchemaType(TypeDef):
    """Base class for schema type definitions."""


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
    is_optional: bool = False


# fmt: off
@dataclass(slots=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class DeferrableType(Value):
# fmt: on
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


# fmt: off
@dataclass
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class GenericTypeVal(TypeVal):
# fmt: on
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
    def concrete_type_info(self) -> TypeVal | InferenceVar:
        """Delegate to the generic type being instantiated."""
        return self.instantiates.concrete_type_info()

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
        context: node.Module,
        numeric_type: NumericType = NumericType.NONE,
        cst_node: Any | None = None,  # noqa: ANN401 (Any is required for flexibility)
    ) -> None:
        """Create a new InferenceVar.

        Other than for unit testing, use the make() factory method instead of directly creating instances.

        Args:
            uniq_id: Unique identifier for this type variable
            context: Module context
            numeric_type: Numeric type constraint
            cst_node: CST node associated with this inference var, if any
        """
        self._id = uniq_id
        self.numeric_type = numeric_type
        # If a TypeVal, then that is the concrete type; if another InferenceVar, then we are equated to that var (and,
        # transitively, its resolution if any); if None then no resolution yet.
        self._resolution: TypeVal | InferenceVar | None = None
        self.context = context
        self.cst_node = cst_node

    @override
    def __repr__(self) -> str:
        """Return a string representation of the var."""
        return f"InferenceVar({self._id}, {self.numeric_type}, {self.resolution()})"

    @classmethod
    def make(
        cls: type[InferenceVar],
        context: node.Module,
        cst_node: Any | None = None,  # noqa: ANN401 (Any is required for flexibility)
        numeric_type: NumericType = NumericType.NONE,
    ) -> InferenceVar:
        """Construct a new InferenceVar in a given context.

        Args:
            context: The inference context (currently unused)
            cst_node: The CST node corresponding to the entity whose type is inferred (currently unused)
            numeric_type: The numeric type constraint
        """
        return cls(uniq_id=next(InferenceVar._ID_SEQ), context=context, numeric_type=numeric_type, cst_node=cst_node)

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

    def alternatives(self) -> Sequence[TypeVal]:
        """Get the set of types this variable is constrained to.

        If the variable is unconstrainted, then the empy set is returned.
        This is a helper for use in tests.
        """
        resolved = self.resolution()
        if isinstance(resolved, TypeVal):
            return resolved.alternatives()

        return ()

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
                lhs_set = lhs_final.alternatives()
                rhs_set = rhs_final.alternatives()
                intersection = _type_intersection(lhs_set, rhs_set)
                if len(intersection) == 0:
                    msg = node.append_error_line(
                        self.cst_node,
                        self.context,
                        f"Type inference failed: {_type_set_to_str(lhs_set)} and {_type_set_to_str(rhs_set)} are disjoint",
                    )
                    raise TypeError(msg)
                if len(intersection) == 1:
                    for typ in intersection:
                        return typ
                return TypeUnion.make(types=intersection)
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
            if not _compatible(resolution, to):
                msg = node.append_error_line(
                    self.cst_node, self.context, f"Type inference failed: {resolution} != {to}"
                )
                raise TypeError(msg)
            return to
        if not to.satisfies(resolution.numeric_type):
            msg = node.append_error_line(
                self.cst_node, self.context, f"Attempt to unify {resolution.numeric_type} type with {to}"
            )
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


def _compatible(lhs: TypeVal, rhs: TypeVal) -> bool:
    if isinstance(lhs, TypeUnion):
        return lhs.contains(rhs)

    if isinstance(rhs, TypeUnion):
        return rhs.contains(lhs)

    return lhs.value_key() == rhs.value_key()


def unify(
    lhs: TypeVal | DeferrableType | InferenceVar,
    rhs: TypeVal | InferenceVar,
    is_optional: bool = False,
) -> TypeVal | InferenceVar:
    """Unify two types or inference variables.

    Args:
        lhs: The left-hand side of unification; given slight preference in priority
        rhs: The right-hand side of unification
        is_optional: Whether this unification is occurring in the context of an optional parameter (i.e., whether the lhs is an optional parameter type bound)

    Returns:
        The unification result

    Raises:
        TypeError on failed unification.
    """
    if is_optional:
        assert isinstance(lhs, Instantiation)
        assert isinstance(lhs.arguments["type"], TypeVal | DeferrableType | InferenceVar)
        lhs = lhs.arguments["type"]
    if isinstance(lhs, DeferrableType):
        lhs = lhs.type_info
    if isinstance(lhs, InferenceVar):
        return lhs.unify(rhs)
    if isinstance(rhs, InferenceVar):
        return rhs.unify(lhs)

    lhs_set = lhs.alternatives()
    rhs_set = rhs.alternatives()
    intersection = _type_intersection(lhs_set, rhs_set)
    if len(intersection) == 0:
        msg = f"Type inference failed: {_type_set_to_str(lhs_set)} and {_type_set_to_str(rhs_set)} are disjoint"
        raise TypeError(msg)
    if len(intersection) == 1:
        for typ in intersection:
            return typ
    return TypeUnion.make(types=intersection)


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
    if isinstance(argument, AbsentOptionalValue) and parameter.is_optional:
        # An absent optional value is valid for any optional parameter; propagate as-is.
        return argument
    unify(parameter.type_bound, argument.concrete_type_info(), parameter.is_optional)

    return argument


def bind_args(parameters: Sequence[Parameter], args: Sequence[tuple[str | None, Value]]) -> dict[str, Value]:  # noqa: C901 TODO(OI-4817)
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
            if (param.is_optional and param.default is not None) or param.default is not None:
                result[param.name] = bind_arg(param, param.default)
            elif param.is_optional:
                result[param.name] = AbsentOptionalValue(type_info=param.type_bound)
            elif not param.is_optional:
                msg = f"No value specified for parameter {param.name}"
                raise ValueError(msg) from None
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

    def make_instance_for_resolution(  # noqa: PLR0913 (see above)
        self,
        *,
        cst_node: cst.NewStmt | None,
        module: node.Module,
        source_module: node.Module | None = None,
        scope: node.Scope,
        name: str,
        doc: node.Doc | None,
    ) -> node.NamedEntity:
        """Create an instance for use during box resolution.

        By default this is identical to make_instance, but can be overridden
        by types that need to defer side effects to the outermost make_instance call.
        """
        return self.make_instance(
            cst_node=cst_node,
            module=module,
            source_module=source_module,
            scope=scope,
            name=name,
            doc=doc,
        )

    @abstractmethod
    def get_module(self) -> node.Module:
        """Access the entity's module."""


class GenericCallable(ABC):
    """Represents a thing that can be called."""

    @abstractmethod
    def evaluate_call(
        self, *, ir_node: node.CstNode[cst.Expr], module: node.Module, args: Sequence[tuple[str | None, Value]]
    ) -> Value:
        """Evaluate the call operation."""


# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class CallableEntity(GenericCallable):
    """Base class for things which support call syntax."""


# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class MacroCallableEntity(GenericCallable):
    """Base class for things which support macro call syntax (trailing '!')."""


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
            print(parameters)
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


def _type_intersection(lhs: Sequence[TypeVal], rhs: Sequence[TypeVal]) -> Sequence[TypeVal]:
    """Helper for doing intersections on type sets that are represented as seqeuences."""
    key2type: dict[str, TypeVal] = {}
    for typ in lhs:
        key2type[typ.value_key()] = typ

    for typ in rhs:
        key2type[typ.value_key()] = typ

    intersection = frozenset({typ.value_key() for typ in lhs}) & frozenset({typ.value_key() for typ in rhs})
    return tuple(key2type[key] for key in sorted(intersection))


def _type_set_to_str(types: Sequence[TypeVal]) -> str:
    return "|".join(sorted(typ.value_key() for typ in types))
