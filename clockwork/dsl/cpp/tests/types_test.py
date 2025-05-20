# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for typereg."""

from clockwork.dsl.cpp import types
from clockwork.dsl.cpp.context import CppChunk, Header


def test_cpp_type_same_namespace() -> None:
    namespace = "a::b::c"
    cpp_type = types.CppType([], "SomeType", namespace, False, None)
    assert cpp_type.render(namespace) == "SomeType"


def test_cpp_type_different_namespace() -> None:
    cpp_type = types.CppType([], "SomeType", "a::b::c", False, None)
    assert cpp_type.render("d::e::f") == "::a::b::c::SomeType"


def test_cpp_type_no_namespace() -> None:
    cpp_type = types.CppType([], "SomeType", None)
    assert cpp_type.render("a::b::c") == "SomeType"


def test_cpp_value() -> None:
    header = Header("repo", "a/b/c.hh")
    cpp_value = types.CppValue(types.CppType([header], "SomeType", "a", False, None), "123")
    assert list(cpp_value.includes) == [header]
    assert cpp_value.render("b") == "::a::SomeType{123}"

    cpp_value = types.CppValue(types.CppType([header], "SomeType", "a", False, None), ["123", "abc"])
    assert list(cpp_value.includes) == [header]
    assert cpp_value.render("b") == "::a::SomeType{123, abc}"

    another_header = Header("repo", "x/y/z.hh")
    cpp_value = types.CppValue(
        types.CppType([header], "SomeType", "a", False, None),
        ["123", types.CppValue(types.CppType([another_header], "AnotherType", "x", False, None), "abc")],
    )
    assert list(cpp_value.includes) == [header, another_header]
    assert cpp_value.render("b") == "::a::SomeType{123, ::x::AnotherType{abc}}"

    cpp_value = types.CppValue(
        types.CppType([header], "SomeType", "a", False, None),
        {
            "first": "123",
            "second": types.CppValue(types.CppType([another_header], "AnotherType", "x", False, None), "abc"),
        },
    )
    assert list(cpp_value.includes) == [header, another_header]
    assert cpp_value.render("b") == "::a::SomeType{.first = 123, .second = ::x::AnotherType{abc}}"


def test_cpp_scoped_variable() -> None:
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")
    cpp_type = types.CppType([header_a], "SomeType", "a", False, None)

    # String scope
    assert types.CppScopedValue("hello", [], "world").render("") == "::hello::world"
    assert types.CppScopedValue("hello", [], "world").render("hello") == "world"
    assert list(types.CppScopedValue("hello", [], "world").includes) == []
    assert list(types.CppScopedValue("hello", [header_a], "world").includes) == [header_a]

    # Type scope
    assert types.CppScopedValue(cpp_type, [], "var").render("") == "::a::SomeType::var"
    assert list(types.CppScopedValue(cpp_type, [], "var").includes) == [header_a]
    assert types.CppScopedValue(cpp_type, [], "var").render("a") == "SomeType::var"
    assert list(types.CppScopedValue(cpp_type, [header_b], "var").includes) == [header_b, header_a]


def test_cpp_template() -> None:
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")
    header_c = Header("repo", "c.hh")
    arg = types.CppType([header_a], "SomeType", "a")
    val = types.CppValue(types.CppType([header_b], "SomeValue", "b"), "123")
    template = types.CppTemplate([header_c], "SomeTemplate", "c")
    template_type = template.instantiate([arg, val])
    assert template_type.render("") == "::c::SomeTemplate<::a::SomeType, ::b::SomeValue{123}>"
    assert list(template_type.includes) == [header_c, header_a, header_b]


def test_cpp_named_type() -> None:
    header = Header("repo", "a/b/c.hh")
    named_type = types.CppNamedType(types.CppType([header], "SomeType", "b"), "some_name")
    assert named_type.render("") == "::b::SomeType some_name"
    assert list(named_type.includes) == [header]


def test_cpp_named_value() -> None:
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")
    named_value = types.CppNamedValue(
        types.CppNamedType(types.CppType([header_a], "SomeType", "a"), "some_name"),
        types.CppValue(types.CppType([header_b], "AnotherType", "b"), "123"),
        doc="Some doc",
    )
    assert named_value.render("").render_str() == "/// Some doc\n::a::SomeType some_name{::b::AnotherType{123}};\n"
    named_value.qualifiers = ["inline", "constexpr"]
    assert (
        named_value.render("").render_str()
        == "/// Some doc\ninline constexpr ::a::SomeType some_name{::b::AnotherType{123}};\n"
    )
    assert list(named_value.includes) == [header_a, header_b]


def test_cpp_method() -> None:
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")
    impl = CppChunk()
    impl.append("return {};")
    method = types.CppMethod(
        name="some_method",
        doc="Some docs",
        return_type=types.CppType([header_a], "SomeType", "a"),
        arguments=[types.CppNamedType(types.CppType([header_b], "AnotherType", "b"), "some_name", None)],
        leading_qualifiers=[],
        trailing_qualifiers=[],
        body=impl,
        no_discard=False,
    )
    assert list(method.includes) == [header_a, header_b]

    parent_type = types.CppType([], "Parent", "c")
    cpp_mod = method.render(parent_type, "")
    expected_header = "    /// Some docs\n    ::a::SomeType some_method(::b::AnotherType some_name);\n"
    assert cpp_mod.header_chunk.render_str(render_includes=False) == expected_header
    expected_inline = """auto ::c::Parent::some_method(::b::AnotherType some_name) -> ::a::SomeType
{
    return {};
}
"""
    assert cpp_mod.implementation_chunk.render_str(render_includes=False) == expected_inline
    assert cpp_mod.inline_chunk.render_str(render_includes=False) == ""

    method.leading_qualifiers = ["inline"]
    method.trailing_qualifiers = ["const"]
    method.no_discard = True

    parent_template = types.CppTemplateType([header_a], "Parent", "c", [types.CppType([], "Arg", "d")])
    cpp_mod = method.render(parent_template, "")
    expected_header = (
        "    /// Some docs\n    [[nodiscard]] inline ::a::SomeType some_method(::b::AnotherType some_name) const;\n"
    )
    assert cpp_mod.header_chunk.render_str(render_includes=False) == expected_header
    expected_inline = """inline auto ::c::Parent<::d::Arg>::some_method(::b::AnotherType some_name) const -> ::a::SomeType
{
    return {};
}
"""
    assert cpp_mod.inline_chunk.render_str(render_includes=False) == expected_inline
    assert cpp_mod.implementation_chunk.render_str() == ""


def test_cpp_constructor() -> None:
    header_a = Header("repo", "a.hh")
    impl = CppChunk()
    impl.append("static_cast<void>(int{});")
    method = types.CppConstructor(
        doc="Some docs",
        arguments=[types.CppNamedType(types.CppType([header_a], "AnotherType", "b"), "some_name", None)],
        leading_qualifiers=["constexpr"],
        trailing_qualifiers=["noexcept"],
        member_init_list=[("some_name_", "some_name")],
        body=impl,
    )
    assert list(method.includes) == [header_a]

    parent_type = types.CppType([], "Parent", "c")
    cpp_mod = method.render(parent_type, "")
    expected_header = "    /// Some docs\n    constexpr explicit Parent(::b::AnotherType some_name) noexcept;\n"
    assert cpp_mod.header_chunk.render_str(render_includes=False) == expected_header
    expected_inline = """constexpr ::c::Parent::Parent(::b::AnotherType some_name) noexcept
    : some_name_{some_name}
{
    static_cast<void>(int{});
}
"""
    assert cpp_mod.implementation_chunk.render_str(render_includes=False) == expected_inline
    assert cpp_mod.inline_chunk.render_str(render_includes=False) == ""


def test_ref_qualify() -> None:
    cpp_type = types.CppType([], "Arg", "d")
    assert cpp_type.render("") == "::d::Arg"
    assert types.const_qualify(cpp_type, True).render("") == "const ::d::Arg"
    assert types.ref_qualify(cpp_type, types.Ref.L).render("") == "::d::Arg&"
    assert types.ref_qualify(cpp_type, types.Ref.R).render("") == "::d::Arg&&"
    assert types.const_qualify(types.ref_qualify(cpp_type, types.Ref.L), True).render("") == "const ::d::Arg&"
    assert types.const_qualify(types.ref_qualify(cpp_type, types.Ref.R), True).render("") == "const ::d::Arg&&"


def test_cpp_type_arg() -> None:
    assert types.CppTypeArg("SomeType").render("") == "class SomeType"


def test_cpp_struct_no_members() -> None:
    header_a = Header("repo", "a.hh")
    cpp_type = types.CppStruct(
        name=types.CppType([header_a], "SomeType", "a"),
        doc="Some doc",
    )
    rendered = cpp_type.render("a")
    expected_header = """
/// Some doc
struct SomeType
{
};
    """.strip()
    assert list(cpp_type.includes) == [header_a]
    assert rendered.header_chunk.render_str(render_includes=False).strip() == expected_header
    assert rendered.inline_chunk.render_str(render_includes=False) == ""
    assert rendered.implementation_chunk.render_str(render_includes=False) == ""


def test_cpp_struct_members() -> None:
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")
    header_c = Header("repo", "c.hh")
    cpp_type = types.CppStruct(
        name=types.CppType([header_a], "SomeType", "a"),
        doc="Some doc.",
    )
    cpp_type.members[types.MemberAccess.private] = [
        types.CppNamedValue(
            types.CppNamedType(types.CppType([header_b], "SomeVar", "b"), "some_var"),
            types.CppValue(None, "123"),
            doc="Some variable.",
        ),
    ]
    member_body = CppChunk()
    member_body.append("return {};")
    cpp_type.members[types.MemberAccess.public] = [
        types.CppMethod(
            name="some_method",
            doc="Some method.",
            return_type=types.CppType([header_c], "SubType", "c"),
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=member_body,
            no_discard=False,
        ),
    ]
    rendered = cpp_type.render("a")
    expected_header = """
/// Some doc.
struct SomeType
{
public:
    /// Some method.
    ::c::SubType some_method();
private:
    /// Some variable.
    ::b::SomeVar some_var{123};
};
    """.strip()
    expected_implementation = """
auto SomeType::some_method() -> ::c::SubType
{
    return {};
}
    """.strip()
    assert list(cpp_type.includes) == [header_a, header_b, header_c]
    assert rendered.header_chunk.render_str(render_includes=False).strip() == expected_header
    assert rendered.inline_chunk.render_str(render_includes=False).strip() == ""
    assert rendered.implementation_chunk.render_str(render_includes=False).strip() == expected_implementation

    assert cpp_type.public is cpp_type.members[types.MemberAccess.public]
    assert cpp_type.protected is cpp_type.members[types.MemberAccess.protected]
    assert cpp_type.private is cpp_type.members[types.MemberAccess.private]


def test_cpp_struct_template_no_args() -> None:
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")
    cpp_type = types.CppStruct(
        name=types.CppTemplate([header_a], "SomeType", "a").instantiate(
            [types.CppType([header_b], "AnotherType", "b")]
        ),
        doc="Some doc.",
    )
    rendered = cpp_type.render("a")
    expected_header = """
/// Some doc.
template <>
struct SomeType<::b::AnotherType>
{
};
    """.strip()
    assert list(cpp_type.includes) == [header_a, header_b]
    assert rendered.header_chunk.render_str(render_includes=False).strip() == expected_header
    assert rendered.inline_chunk.render_str(render_includes=False).strip() == ""
    assert rendered.implementation_chunk.render_str(render_includes=False).strip() == ""


def test_cpp_struct_template_with_args() -> None:
    header_a = Header("repo", "a.hh")
    cpp_type = types.CppStruct(
        name=types.CppType([header_a], "SomeType", "a"),
        doc="Some doc.",
    )
    cpp_type.template_param = [
        types.CppTypeArg("SomeArg"),
        types.CppNamedType(types.CppType([], "uint64_t", None), "some_value"),
    ]
    rendered = cpp_type.render("a")
    expected_header = """
/// Some doc.
template <class SomeArg, uint64_t some_value>
struct SomeType
{
};
    """.strip()
    assert rendered.header_chunk.render_str(render_includes=False).strip() == expected_header
    assert rendered.inline_chunk.render_str(render_includes=False).strip() == ""
    assert rendered.implementation_chunk.render_str(render_includes=False).strip() == ""


def test_cpp_struct_with_no_lint() -> None:
    header_a = Header("repo", "a.hh")
    cpp_type = types.CppStruct(
        name=types.CppType([header_a], "SomeType", "a"),
        doc="Some doc.",
        no_lints=["some.clang.tidy.check", "another.clang.tidy.check"],
    )
    rendered = cpp_type.render("a")
    expected_header = """
/// Some doc.
struct SomeType  // NOLINT(some.clang.tidy.check, another.clang.tidy.check)
{
};
    """.strip()
    assert rendered.header_chunk.render_str(render_includes=False).strip() == expected_header
    assert rendered.inline_chunk.render_str(render_includes=False).strip() == ""
    assert rendered.implementation_chunk.render_str(render_includes=False).strip() == ""


def test_cpp_type_alias_def() -> None:
    header_a = Header("repo", "a.hh")
    alias = types.CppTypeAliasDef(
        name="SomeAlias",
        alias_for=types.CppType([header_a], "SomeType", "a"),
        doc="Some doc.",
    )

    assert list(alias.includes) == [header_a]
    assert alias.render("").render_str(render_includes=False) == "/// Some doc.\nusing SomeAlias = ::a::SomeType;\n"


def test_cpp_fn() -> None:
    header = Header("repo", "a.hh")
    fn = types.CppFn([header], "a::b::c", "d")
    assert fn.render(types.GLOBAL_NAMESPACE) == "::a::b::c::d"
    assert list(fn.includes) == [header]


def test_cpp_fn_call() -> None:
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")
    invocation = types.CppFn([header_a], "a", "func").invoke(
        [types.CppValue(types.CppType([header_b], "Type", "b"), "value")]
    )
    assert invocation.render(types.GLOBAL_NAMESPACE) == "::a::func(::b::Type{value})"
    assert list(invocation.includes) == [header_a, header_b]
