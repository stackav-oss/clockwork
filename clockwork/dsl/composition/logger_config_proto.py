# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to logger configurations."""

from dataclasses import dataclass
from typing import Protocol
from uuid import UUID

from clockwork.serialization.py.protocol import Tachyon


class MessageEncoding(Protocol):
    """Fake enum type for values of MessageEncoding."""

    # The point is that the only way to get an instance of the enum is via the
    # dynamic runtime enum class.  So this just needs to be a protocol that
    # nothing can ever satisfy.
    _please_never_define_a_class_with_this_attribute_message_encoding: None


class MessageEncodingEnum(Protocol):
    """Message encoding."""

    unspecified: MessageEncoding
    undefined: MessageEncoding
    cdr: MessageEncoding
    tachyon: MessageEncoding


class ChannelType(Protocol):
    """Fake enum type for values of ChannelType."""

    # The point is that the only way to get an instance of the enum is via the
    # dynamic runtime enum class.  So this just needs to be a protocol that
    # nothing can ever satisfy.
    _please_never_define_a_class_with_this_attribute_channel_type: None


class ChannelTypeEnum(Protocol):
    """Type of channel."""

    regular: ChannelType
    persistent: ChannelType


class SchemaEncoding(Protocol):
    """Fake enum type for values of SchemaEncoding."""

    # The point is that the only way to get an instance of the enum is via the
    # dynamic runtime enum class.  So this just needs to be a protocol that
    # nothing can ever satisfy.
    _please_never_define_a_class_with_this_attribute_schema_encoding: None


class SchemaEncodingEnum(Protocol):
    """Schema encoding."""

    unspecified: SchemaEncoding
    undefined: SchemaEncoding
    ros2msg: SchemaEncoding
    ros2idl: SchemaEncoding
    clockwork_tachyon: SchemaEncoding
    clockwork_tachyon_zstd: SchemaEncoding


class LogType(Protocol):
    """Fake enum type for values of LogType."""

    # The point is that the only way to get an instance of the enum is via the
    # dynamic runtime enum class.  So this just needs to be a protocol that
    # nothing can ever satisfy.
    _please_never_define_a_class_with_this_attribute_log_type: None


class LogTypeEnum(Protocol):
    """LogType."""

    none: LogType
    event: LogType
    non_redundant_telemetry: LogType
    redundant_telemetry: LogType


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class LoggedChannelConfig(Tachyon["LoggedChannelConfig"]):
# fmt: on
    """Logged channel configuration."""

    uuid: UUID
    num_slots: int
    message_size: int
    channel_name: str
    message_encoding: MessageEncoding
    schema_name: str
    schema_encoding: SchemaEncoding
    schema_definition: list[int]
    channel_type: ChannelType


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class LogWriterConfig(Tachyon["LogWriterConfig"]):
# fmt: on
    """Log writer configuration."""

    channels: list[LoggedChannelConfig]
