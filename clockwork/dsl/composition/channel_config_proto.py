# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to channel configurations."""

from typing import Protocol


class ChannelType(Protocol):
    """Fake enum type for values of ChannelType."""

    # The point is that the only way to get an instance of the enum is via the
    # dynamic runtime enum class.  So this just needs to be a protocol that
    # nothing can ever satisfy.
    _please_never_define_a_class_with_this_attribute: None


class ChannelTypeEnum(Protocol):
    """Type of pinion channel."""

    unspecified: ChannelType
    shared_memory: ChannelType
    gpu: ChannelType
