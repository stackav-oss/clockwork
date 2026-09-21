# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for protobuf type registry."""

from __future__ import annotations

from typing import Final
from unittest.mock import MagicMock

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir import clkbuiltins, typesys
from clockwork.dsl.proto.proto_typereg import (
    ProtobufType,
    get_protobuf_type,
    register_protobuf_type,
)

MOCK_TYPE: Final = typesys.TypeDef(name="MockType", scope=MagicMock(), type_info=clkbuiltins.TYPE_TYPE)
MOCK_TYPE_PROTO: Final = ProtobufType(
    type_name="MockType",
    go_dep_label=None,
)


@pytest.fixture()
def compiler_context() -> CompilerContext:
    return CompilerContext()


def test_register_protobuf_type(compiler_context: CompilerContext) -> None:
    register_protobuf_type(MOCK_TYPE, MOCK_TYPE_PROTO, compiler_context)
    assert get_protobuf_type(MOCK_TYPE, compiler_context) == MOCK_TYPE_PROTO


def test_register_protobuf_type_existing_error(compiler_context: CompilerContext) -> None:
    """Registering an existing type should raise ValueError."""
    register_protobuf_type(MOCK_TYPE, MOCK_TYPE_PROTO, compiler_context)
    with pytest.raises(ValueError, match=r"already registered as ProtobufType"):
        register_protobuf_type(MOCK_TYPE, MOCK_TYPE_PROTO, compiler_context)


def test_get_protobuf_type_not_registered(compiler_context: CompilerContext) -> None:
    """Test getting Protobuf type info for an unregistered type should raise TypeError."""
    unregistered_clk_type = typesys.TypeDef(name="Unregistered", scope=MagicMock(), type_info=clkbuiltins.TYPE_TYPE)
    with pytest.raises(TypeError):
        get_protobuf_type(unregistered_clk_type, compiler_context)
