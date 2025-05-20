# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for pod module."""

from uuid import UUID

from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.cpp import values
from clockwork.dsl.ir import clkbuiltins


def test_uuid_to_byte_array() -> None:
    context = CompilerContext()
    value = UUID("0123456789abcdef0123456789abcdef")
    byte_str = ["0x01", "0x23", "0x45", "0x67", "0x89", "0xab", "0xcd", "0xef"] * 2
    assert (
        values.uuid_to_byte_array(context, value).render("") == f"::std::array<uint8_t, 16U>{{{', '.join(byte_str)}}}"
    )


def test_uuid_to_value() -> None:
    context = CompilerContext()
    uuid = UUID("0123456789abcdef0123456789abcdef")
    assert (
        values.uuid_to_value(context, uuid, tag=clkbuiltins.SCHEMA_TAG_TYPE).render("").strip()
        == """::jewels::Uuid<::clockwork::SchemaTag>{::std::array<uint8_t, 16U>{0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef}}"""
    )


def test_uuid_to_named_value() -> None:
    context = CompilerContext()
    uuid = UUID("0123456789abcdef0123456789abcdef")
    assert (
        values.uuid_to_named_value(context, uuid, "abcd", tag=clkbuiltins.REPRESENTATION_TAG_TYPE)
        .render("")
        .render_str()
        .strip()
        == """/// Class type UUID.
static constexpr ::jewels::Uuid<::clockwork::RepresentationTag> abcd{::std::array<uint8_t, 16U>{0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef}};"""
    )
