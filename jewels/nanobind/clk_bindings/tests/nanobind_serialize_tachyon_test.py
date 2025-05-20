# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for serializing/deserializing tachyon."""

import pickle
import re
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import TYPE_CHECKING

import pytest
from clockwork.dsl.ir.module_id import CLK_REPO
from clockwork.serialization.py import protocol
from jewels.nanobind.clk_bindings.tests.support.foo_clk_nb import Foo
from jewels.nanobind.clk_bindings.tests.support.foo_clk_py import Foo as FooPy

if TYPE_CHECKING:
    from clockwork.dsl.serialization.tachyon_reg import FieldConstraint
    from clockwork.serialization.metadata.tachyon_model import TachyonMetadata


def test_serialize() -> None:
    """Test serialize_tachyon()."""
    assert Foo.get_tachyon_constraint().size == 272

    foo = Foo(
        question="What is the result of multiplying six by seven?",
        answer=42,
    )
    serialization_bytearray = bytearray(Foo.get_tachyon_constraint().size)
    foo.serialize_tachyon(memoryview(serialization_bytearray))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    serialized_bytes = bytes(serialization_bytearray)
    assert (
        serialized_bytes
        == b"What is the result of multiplying six by seven?\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00/\x00\x00\x00\x00\x00\x00\x00*\x00\x00\x00\x00\x00\x00\x00"
    )

    assert foo == Foo.deserialize_tachyon(memoryview(serialized_bytes))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

    # Test pickle round trip.
    # Don't look at the pickled bytes because they're not gonna match the tachyon bytes.
    # Pickle uses its own protocol.
    assert foo == pickle.loads(pickle.dumps(foo))  # noqa: S301


def test_bad_size() -> None:
    """Test errors on bad sizes."""
    foo = Foo(
        question="What is the result of multiplying six by seven?",
        answer=42,
    )
    serialization_bytearray = bytearray(Foo.get_tachyon_constraint().size + 2)
    with pytest.raises(
        TypeError,
        match=re.escape("""\
serialize_tachyon(): incompatible function arguments. The following argument types are supported:
    1. serialize_tachyon(self, buffer: ndarray[dtype=uint8, shape=(272)]) -> None

Invoked with types: jewels.nanobind.clk_bindings.tests.support.foo_clk_nb.Foo, memoryview"""),
    ):
        foo.serialize_tachyon(memoryview(serialization_bytearray))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

    with pytest.raises(
        TypeError,
        match=re.escape("""\
deserialize_tachyon(): incompatible function arguments. The following argument types are supported:
    1. deserialize_tachyon(buffer: ndarray[dtype=uint8, shape=(272), writable=False]) -> jewels.nanobind.clk_bindings.tests.support.foo_clk_nb.Foo

Invoked with types: memoryview"""),
    ):
        Foo.deserialize_tachyon(memoryview(serialization_bytearray))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip


def test_tachyon_metadata() -> None:
    """Check that the tachyon metadata came through correctly for Foo."""
    assert Foo.get_tachyon_metadata_name() == f"@{CLK_REPO}::jewels::nanobind::clk_bindings::tests::support::foo::Foo"
    tachyon_metadata: TachyonMetadata = Foo.get_tachyon_metadata()  # pyright: ignore[reportAssignmentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert tachyon_metadata == FooPy.get_tachyon_metadata()


def test_tachyon_constraint() -> None:
    """Check that the tachyon constraint came through correctly for Foo."""
    tachyon_constraint: FieldConstraint = Foo.get_tachyon_constraint()  # pyright: ignore[reportAssignmentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert tachyon_constraint == FooPy.get_tachyon_constraint()


def test_protocol() -> None:
    """Test that we can write to and read from a file.

    This statically ensures that nanobindings are compatible with the Tachyon protocol.
    It also dynamically checks that it works by roundtripping the protocol-based functions
      'write_tachyon_to_file'/'read_tachyon_from_file'.
    """
    foo = Foo(
        question="What is the answer to the Ultimate Question of Life, The Universe, and Everything?",
        answer=42,
    )

    with TemporaryDirectory() as temp_dir:
        ser_path = temp_dir / Path("serialized_foo")
        protocol.write_tachyon_to_file(foo, ser_path)  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

        foo_loaded = protocol.read_tachyon_from_file(Foo, ser_path)  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

    assert foo == foo_loaded
