# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Global registry for Signal-related entities."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Final

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.ir import cog, primitive
from typing_extensions import override

if TYPE_CHECKING:
    from clockwork.dsl.ir.signal import ResolvedSignal, SignalInstanceSpec


SIGNAL_KEY_TYPE = str  # Signal names are strings
SIGNAL_INSTANCE_KEY_TYPE = tuple[str, str]  # (signal_name, instance_name)


@dataclass(frozen=True, eq=True, slots=True)
class SignalInstanceInfo:
    """Represents a signal instance in the registry.

    Attributes:
        signal_ir: The resolved signal definition
        instance_name: The name of this specific signal instance
        cog_instance: The CogInstance that produces this signal (if any)
        cog_class: The Cog class associated with this signal instance (if any)
    """

    signal_ir: ResolvedSignal = field(compare=False, hash=False)
    instance_name: str
    cog_instance: cog.CogInstance | None = field(default=None, compare=False, hash=False)
    cog_class: cog.Cog | cog.InstantiatedCog | None = field(default=None, compare=False, hash=False)


class SignalRegistry(Context):
    """Registry for signals and their instances."""

    def __init__(self, name: str | None) -> None:
        """Create a new, empty signal registry."""
        self.name = name
        self.signal_registry: dict[SIGNAL_KEY_TYPE, ResolvedSignal] = {}
        self.instance_registry: dict[SIGNAL_INSTANCE_KEY_TYPE, SignalInstanceInfo] = {}

    @override
    def import_from(self, other: SignalRegistry) -> None:
        """Combine this registry with items from another.

        Raises:
            RuntimeError: If a signal or instance already exists with different info.
        """
        for key, signal_ir in other.signal_registry.items():
            if key in self.signal_registry:
                existing_ir = self.signal_registry[key]
                # Two different signals should not have the same signal_name
                if existing_ir is not signal_ir:
                    msg = f"Signal name '{key}' has conflicting signal info"
                    raise RuntimeError(msg)
            self.signal_registry[key] = signal_ir

        for key, instance_info in other.instance_registry.items():
            if key in self.instance_registry and self.instance_registry[key] != instance_info:
                msg = f"Signal instance '{key[0]}.{key[1]}' has conflicting instance info"
                raise RuntimeError(msg)
            self.instance_registry[key] = instance_info


def register_signal(compiler_context: CompilerContext, signal_ir: ResolvedSignal) -> None:
    """Register a signal in the context.

    Args:
        compiler_context: Compiler context containing the registry.
        signal_ir: The resolved signal to register.

    Raises:
        ValueError: If the signal name is already registered.
    """
    registry = compiler_context[SIGNAL_REGISTRY_KEY]
    signal_name = signal_ir.signal_name
    if signal_name in registry.signal_registry:
        existing_signal = registry.signal_registry[signal_name]
        msg = (
            f"Signal name '{signal_name}' is already registered.\n"
            f"Existing signal: {existing_signal.name}\n"
            f"New signal: {signal_ir.name}"
        )
        raise ValueError(msg)
    registry.signal_registry[signal_name] = signal_ir


def register_signal_instance(
    compiler_context: CompilerContext,
    signal_ir: ResolvedSignal,
    instance_name: str,
    cog_instance: cog.CogInstance | None = None,
) -> None:
    """Register a signal instance.

    Args:
        compiler_context: Compiler context containing the registry.
        signal_ir: The resolved signal.
        instance_name: The name of this specific instance.
        cog_instance: The cog instance that produces this signal (if any).

    Raises:
        ValueError: If the instance name is already registered for this signal, or if attempting
            to register multiple instances for a non-multi-instance signal.
        RuntimeError: If the signal is not yet registered.
    """
    registry = compiler_context[SIGNAL_REGISTRY_KEY]
    signal_name = signal_ir.signal_name

    # Verify signal is registered
    if signal_name not in registry.signal_registry:
        msg = f"Signal '{signal_name}' must be registered before registering instances"
        raise RuntimeError(msg)

    # Check for duplicate instance name
    key = (signal_name, instance_name)
    if key in registry.instance_registry:
        msg = (
            f"Instance name '{instance_name}' is already registered for signal '{signal_name}'.\n"
            f"Cannot register duplicate instance name."
        )
        raise ValueError(msg)

    # For non-multi-instance signals, verify this is the first (and only) instance
    if not signal_ir.multi_instance:
        existing_instances = [
            inst for (sig_name, _), inst in registry.instance_registry.items() if sig_name == signal_name
        ]
        if existing_instances:
            msg = (
                f"Signal '{signal_name}' is not marked as multi_instance and already has an instance "
                f"'{existing_instances[0].instance_name}'. Cannot register additional instance '{instance_name}'."
            )
            raise ValueError(msg)

    cog_class = cog_instance.cog_class if cog_instance is not None else None

    instance_info = SignalInstanceInfo(
        signal_ir=signal_ir,
        instance_name=instance_name,
        cog_instance=cog_instance,
        cog_class=cog_class,
    )
    registry.instance_registry[key] = instance_info


def lookup_signal(compiler_context: CompilerContext, signal_name: str) -> ResolvedSignal | None:
    """Look up a signal by name in the registry.

    Args:
        compiler_context: Compiler context containing the registry.
        signal_name: The name of the signal to look up.

    Returns:
        The resolved signal if found, else None.
    """
    registry = compiler_context[SIGNAL_REGISTRY_KEY]
    return registry.signal_registry.get(signal_name)


def lookup_signal_instance(
    compiler_context: CompilerContext, signal_name: str, instance_name: str
) -> SignalInstanceInfo | None:
    """Look up a signal instance in the registry.

    Args:
        compiler_context: Compiler context containing the registry.
        signal_name: The name of the parent signal.
        instance_name: The name of the instance.

    Returns:
        The instance info if found, else None.
    """
    registry = compiler_context[SIGNAL_REGISTRY_KEY]
    key = (signal_name, instance_name)
    return registry.instance_registry.get(key)


def get_signal_instances(compiler_context: CompilerContext, signal_name: str) -> list[SignalInstanceInfo]:
    """Get all instances registered for a given signal.

    Args:
        compiler_context: Compiler context containing the registry.
        signal_name: The name of the signal.

    Returns:
        List of all instances for the signal (empty list if none or signal doesn't exist).
    """
    registry = compiler_context[SIGNAL_REGISTRY_KEY]
    return [
        instance_info
        for (sig_name, _instance_name), instance_info in registry.instance_registry.items()
        if sig_name == signal_name
    ]


def get_signal_instances_from_spec(
    compiler_context: CompilerContext, signal_instance_spec: SignalInstanceSpec
) -> list[SignalInstanceInfo]:
    """Get all instances registered for a given signal instance specification.

    Args:
        compiler_context: Compiler context containing the registry.
        signal_instance_spec: The signal instance specification to look up.

    Returns:
        List of all instances matching the specification (empty list if none found).
    """
    resolved_signal = signal_instance_spec.signal.get_resolved()
    signal_name = resolved_signal.signal_name
    registry = compiler_context[SIGNAL_REGISTRY_KEY]

    # Wildcard: signal[*] - all instances of the signal
    if signal_instance_spec.is_wildcard():
        return get_signal_instances(compiler_context, signal_name)

    # String instance name: signal["instance_name"]
    if isinstance(signal_instance_spec.instance_key, primitive.StringValue):
        return [
            instance_info
            for (sig_name, _instance_name), instance_info in registry.instance_registry.items()
            if sig_name == signal_name and _instance_name == signal_instance_spec.instance_key.value
        ]

    # Cog instance: signal[cog_instance] - instances produced by this specific cog instance
    if isinstance(signal_instance_spec.instance_key, cog.CogInstance):
        return [
            instance_info
            for (sig_name, _), instance_info in registry.instance_registry.items()
            if sig_name == signal_name and instance_info.cog_instance is signal_instance_spec.instance_key
        ]

    # Cog class: signal[CogClass] - instances produced by any instance of this cog class
    if isinstance(signal_instance_spec.instance_key, cog.Cog):
        return [
            instance_info
            for (sig_name, _), instance_info in registry.instance_registry.items()
            if sig_name == signal_name and instance_info.cog_class is signal_instance_spec.instance_key
        ]

    return []


def get_all_signals(compiler_context: CompilerContext) -> list[ResolvedSignal]:
    """Get all signals registered in the system.

    Args:
        compiler_context: Compiler context containing the registry.

    Returns:
        List of all registered signals.
    """
    registry = compiler_context[SIGNAL_REGISTRY_KEY]
    return list(registry.signal_registry.values())


def get_all_signal_instances(compiler_context: CompilerContext) -> list[SignalInstanceInfo]:
    """Get all signal instances registered in the system.

    Args:
        compiler_context: Compiler context containing the registry.

    Returns:
        List of all registered signal instances.
    """
    registry = compiler_context[SIGNAL_REGISTRY_KEY]
    return list(registry.instance_registry.values())


def get_signals_by_cog_class(compiler_context: CompilerContext, cog_class: cog.Cog) -> list[SignalInstanceInfo]:
    """Get all signal instances produced by a specific cog class.

    Args:
        compiler_context: Compiler context containing the registry.
        cog_class: The cog class to filter by.

    Returns:
        List of signal instances produced by any instance of the specified cog class.
    """
    registry = compiler_context[SIGNAL_REGISTRY_KEY]
    return [
        instance_info for instance_info in registry.instance_registry.values() if instance_info.cog_class is cog_class
    ]


def get_unique_signal_instance_names(compiler_context: CompilerContext) -> list[str]:
    """Get a list of all unique signal instance names in the system.

    Args:
        compiler_context: Compiler context containing the registry.

    Returns:
        List of unique instance names across all signals.
    """
    registry = compiler_context[SIGNAL_REGISTRY_KEY]
    unique_names = {instance_info.instance_name for instance_info in registry.instance_registry.values()}
    return list(unique_names)


class SignalRegistryKey(ContextKey[SignalRegistry]):
    """Compiler context key for signal registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> SignalRegistry:
        """Create a default instance of the registry."""
        return SignalRegistry(compiler_context.name)


SIGNAL_REGISTRY_KEY: Final = SignalRegistryKey("SignalRegistry")
