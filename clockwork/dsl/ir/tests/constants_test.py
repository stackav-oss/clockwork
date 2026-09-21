# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for ir.constant."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Final

import pytest
from clockwork.dsl.ir import clkbuiltins, compiler, cpp_target, py_target, schema, statement, typesys
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.tests.support.py_test_utils import fix_clockwork_path


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_constants(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, fix_clockwork_path(Path("clockwork/dsl/tests/support/constants.clk"))), fs_importer
    )
    assert module is not None

    a_number_ir = module.inner_scope.lookup("a_number", recursive=False)
    assert isinstance(a_number_ir, statement.ImmutableBinding)
    assert isinstance(a_number_ir.type_info, typesys.InferenceVar)
    assert a_number_ir.type_info.resolution() is clkbuiltins.UINT64

    another_number_ir = module.inner_scope.lookup("another_number", recursive=False)
    assert isinstance(another_number_ir, statement.ImmutableBinding)
    assert isinstance(another_number_ir.type_info, typesys.InferenceVar)
    assert another_number_ir.type_info.resolution() is clkbuiltins.UINT64

    a_string_ir = module.inner_scope.lookup("a_string", recursive=False)
    assert isinstance(a_string_ir, statement.ImmutableBinding)
    assert isinstance(a_string_ir.type_info, typesys.InferenceVar)
    assert a_string_ir.type_info.resolution() is clkbuiltins.STRING

    bar_ir = module.inner_scope.lookup("BarMessage", recursive=False)
    assert isinstance(bar_ir, schema.Schema)
    bar_resolved = bar_ir.get_resolved()
    holder_field = bar_resolved.fields[1]
    assert isinstance(holder_field.type_info, typesys.Instantiation)
    assert isinstance(holder_field.type_info.arguments["max_things"], statement.ImmutableBinding)

    cpp_target_ir = module.inner_scope.lookup("dummy", recursive=False)
    assert cpp_target_ir is not None
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    cpp_chunks = cpp_target_ir.render_cpp_entities()
    assert (
        cpp_chunks.header_chunk.render_str(render_includes=True)
        == """#include <cstdint>
#include <string_view>
namespace dummy
{
inline constexpr bool a_bool{false};
inline constexpr float a_float{1.234f};
/// A string.
inline constexpr ::std::string_view a_string{"very interesting and useful string"};
/// A constant with an inferred type.
inline constexpr uint64_t another_number{456U};
} // namespace dummy
namespace dummy
{
// Interface and instantiation aliases
} // namespace dummy
"""
    )

    py_target_ir = module.inner_scope.lookup("dummy_py", recursive=False)
    assert py_target_ir is not None
    assert isinstance(py_target_ir, py_target.PyTarget)
    assert (
        py_target_ir.render_to_str()
        == """import pathlib
import typing

from clockwork.dsl.ir.module_id import ModuleID
import clockwork.dsl.ir.compiler
import clockwork.dsl.ir.importer
import clockwork.serialization.py.tachyon_dyn

_module = clockwork.dsl.ir.compiler.compile_source_file(ModuleID.from_path("clockwork", pathlib.Path("clockwork/dsl/tests/support/constants.clk")), clockwork.dsl.ir.importer.FilesystemImporter(compile_fn=clockwork.dsl.ir.compiler.compile_source_file))
A_BOOL: typing.Final = False
A_FLOAT: typing.Final = 1.234
\"\"\"A string.\"\"\"
A_STRING: typing.Final = "very interesting and useful string"
\"\"\"A constant with an inferred type.\"\"\"
ANOTHER_NUMBER: typing.Final = 456
"""
    )


def test_clk_constants(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, fix_clockwork_path(Path("clockwork/dsl/tests/support/clk_constants.clk"))),
        fs_importer,
    )
    assert module is not None

    a_number_ir = module.inner_scope.lookup("a_number", recursive=False)
    assert isinstance(a_number_ir, statement.ImmutableBinding)
    assert isinstance(a_number_ir.type_info, typesys.InferenceVar)
    assert a_number_ir.type_info.resolution() is clkbuiltins.UINT64

    another_number_ir = module.inner_scope.lookup("another_number", recursive=False)
    assert isinstance(another_number_ir, statement.ImmutableBinding)
    assert isinstance(another_number_ir.type_info, typesys.InferenceVar)
    assert another_number_ir.type_info.resolution() is clkbuiltins.UINT32

    a_string_ir = module.inner_scope.lookup("a_string", recursive=False)
    assert isinstance(a_string_ir, statement.ImmutableBinding)
    assert isinstance(a_string_ir.type_info, typesys.InferenceVar)
    assert a_string_ir.type_info.resolution() is clkbuiltins.STRING

    cpp_target_ir = module.inner_scope.lookup("clk_constants_clk_cc", recursive=False)
    assert cpp_target_ir is not None
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    cpp_chunks = cpp_target_ir.render_cpp_entities()
    assert (
        cpp_chunks.header_chunk.render_str(render_includes=True)
        == """#include <cstdint>
#include <string_view>
namespace dummy
{
/// A constant with an explicit type.
inline constexpr uint64_t a_number{123U};
/// A constant with an inferred type.
inline constexpr uint32_t another_number{456U};
/// A string.
inline constexpr ::std::string_view a_string{"very interesting and useful string"};
inline constexpr bool a_bool{false};
inline constexpr float a_float{1.234f};
} // namespace dummy
namespace dummy
{
// Interface and instantiation aliases
} // namespace dummy
"""
    )


def test_constant_via_use(fs_importer: FilesystemImporter) -> None:
    source = """
use clockwork::dsl::tests::support::constants;

// Doc.
schema Message
{
  fields
  {
    // Doc.
    #0 value: VarArray<UInt32, constants::a_number>;
  }
}

cpp_target dummy
{
  options
  {
    namespace dummy;
  }

  schema Message;
  representation Tachyon<Message>;
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    assert module is not None
    message_ir = module.inner_scope.lookup("Message", recursive=False)
    assert isinstance(message_ir, schema.Schema)
    resolved = message_ir.get_resolved()
    assert isinstance(resolved.fields[0].type_info, typesys.Instantiation)
    assert isinstance(resolved.fields[0].type_info.arguments["max_size"], statement.ImmutableBinding)

    cpp_target_ir = module.inner_scope.lookup("dummy", recursive=False)
    assert cpp_target_ir is not None
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    cpp_target_ir.render_cpp_entities()


def test_invalid_constants_type_mismatch(fs_importer: FilesystemImporter) -> None:
    source: Final = """
// Whoopsie.
mismatched: UInt32 = "foo";
"""
    with pytest.raises(
        TypeError,
        match="Type inference failed",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_invalid_constants_name_collision(fs_importer: FilesystemImporter) -> None:
    source = """
// Doc.
already_exists: UInt32 = 1234;

// Whoopsie.
already_exists: UInt32 = 5678;
"""
    with pytest.raises(ValueError, match=re.escape('Redefinition of name "already_exists"')):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)

    source = """
// Doc.
schema Message
{
  fields
  {
    // Doc.
    #0 value: UInt32;
  }
}

cpp_target already_exists
{
    options
    {
        namespace foo;
    }
	schema Message;
}

// Whoopsie.
already_exists: UInt32 = 5678;
"""
    with pytest.raises(ValueError, match=re.escape('Redefinition of name "already_exists"')):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_invalid_constants_bad_inference(fs_importer: FilesystemImporter) -> None:
    source = """
// Doc.
a_float: Float32 = 1234.0;

// Doc.
schema Foo
{
	parameters
	{
        // Doc.
        #1 max_things = a_float;
	}

	fields
  {
    // Doc.
    #2 things: VarArray<Int32, max_things>;
  }
}
"""
    with pytest.raises(
        TypeError,
        match="Type inference failed",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)

    source = """
// Doc.
a_string = "cool string";

// Doc.
schema Foo
{
	parameters
	{
        // Doc.
        #1 max_things: UInt64;
	}

    fields
    {
        // Doc.
        #2 things: VarArray<Int32, max_things>;
    }
}

// Doc.
schema Bar
{
    fields
    {
        // Doc.
        #1 foo: Foo<a_string>;
    }
}
"""
    with pytest.raises(
        TypeError,
        match="Type inference failed",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_invalid_target_export(fs_importer: FilesystemImporter) -> None:
    source: Final = """
num: UInt32 = 123;

cpp_target bad
{
    options
    {
        namespace bad;
    }
    constant num;
    constant num;
}
"""
    with pytest.raises(
        ValueError,
        match=re.escape("Constant num is already included in target bad"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
