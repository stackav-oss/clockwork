# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for protobuf type registry."""

from __future__ import annotations

from typing import Final
from unittest.mock import MagicMock

import pytest
from clockwork.dsl.ir import clkbuiltins, typesys
from clockwork.dsl.proto.proto_typereg import (
    ProtobufType,
    get_protobuf_type,
    register_protobuf_type,
)

MOCK_TYPE: Final = typesys.TypeDef(name="MockType", scope=MagicMock(), type_info=clkbuiltins.TYPE_TYPE)
MOCK_TYPE_PROTO: Final = ProtobufType(
    type_name="MockType",
)
# We can't put this in a test case because the type registry is global, so this
# needs to be done once.
register_protobuf_type(MOCK_TYPE, MOCK_TYPE_PROTO)
assert get_protobuf_type(MOCK_TYPE) == MOCK_TYPE_PROTO


def test_register_protobuf_type_existing_error() -> None:
    """Registering an existing type should raise ValueError."""
    with pytest.raises(ValueError, match=r"already registered as ProtobufType"):
        register_protobuf_type(MOCK_TYPE, MOCK_TYPE_PROTO)


def test_get_protobuf_type_not_registered() -> None:
    """Test getting Protobuf type info for an unregistered type should raise TypeError."""
    unregistered_clk_type = typesys.TypeDef(name="Unregistered", scope=MagicMock(), type_info=clkbuiltins.TYPE_TYPE)
    with pytest.raises(TypeError):
        get_protobuf_type(unregistered_clk_type)
