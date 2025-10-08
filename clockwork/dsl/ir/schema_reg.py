# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Global registry for Schema-related entities: Representations and Interfaces."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Final, TypeAlias

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.ir import clkbuiltins, interface, node, representation, schema, typesys
from typing_extensions import override

DEFAULT_REPRESENTATION_KEY: TypeAlias = tuple[int, str]


class DefaultRepresentationRegistry(Context):
    """Registry for default schema representations."""

    def __init__(self, name: str | None) -> None:
        """Create a new, empty default representation registry."""
        self.name = name
        self.registry: dict[DEFAULT_REPRESENTATION_KEY, representation.Representation] = {}

    @override
    def import_from(self, other: DefaultRepresentationRegistry) -> None:
        """Combine this registry with items from another.

        Raises:
            RuntimeError: If a type already exists with different options.
        """
        for key, other_repr in other.registry.items():
            if key in self.registry and self.registry[key] != other_repr:
                msg = f"Type {key} has conflicting default representation options"
                raise RuntimeError(msg)
            self.registry[key] = other_repr


def _default_representation_key(repr_type: typesys.TypeVal, schema_ir: schema.Schema) -> DEFAULT_REPRESENTATION_KEY:
    return (id(repr_type), schema_ir.value_key())


def register_default_representation_options(
    compiler_context: CompilerContext, representation_ir: representation.Representation
) -> None:
    """Register default representation options for a schema.

    Each schema may have only one set of default options for each representation
    type, and it must be defined in the same Clockwork module as the schema
    itself, which ensures that all modules see the same options for the
    representation.

    Named representations are not subject to this limitation and should not be
    registered through this function.
    """
    registry = compiler_context[DEFAULT_REPRESENTATION_REGISTRY_KEY]
    repr_type = representation_ir.get_repr_type()
    if repr_type is not clkbuiltins.TACHYON:
        msg = representation_ir.append_error_line("Only Tachyon representation options currently supported.")
        raise NotImplementedError(msg)
    schema_ir = representation_ir.get_schema()
    key = _default_representation_key(repr_type=repr_type, schema_ir=schema_ir)
    if key in registry.registry:
        msg = representation_ir.append_error_line(
            "Cannot define more than one default representation options for each representation/schema pair.\n"  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
            "You can make one of them a named (non-default) representation if you need more than one representation.",
        )
        raise ValueError(msg)
    if representation_ir.module is not schema_ir.module:
        msg = representation_ir.append_error_line(
            "Default representation options must be defined in the same module as the schema they are for.",
        )
        raise ValueError(msg)
    registry.registry[key] = representation_ir


def lookup_default_representation(
    compiler_context: CompilerContext,
    repr_type: typesys.TypeVal,
    schema_ir: schema.Schema,
) -> representation.Representation | None:
    """Look up the default representation options registered for a representation type and schema.

    Args:
        compiler_context: Compiler context containing the registry.
        repr_type: The representation type (e.g., clkbuiltins.TACHYON)
        schema_ir: The schema to look up default representation for

    Returns:
        The default representation or None if none is registered.
    """
    registry = compiler_context[DEFAULT_REPRESENTATION_REGISTRY_KEY]
    key = _default_representation_key(repr_type=repr_type, schema_ir=schema_ir)
    return registry.registry.get(key)


class DefaultRepresentationRegistryKey(ContextKey[DefaultRepresentationRegistry]):
    """Compiler context key for default representation registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> DefaultRepresentationRegistry:
        """Create a default instance of the registry."""
        return DefaultRepresentationRegistry(compiler_context.name)


DEFAULT_REPRESENTATION_REGISTRY_KEY: Final = DefaultRepresentationRegistryKey("DefaultRepresentationRegistry")


REPRESENTATION_KEY_TYPE: TypeAlias = str


@dataclass(frozen=True, eq=True, slots=True)
class RepresentationInfo:
    """Represents a schema representation in the registry.

    Attributes:
        representation_ir: The schema representation
        type_key: The computed unique key for this representation
    """

    representation_ir: representation.ResolvedReprInstantiation = field(compare=False, hash=False)
    type_key: REPRESENTATION_KEY_TYPE

    @classmethod
    def make(
        cls: type[RepresentationInfo],
        representation_ir: representation.ResolvedReprInstantiation,
    ) -> RepresentationInfo:
        """Construct a RepresentationInfo with computed key.

        Args:
            representation_ir: The representation to be registered

        Returns:
            An object for referencing this representation uniquely in the registry.
        """
        type_key = cls.key_for(representation_ir)
        return cls(representation_ir=representation_ir, type_key=type_key)

    @staticmethod
    def key_for(
        representation: representation.ResolvedReprInstantiation | representation.RepresentationReference,
    ) -> REPRESENTATION_KEY_TYPE:
        """Compute the registry key for the given representation."""
        if representation.schema_ir is None or not isinstance(representation.typespec, typesys.Instantiation):  # pyright: ignore[reportUnnecessaryComparison, reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            msg = node.enrich_error_if_possible(
                representation,
                f"Attempt to register unresolved representation: {representation}",
            )
            raise RuntimeError(msg)
        # Replace the schema arg with the InstantiatedSchema so that args are fully resolved
        instantiated = typesys.Instantiation(
            clkbuiltins.TYPE_TYPE,
            instantiates=representation.typespec.instantiates,
            arguments={"schema": representation.schema_ir},
        )
        return instantiated.value_key()


class RepresentationRegistry(Context):
    """Registry for schema representations."""

    def __init__(self, name: str | None) -> None:
        """Create a new, empty representation registry."""
        self.name = name
        self.registry: dict[REPRESENTATION_KEY_TYPE, RepresentationInfo] = {}

    @override
    def import_from(self, other: RepresentationRegistry) -> None:
        """Combine this registry with items from another.

        Raises:
            RuntimeError: If a type already exists with different representation info.
        """
        for key, representation_info in other.registry.items():
            if key in self.registry and self.registry[key] != representation_info:
                msg = f"Type {key} has conflicting representation info"
                raise RuntimeError(msg)
            self.registry[key] = representation_info


def register_representation(compiler_context: CompilerContext, representation_info: RepresentationInfo) -> None:
    """Registers a representation in the context.

    Args:
        compiler_context: Compiler context containing the registry.
        representation_info: Registry info for the representation.

    Raises:
        ValueError: If the representation is already registered.
    """
    registry = compiler_context[REPRESENTATION_REGISTRY_KEY]
    try:
        existing_type = registry.registry[representation_info.type_key]
        msg = f"Type {representation_info}\n\nalready registered as {existing_type}"
        raise ValueError(msg)
    except KeyError:
        pass
    registry.registry[representation_info.type_key] = representation_info


def lookup_representation(
    compiler_context: CompilerContext, representation: representation.RepresentationReference
) -> RepresentationInfo | None:
    """Look up a representation in the registry.

    Args:
        compiler_context: Compiler context containing the registry.
        representation: A reference to a representation.

    Returns:
        The registry entry if found, else None.
    """
    registry = compiler_context[REPRESENTATION_REGISTRY_KEY]
    try:
        return registry.registry[RepresentationInfo.key_for(representation)]
    except KeyError:
        return None


class RepresentationRegistryKey(ContextKey[RepresentationRegistry]):
    """Compiler context key for representation registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> RepresentationRegistry:
        """Create a default instance of the registry."""
        return RepresentationRegistry(compiler_context.name)


REPRESENTATION_REGISTRY_KEY: Final = RepresentationRegistryKey("RepresentationRegistry")


INTERFACE_KEY_TYPE: TypeAlias = tuple[int, REPRESENTATION_KEY_TYPE]


@dataclass(frozen=True, eq=True, slots=True)
class InterfaceInfo:
    """Represents a schema interface in the registry.

    Attributes:
        interface_ir: The schema interface
        type_key: The computed unique key for this interface
    """

    interface_ir: interface.InterfaceInstantiation = field(compare=False)
    type_key: INTERFACE_KEY_TYPE

    @classmethod
    def make(
        cls: type[InterfaceInfo],
        interface_ir: interface.InterfaceInstantiation,
    ) -> InterfaceInfo:
        """Construct a InterfaceInfo with computed key.

        Args:
            interface_ir: The interface to be registered

        Returns:
            An object for referencing this interface uniquely in the registry.
        """
        type_key = cls.key_for(interface_ir)
        return cls(interface_ir=interface_ir, type_key=type_key)

    @staticmethod
    def key_for(
        interface: interface.InterfaceInstantiation | interface.InterfaceReference,
    ) -> INTERFACE_KEY_TYPE:
        """Compute the registry key for the given interface."""
        if interface.representation is None or not isinstance(interface.typespec, typesys.Instantiation):
            msg = f"Attempt to register unresolved interface: {interface}"
            raise RuntimeError(msg)
        return (id(interface.typespec.instantiates), RepresentationInfo.key_for(interface.representation))


class InterfaceRegistry(Context):
    """Registry for schema interfaces."""

    def __init__(self, name: str | None) -> None:
        """Create a new, empty interface registry."""
        self.name = name
        self.registry: dict[INTERFACE_KEY_TYPE, InterfaceInfo] = {}

    @override
    def import_from(self, other: InterfaceRegistry) -> None:
        """Combine this registry with items from another.

        Raises:
            RuntimeError: If a type already exists with different interface info.
        """
        for key, interface_info in other.registry.items():
            if key in self.registry and self.registry[key] != interface_info:
                msg = f"Type {key} has conflicting interface info"
                raise RuntimeError(msg)
            self.registry[key] = interface_info


def register_interface(compiler_context: CompilerContext, interface_info: InterfaceInfo) -> None:
    """Registers an interface in the context.

    Args:
        compiler_context: Compiler context containing the registry.
        interface_info: Registry info for the interface.

    Raises:
        ValueError: If the interface is already registered.
    """
    registry = compiler_context[INTERFACE_REGISTRY_KEY]
    try:
        existing_type = registry.registry[interface_info.type_key]
        msg = f"Type {interface_info} already registered as {existing_type}"
        raise ValueError(msg)
    except KeyError:
        pass
    registry.registry[interface_info.type_key] = interface_info


def lookup_interface(
    compiler_context: CompilerContext, interface: interface.InterfaceReference
) -> InterfaceInfo | None:
    """Look up a interface in the registry.

    Args:
        compiler_context: Compiler context containing the registry.
        interface: A reference to a interface.

    Returns:
        The registry entry if found, else None.
    """
    registry = compiler_context[INTERFACE_REGISTRY_KEY]
    try:
        return registry.registry[InterfaceInfo.key_for(interface)]
    except KeyError:
        return None


class InterfaceRegistryKey(ContextKey[InterfaceRegistry]):
    """Compiler context key for interface registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> InterfaceRegistry:
        """Create a default instance of the registry."""
        return InterfaceRegistry(compiler_context.name)


INTERFACE_REGISTRY_KEY: Final = InterfaceRegistryKey("InterfaceRegistry")
