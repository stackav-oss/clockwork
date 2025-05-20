# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for cpp.context."""

import re
from pathlib import Path, PurePath

import pytest
from clockwork.dsl.bazel.cc_targets import CcBinary, CcBinaryWithEmbeddedPy, CcLibrary
from clockwork.dsl.bazel.targets import Label
from clockwork.dsl.cpp.context import (
    CppChunk,
    CppContext,
    CppModuleChunks,
    FwdDecl,
    Header,
    SystemHeader,
    as_cc_binary,
    as_cc_binary_with_embedded_py,
    as_cc_library,
    comment_doc_string,
)


def test_comment_doc_string() -> None:
    expected = ["/// hello", "/// world"]
    assert comment_doc_string("hello\nworld") == expected


def test_header_creation() -> None:
    hdr = Header("repo", "path/to/header.hh")
    assert isinstance(hdr.path, PurePath)
    assert hdr.path == PurePath("path/to/header.hh")
    assert hdr.render() == '#include "path/to/header.hh"'
    assert hdr.is_system is False


def test_system_header() -> None:
    sys_hdr = SystemHeader("path/to/system_header")
    assert sys_hdr.is_system is True
    assert sys_hdr.render() == "#include <path/to/system_header>"


def test_fwd_decl() -> None:
    fwd_decl = FwdDecl("some::scope", "struct SomeType")
    assert fwd_decl.render() == "namespace some::scope { struct SomeType; } // IWYU pragma: keep"


def test_header_sort_order() -> None:
    user_header1 = Header("repo", "aaa.hh")
    user_header2 = Header("repo", "bbb.hh")
    system_header1 = SystemHeader("aaa")
    system_header2 = SystemHeader("bbb")
    fwd_decl1 = FwdDecl("scopeA", "declA")
    fwd_decl2 = FwdDecl("scopeA", "declB")
    fwd_decl3 = FwdDecl("scopeB", "declC")

    # Test sorting with mixed header types
    mixed_headers = [
        system_header1,
        user_header1,
        system_header2,
        user_header2,
        fwd_decl1,
        fwd_decl2,
        fwd_decl3,
    ]
    mixed_headers.sort()
    assert mixed_headers == [
        user_header1,
        user_header2,
        system_header1,
        system_header2,
        fwd_decl1,
        fwd_decl2,
        fwd_decl3,
    ], "Order: headers, system headers, fwd decls"

    # Test sorting with same type - System headers
    system_headers = [system_header2, system_header1]
    system_headers.sort()
    assert system_headers == [system_header1, system_header2], "System headers should sort lexicographically"

    # Test sorting with same type - User headers
    user_headers = [user_header2, user_header1]
    user_headers.sort()
    assert user_headers == [user_header1, user_header2], "User headers should sort lexicographically"

    assert user_header1.__lt__(system_header1)
    assert not system_header1.__lt__(user_header1)

    assert not user_header1 < user_header1  # noqa: PLR0124 testing the actual __lt__ method
    assert user_header1 < user_header2
    assert not user_header2 < user_header1

    assert not system_header1 < system_header1  # noqa: PLR0124 testing the actual __lt__ method
    assert system_header1 < system_header2
    assert not system_header2 < system_header1
    assert user_header1 < system_header1
    assert not system_header1 < user_header1

    assert not fwd_decl1 < fwd_decl1  # noqa: PLR0124 testing the actual __lt__ method
    assert fwd_decl1 < fwd_decl2
    assert fwd_decl1 < fwd_decl3
    assert fwd_decl2 < fwd_decl3
    assert not fwd_decl2 < fwd_decl1
    assert not fwd_decl3 < fwd_decl1
    assert not fwd_decl3 < fwd_decl2
    assert user_header1 < fwd_decl1
    assert not fwd_decl1 < user_header1
    assert system_header1 < fwd_decl1
    assert not fwd_decl1 < system_header1


def test_cpp_context_includes() -> None:
    context = CppContext()

    hdr = SystemHeader("aaa")
    context.add_include(hdr)
    assert hdr in context.includes

    hdr2 = Header("repo", "zzz.hh")
    context.add_includes([hdr, hdr2])
    assert hdr2 in context.includes
    assert len(context.includes) == 2

    rendered_includes = list(context.render_includes())
    # Sort order puts system headers after user headers
    expected = ['#include "zzz.hh"', "#include <aaa>"]
    assert rendered_includes == expected

    context.remove_include(hdr)
    assert hdr not in context.includes
    assert list(context.render_includes()) == ['#include "zzz.hh"']


def test_cpp_chunk_append_str() -> None:
    chunk = CppChunk()
    chunk.append("int main() {}")
    assert "int main() {}" in chunk.lines
    assert chunk.produce is True


def test_cpp_chunk_append_list() -> None:
    cpp_chunk = CppChunk()
    lines = ["int x = 0;", "x++;"]
    cpp_chunk.append(lines, causes_production=True)
    assert cpp_chunk.lines == lines, "CppChunk should include the appended list of strings."
    assert cpp_chunk.produce is True, "Adding lines should set produce to True."

    rendered_str = cpp_chunk.render_str()
    expected_str = "int x = 0;\nx++;\n"
    assert rendered_str == expected_str, "render_str() should correctly join lines with a newline."


def test_cpp_chunk_empty_render() -> None:
    cpp_chunk = CppChunk()
    assert cpp_chunk.render_str() == "", "An empty chunk should not include a newline."


def test_cpp_chunk_append_chunk() -> None:
    chunk1 = CppChunk()
    chunk1.append("namespace test {}")

    chunk2 = CppChunk()
    chunk2.append(chunk1)
    assert "namespace test {}" in chunk2.lines
    assert chunk2.produce is True


def test_cpp_module_chunks_append() -> None:
    header_content = CppChunk()
    header_content.append("#pragma once", causes_production=False)

    module_chunks = CppModuleChunks()
    module_chunks.append(header_content, header_indent=1)

    assert "    #pragma once" in module_chunks.header_chunk.lines
    assert not module_chunks.header_chunk.produce


def test_cpp_module_chunks_append_with_module() -> None:
    chunk1 = CppChunk()
    chunk1.append("void function() {}", causes_production=True)
    mod_chunk1 = CppModuleChunks(header_chunk=chunk1)

    mod_chunk2 = CppModuleChunks()
    mod_chunk2.append(mod_chunk1)

    assert "void function() {}" in mod_chunk2.header_chunk.lines
    assert mod_chunk2.header_chunk.produce is True
    assert mod_chunk2.inline_chunk.produce is False
    assert mod_chunk2.implementation_chunk.produce is False


def test_cpp_module_chunks_with_str_and_list() -> None:
    module_chunks = CppModuleChunks()
    module_chunks.append("void myFunc();", header_indent=1)
    module_chunks.append(["#include <iostream>", "using namespace std;"], implementation_indent=0)

    header_content = module_chunks.header_chunk.render_str()
    implementation_content = module_chunks.implementation_chunk.render_str()

    assert header_content == "    void myFunc();\n#include <iostream>\nusing namespace std;\n"
    assert implementation_content == "void myFunc();\n#include <iostream>\nusing namespace std;\n"


def test_cpp_module_chunks_unexpected_causes_production() -> None:
    module_chunks = CppModuleChunks()

    with pytest.raises(
        ValueError,
        match=re.escape("Cannot specify causes_production in combination with CppChunk or CppModuleChunks."),
    ):
        module_chunks.append(CppChunk(), causes_production=True)


def test_as_cc_library_nothing_produced() -> None:
    header_a = Header("repo", "a.hh")
    cpp_mod = CppModuleChunks()
    cpp_mod.header_chunk.context.add_include(header_a)

    name = "target"
    fake_package = Path("a/b/c")
    # Always add the deps, because the files are always produced.
    assert as_cc_library(cpp_mod, name, fake_package, "repo") == CcLibrary(
        name=name,
        hdrs=[Path(name + ".hh")],
        srcs=[Path(name + ".inl"), Path(name + ".cc")],
        deps=[Label("//:a")],
        data=[],
    )


def test_as_cc_library() -> None:
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")
    header_c = Header("repo", "c.hh")
    cpp_mod = CppModuleChunks()
    cpp_mod.header_chunk.context.add_include(header_a)
    cpp_mod.header_chunk.append("a")
    cpp_mod.inline_chunk.context.add_include(header_b)
    cpp_mod.inline_chunk.append("b")
    cpp_mod.implementation_chunk.context.add_include(header_c)
    cpp_mod.implementation_chunk.append("c")

    name = "target"
    fake_package = Path("a/b/c")
    assert as_cc_library(cpp_mod, name, fake_package, "repo") == CcLibrary(
        name=name,
        hdrs=[Path(name + ".hh")],
        srcs=[Path(name + ".inl"), Path(name + ".cc")],
        deps=[
            Label("//:a"),
            Label("//:b"),
            Label("//:c"),
        ],
        data=[],
    )


def test_as_cc_library_self_referencing() -> None:
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")
    header_c = Header("repo", "c.hh")
    cpp_mod = CppModuleChunks()
    cpp_mod.header_chunk.context.add_include(header_a)
    cpp_mod.header_chunk.append("a")
    cpp_mod.inline_chunk.context.add_include(header_b)
    cpp_mod.inline_chunk.append("b")
    cpp_mod.implementation_chunk.context.add_include(header_c)
    cpp_mod.implementation_chunk.append("c")

    name = "target"
    fake_package = Path("a/b/c")
    cpp_mod.implementation_chunk.context.add_include(Header("repo", str(fake_package / (name + ".hh"))))
    assert as_cc_library(cpp_mod, name, fake_package, "repo") == CcLibrary(
        name=name,
        hdrs=[Path(name + ".hh")],
        srcs=[Path(name + ".inl"), Path(name + ".cc")],
        deps=[
            Label("//:a"),
            Label("//:b"),
            Label("//:c"),
        ],
        data=[],
    )


def test_as_cc_binary() -> None:
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")
    header_c = Header("repo", "c.hh")
    cpp_mod = CppModuleChunks()
    cpp_mod.header_chunk.context.add_include(header_a)
    cpp_mod.header_chunk.append("a")
    cpp_mod.inline_chunk.context.add_include(header_b)
    cpp_mod.inline_chunk.append("b")
    cpp_mod.implementation_chunk.context.add_include(header_c)
    cpp_mod.implementation_chunk.append("c")

    name = "target"
    fake_package = Path("a/b/c")
    assert as_cc_binary(cpp_mod, name, fake_package, "repo") == CcBinary(
        name=name,
        srcs=[Path(name + ".hh"), Path(name + ".inl"), Path(name + ".cc")],
        deps=[
            Label("//:a"),
            Label("//:b"),
            Label("//:c"),
        ],
        data=[],
    )


def test_as_cc_binary_with_embedded_py() -> None:
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")
    header_c = Header("repo", "c.hh")
    cpp_mod = CppModuleChunks()
    cpp_mod.header_chunk.context.add_include(header_a)
    cpp_mod.header_chunk.append("a")
    cpp_mod.inline_chunk.context.add_include(header_b)
    cpp_mod.inline_chunk.append("b")
    cpp_mod.implementation_chunk.context.add_include(header_c)
    cpp_mod.implementation_chunk.append("c")

    name = "target"
    fake_package = Path("a/b/c")
    py_deps = [Label("//:d"), Label("//:e")]
    assert as_cc_binary_with_embedded_py(cpp_mod, name, fake_package, py_deps, "repo") == CcBinaryWithEmbeddedPy(
        name=name,
        srcs=[Path(name + ".hh"), Path(name + ".inl"), Path(name + ".cc")],
        deps=[
            Label("//:a"),
            Label("//:b"),
            Label("//:c"),
        ],
        data=[],
        py_deps=[
            Label("//:d"),
            Label("//:e"),
        ],
    )
