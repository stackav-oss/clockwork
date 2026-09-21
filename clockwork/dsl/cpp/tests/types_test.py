# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

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
        types.CppTemplateParam(types.CppTypeArg("SomeArg"), None),
        types.CppTemplateParam(types.CppNamedType(types.CppType([], "uint64_t", None), "some_value"), None),
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


def test_cpp_struct_template_with_default_args() -> None:
    header_a = Header("repo", "a.hh")
    cpp_type = types.CppStruct(
        name=types.CppType([header_a], "SomeType", "a"),
        doc="Some doc.",
    )
    cpp_type.template_param = [
        types.CppTemplateParam(
            types.CppNamedType(types.CppType([], "int32_t", None), "some_value"), types.CppValue(None, "0")
        ),
        types.CppTemplateParam(
            types.CppNamedType(types.CppType([], "uint64_t", None), "another_value"), types.CppValue(None, "1")
        ),
    ]
    rendered = cpp_type.render("a")
    expected_header = """
/// Some doc.
template <int32_t some_value = 0, uint64_t another_value = 1>
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


def test_cpp_struct_nested_simple() -> None:
    """Test nested struct with various members, methods, and constructors."""
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")

    outer = types.CppStruct(
        name=types.CppType([header_a], "Outer", "ns"),
        doc="Outer struct.",
    )

    inner = types.CppStruct(
        name=types.CppType([], "Inner", "ns::Outer"),
        doc="Inner struct.",
    )

    # Add constructor
    ctor_body = CppChunk()
    ctor_body.append("// Initialize")
    inner.public.append(
        types.CppConstructor(
            doc="Constructor.",
            arguments=[types.CppNamedType(types.CppType([], "int", None), "val")],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            member_init_list=[("value_", "val")],
            body=ctor_body,
        )
    )

    # Add regular method
    method_body = CppChunk()
    method_body.append("return value_;")
    inner.public.append(
        types.CppMethod(
            name="get_value",
            doc="Get value method.",
            return_type=types.CppType([], "int", None),
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=["const"],
            body=method_body,
            no_discard=False,
        )
    )

    # Add inline method
    inline_method_body = CppChunk()
    inline_method_body.append("value_ = val;")
    inner.public.append(
        types.CppMethod(
            name="set_value",
            doc="Set value method.",
            return_type=types.CppType([], "void", None),
            arguments=[types.CppNamedType(types.CppType([], "int", None), "val")],
            leading_qualifiers=["inline"],
            trailing_qualifiers=[],
            body=inline_method_body,
            no_discard=False,
        )
    )

    # Add data member
    inner.public.append(
        types.CppNamedValue(
            types.CppNamedType(types.CppType([header_b], "int32_t", None), "extra"),
            types.CppValue(None, "0"),
            doc="Extra value.",
        )
    )

    # Add private member
    inner.private.append(
        types.CppNamedValue(
            types.CppNamedType(types.CppType([], "int", None), "value_"),
            types.CppValue(None, "0"),
            doc="Value member.",
        )
    )

    outer.public.append(inner)

    rendered = outer.render("ns")
    expected_header = """
/// Outer struct.
struct Outer
{
public:
    /// Inner struct.
    struct Inner
    {
    public:
        /// Constructor.
        explicit Inner(int val);
        /// Get value method.
        int get_value() const;
        /// Set value method.
        inline void set_value(int val);
        /// Extra value.
        int32_t extra{0};
    private:
        /// Value member.
        int value_{0};
    };
};
    """.strip()
    expected_inline = """
inline auto Outer::Inner::set_value(int val) -> void
{
    value_ = val;
}
    """.strip()
    expected_implementation = """
Outer::Inner::Inner(int val)
    : value_{val}
{
    // Initialize
}
auto Outer::Inner::get_value() const -> int
{
    return value_;
}
    """.strip()
    assert rendered.header_chunk.render_str(render_includes=False).strip() == expected_header
    assert rendered.inline_chunk.render_str(render_includes=False).strip() == expected_inline
    assert rendered.implementation_chunk.render_str(render_includes=False).strip() == expected_implementation


def test_cpp_struct_deeply_nested() -> None:
    """Test deeply nested structs (3 levels)."""
    header_a = Header("repo", "a.hh")

    outer = types.CppStruct(
        name=types.CppType([header_a], "Outer", "ns"),
        doc="Outer struct.",
    )

    middle = types.CppStruct(
        name=types.CppType([], "Middle", "ns::Outer"),
        doc="Middle struct.",
    )

    inner = types.CppStruct(
        name=types.CppType([], "Inner", "ns::Outer::Middle"),
        doc="Inner struct.",
    )

    method_body = CppChunk()
    method_body.append("return 123;")
    inner.public.append(
        types.CppMethod(
            name="deeply_nested_method",
            doc="Deeply nested method.",
            return_type=types.CppType([], "int", None),
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=method_body,
            no_discard=False,
        )
    )

    middle.public.append(inner)
    outer.public.append(middle)

    rendered = outer.render("ns")
    expected_header = """
/// Outer struct.
struct Outer
{
public:
    /// Middle struct.
    struct Middle
    {
    public:
        /// Inner struct.
        struct Inner
        {
        public:
            /// Deeply nested method.
            int deeply_nested_method();
        };
    };
};
    """.strip()
    expected_implementation = """
auto Outer::Middle::Inner::deeply_nested_method() -> int
{
    return 123;
}
    """.strip()
    assert rendered.header_chunk.render_str(render_includes=False).strip() == expected_header
    assert rendered.inline_chunk.render_str(render_includes=False).strip() == ""
    assert rendered.implementation_chunk.render_str(render_includes=False).strip() == expected_implementation


def test_cpp_struct_nested_multiple_siblings() -> None:
    """Test struct with multiple nested structs."""
    header_a = Header("repo", "a.hh")

    outer = types.CppStruct(
        name=types.CppType([header_a], "Outer", "ns"),
        doc="Outer struct.",
    )

    inner1 = types.CppStruct(
        name=types.CppType([], "Inner1", "ns::Outer"),
        doc="First inner struct.",
    )

    inner2 = types.CppStruct(
        name=types.CppType([], "Inner2", "ns::Outer"),
        doc="Second inner struct.",
    )

    outer.public.append(inner1)
    outer.public.append(inner2)

    rendered = outer.render("ns")
    expected_header = """
/// Outer struct.
struct Outer
{
public:
    /// First inner struct.
    struct Inner1
    {
    };
    /// Second inner struct.
    struct Inner2
    {
    };
};
    """.strip()
    assert rendered.header_chunk.render_str(render_includes=False).strip() == expected_header


def test_cpp_struct_nested_in_private_section() -> None:
    """Test nested struct in private section."""
    header_a = Header("repo", "a.hh")

    outer = types.CppStruct(
        name=types.CppType([header_a], "Outer", "ns"),
        doc="Outer struct.",
    )

    inner_impl = types.CppStruct(
        name=types.CppType([], "InnerImpl", "ns::Outer"),
        doc="Private implementation detail.",
    )
    inner_impl.public.append(
        types.CppNamedValue(
            types.CppNamedType(types.CppType([], "int", None), "internal_value"),
            types.CppValue(None, "0"),
            doc=None,
        )
    )

    outer.private.append(inner_impl)

    rendered = outer.render("ns")
    expected_header = """
/// Outer struct.
struct Outer
{
private:
    /// Private implementation detail.
    struct InnerImpl
    {
    public:
        int internal_value{0};
    };
};
    """.strip()
    assert rendered.header_chunk.render_str(render_includes=False).strip() == expected_header


def test_cpp_struct_with_public_base() -> None:
    """Test struct with a single public base class."""
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")

    base_type = types.CppType([header_b], "BaseClass", "ns")
    derived = types.CppStruct(
        name=types.CppType([header_a], "Derived", "ns"),
        doc="Derived struct.",
    )
    derived.base_classes.append((base_type, types.MemberAccess.public))

    derived.public.append(
        types.CppNamedValue(
            types.CppNamedType(types.CppType([], "int", None), "value"),
            types.CppValue(None, "42"),
            doc="Some value.",
        )
    )

    rendered = derived.render("ns")
    expected_header = """
/// Derived struct.
struct Derived : public BaseClass
{
public:
    /// Some value.
    int value{42};
};
    """.strip()
    assert list(derived.includes) == [header_a, header_b]
    assert rendered.header_chunk.render_str(render_includes=False).strip() == expected_header


def test_cpp_struct_with_private_base() -> None:
    """Test struct with a single private base class."""
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")

    base_type = types.CppType([header_b], "BaseClass", "ns")
    derived = types.CppStruct(
        name=types.CppType([header_a], "Derived", "ns"),
        doc="Derived struct.",
    )
    derived.base_classes.append((base_type, types.MemberAccess.private))

    derived.public.append(
        types.CppNamedValue(
            types.CppNamedType(types.CppType([], "int", None), "value"),
            types.CppValue(None, "42"),
            doc="Some value.",
        )
    )

    rendered = derived.render("ns")
    expected_header = """
/// Derived struct.
struct Derived : private BaseClass
{
public:
    /// Some value.
    int value{42};
};
    """.strip()
    assert list(derived.includes) == [header_a, header_b]
    assert rendered.header_chunk.render_str(render_includes=False).strip() == expected_header


def test_cpp_struct_with_multiple_bases() -> None:
    """Test struct with multiple base classes with mixed access."""
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")
    header_c = Header("repo", "c.hh")

    base1 = types.CppType([header_b], "PublicBase", "ns")
    base2 = types.CppType([header_c], "PrivateBase", "ns")
    derived = types.CppStruct(
        name=types.CppType([header_a], "Derived", "ns"),
        doc="Derived struct.",
    )
    derived.base_classes.append((base1, types.MemberAccess.public))
    derived.base_classes.append((base2, types.MemberAccess.private))

    derived.public.append(
        types.CppNamedValue(
            types.CppNamedType(types.CppType([], "int", None), "value"),
            types.CppValue(None, "42"),
            doc="Some value.",
        )
    )

    rendered = derived.render("ns")
    expected_header = """
/// Derived struct.
struct Derived : public PublicBase, private PrivateBase
{
public:
    /// Some value.
    int value{42};
};
    """.strip()
    assert list(derived.includes) == [header_a, header_b, header_c]
    assert rendered.header_chunk.render_str(render_includes=False).strip() == expected_header


def test_cpp_struct_base_with_different_namespace() -> None:
    """Test struct with base class from different namespace."""
    header_a = Header("repo", "a.hh")
    header_b = Header("repo", "b.hh")

    base_type = types.CppType([header_b], "BaseClass", "other_ns")
    derived = types.CppStruct(
        name=types.CppType([header_a], "Derived", "ns"),
        doc="Derived struct.",
    )
    derived.base_classes.append((base_type, types.MemberAccess.public))

    rendered = derived.render("ns")
    expected_header = """
/// Derived struct.
struct Derived : public ::other_ns::BaseClass
{
};
    """.strip()
    assert rendered.header_chunk.render_str(render_includes=False).strip() == expected_header
