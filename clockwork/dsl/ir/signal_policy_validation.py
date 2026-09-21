# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Validation for signal policies."""

from __future__ import annotations

from collections import defaultdict
from typing import TYPE_CHECKING

from clockwork.dsl.ir import policy, primitive, signal
from clockwork.dsl.ir.cog import Cog, CogInstance

if TYPE_CHECKING:
    from clockwork.dsl.ir import node

WILDCARD_KEY = "*"
PolicyInstanceEntry = tuple[policy.PolicyData, str]


def _get_instance_key(target: signal.SignalInstanceSpec) -> str:
    """Extract the instance key from a signal instance spec.

    Args:
        target: The signal instance spec to extract the key from.

    Returns:
        WILDCARD_KEY for wildcard, string for named instances, or value_key for cog-based instances.

    Raises:
        TypeError: If the instance key type is not recognized.
    """
    if target.is_wildcard():
        return WILDCARD_KEY
    if isinstance(target.instance_key, primitive.StringValue):
        return target.instance_key.value
    if isinstance(target.instance_key, CogInstance | Cog):
        return target.instance_key.value_key()
    msg = f"Unexpected instance key type: {type(target.instance_key)}"
    raise TypeError(msg)


def _validate_multi_instance_signal_target(policy_data: policy.PolicyData, target: signal.Signal) -> None:
    """Validate that a policy doesn't target an entire multi-instance signal.

    Args:
        policy_data: The policy data being validated.
        target: The signal being targeted.

    Raises:
        ValueError: If the signal is multi-instance and no instance is specified.
    """
    resolved_sig = target.get_resolved()
    if resolved_sig.multi_instance:
        msg = (
            f"Policy '{policy_data.policy_class.name}' targets multi-instance signal "
            f"'{target.fqn}' without specifying an instance. "
            f'Use signal["instance_name"] or signal[*] to target specific instances.'
        )
        raise ValueError(msg)


def _check_for_conflicts(policies_list: list[PolicyInstanceEntry]) -> None:
    """Check for policy conflicts within a list of policies for the same signal.

    Args:
        policies_list: List of (policy_data, instance_key) tuples. instance_key is WILDCARD_KEY for wildcard.

    Raises:
        ValueError: If a conflict is detected.
    """
    wildcard_policy: policy.PolicyData | None = None
    specific_instances: dict[str, policy.PolicyData] = {}

    for policy_data, instance_key in policies_list:
        target = policy_data.target
        assert isinstance(target, signal.SignalInstanceSpec)

        if instance_key == WILDCARD_KEY:
            if wildcard_policy is not None:
                msg = (
                    f"Policy '{policy_data.policy_class.name}' applies to signal "
                    f"'{target.signal.fqn}' instance '{WILDCARD_KEY}' more than once"
                )
                raise ValueError(msg)
            wildcard_policy = policy_data
        elif instance_key in specific_instances:
            msg = (
                f"Policy '{policy_data.policy_class.name}' applies to signal "
                f"'{target.signal.fqn}' instance '{instance_key}' more than once"
            )
            raise ValueError(msg)
        else:
            specific_instances[instance_key] = policy_data

    # Wildcard + specific instance = conflict
    if wildcard_policy is not None and specific_instances:
        instance_key, specific_policy = next(iter(specific_instances.items()))
        target = specific_policy.target
        assert isinstance(target, signal.SignalInstanceSpec)
        msg = (
            f"Policy '{specific_policy.policy_class.name}' applies to signal "
            f"'{target.signal.fqn}' instance '{instance_key}' more than once"
        )
        raise ValueError(msg)


def validate_signal_policies(module: node.Module) -> None:
    """Validate signal policies in a module.

    Note that this must be called from the top level system target in order to
    have access to all instances in the system.

    Checks that:
    1. The same policy type doesn't apply to the same signal instance more than once
       (including considering wildcards that would overlap with specific instances)
    2. Policies don't target a multi-instance signal without selecting an instance

    Args:
        module: The module to validate.

    Raises:
        ValueError: If validation fails.
    """
    # Group policies by (policy_class_key, signal_key)
    grouped: defaultdict[tuple[str, str], list[PolicyInstanceEntry]] = defaultdict(list)

    for policy_data in policy.iterate_all_policy_data(module):
        target = policy_data.target

        if isinstance(target, signal.Signal):
            # Direct signal target (not via SignalInstanceSpec)
            _validate_multi_instance_signal_target(policy_data, target)
        elif isinstance(target, signal.SignalInstanceSpec):
            instance_key = _get_instance_key(target)
            group_key = (policy_data.policy_class.value_key(), target.signal.value_key())
            grouped[group_key].append((policy_data, instance_key))

    for policies_list in grouped.values():
        _check_for_conflicts(policies_list)
