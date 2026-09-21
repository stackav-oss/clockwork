# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Test the Node IR module."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from textwrap import dedent
from unittest.mock import MagicMock

import pytest
from clockwork.dsl import compiler_context
from clockwork.dsl.ir import clkbuiltins, compiler, importer, node, parse
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from typing_extensions import override


@dataclass
class A:
    """A."""

    deferrable: node.Deferrable[B]
    b_map: dict[str, node.Deferrable[B]]
    b_list: list[node.Deferrable[B]]
    str_list: list[str]
    recursive: A | None = None


@dataclass
class B(node.NamedEntity):
    """B."""

    child: node.Deferrable[node.NamedEntity]


def test_resolve_names() -> None:
    scope = node.Scope(parent=None, uniq_path="test", module_id_for_errors=None)
    a0 = A(
        deferrable=node.DeferredLookup.make(expected_type=B, identifier="b0"),
        b_map={"x": node.DeferredLookup.make(expected_type=B, identifier="b1")},
        b_list=[node.DeferredLookup.make(expected_type=B, identifier="b2")],
        str_list=["abc"],
    )
    a0.recursive = a0
    with pytest.raises(ValueError, match="Undefined identifier b0"):
        node.resolve_names(a0, scope)
    b0 = B(name="b0", scope=scope, child=node.DeferredLookup.make(expected_type=node.NamedEntity, identifier="entity0"))
    scope.define("b0", b0, terminals=None)
    b1 = B(name="b1", scope=scope, child=node.DeferredLookup.make(expected_type=node.NamedEntity, identifier="entity0"))
    scope.define("b1", b1, terminals=None)
    b2 = B(name="b2", scope=scope, child=node.DeferredLookup.make(expected_type=node.NamedEntity, identifier="entity0"))
    scope.define("b2", b2, terminals=None)

    entity0 = node.NamedEntity(name="entity0", scope=scope)
    scope.define("entity0", entity0, terminals=None)

    node.resolve_names(a0, scope)
    assert a0.deferrable is b0


@dataclass
class MockImporter(node.Importer):
    """Mock."""

    import_specs: dict[node.Module.UseResult, node.ImportSpec] = field(default_factory=dict)
    result_entities: dict[str, node.NamedEntity | node.Module] = field(default_factory=dict)

    @override
    def resolve_import(self, enclosing_module: node.Module, use_result: node.Module.UseResult) -> node.ImportSpec:
        """Mock."""
        return self.import_specs[use_result]

    @override
    def execute_import(
        self, spec: node.ImportSpec, enclosing_module: node.Module, use_result: node.Module.UseResult
    ) -> tuple[node.Module, node.NamedEntity | None]:
        """Mock."""
        module = node.Module(
            doc=None,
            module_id=ModuleID("", ""),
            inner_scope=MagicMock(),
            terminals=None,
            cst_node=None,
            unresolved_imports=[],
            context=compiler_context.CompilerContext(),
            generates=None,
            inner_attrs=None,
        )
        if spec.entity_name:
            entity = node.NamedEntity(name=spec.import_name, scope=MagicMock())
            self.result_entities[spec.import_name] = entity
        else:
            entity = None
            self.result_entities[spec.import_name] = module
        return module, entity

    @override
    def try_cached_load(self, module_id: ModuleID) -> node.Module | None:
        """Mock."""
        return None

    @override
    def cache_module(self, module_id: ModuleID, module: node.Module) -> None:
        """Mock."""
        return


def test_imports() -> None:
    test_source = """
use foo1;
use foo2::bar as bar_alias;
use foo3::{bar};
use foo4::{bar, baz, bang};
use @ext::foo5::extbar;
use @ext::foo6::{extbar, extbaz};
use my_lib1::module::{sub_module1, sub_module2};
use my_lib1::utils;
use my_lib2::module::{something as alias};
use my_lib3::module::sub::{Item1 as Alias1, Item2};
use my_lib3::utils::{Tool, Helper as H};
use my_lib4::module::{Item};
use my_lib4::module::sub_module::{Item};

// Schema
schema Schema
{
    fields
    {
        // foo
        #1 foo: Int64;
    }
}
"""

    parse_result = parse.clk_string_to_cst(test_source)
    module = node.Module.from_cst(
        module_id=ModuleID("", ""),
        builtins=clkbuiltins.BUILTINS_SCOPE,
        cst_node=parse_result.cst,
        terminals=parse_result.terminals,
    )
    assert module.generates is None
    assert module.inner_attrs is None
    mock_data = {
        node.Module.UseResult(
            repo=None, path=("foo1",), alias=None, use_targets=None, use_type=node.UseResultType.module
        ): node.ImportSpec(ModuleID.from_path("", Path("foo1.clk")), None, "foo1"),
        node.Module.UseResult(
            repo=None,
            path=("foo2", "bar"),
            alias="bar_alias",
            use_targets=None,
            use_type=node.UseResultType.module,
        ): node.ImportSpec(
            ModuleID.from_path("", Path("foo2/bar.clk")),
            None,
            "bar_alias",
        ),
        node.Module.UseResult(
            repo=None, path=("foo3", "bar"), alias=None, use_targets=None, use_type=node.UseResultType.entity
        ): node.ImportSpec(ModuleID.from_path("", Path("foo3/bar.clk")), None, "bar"),
        node.Module.UseResult(
            repo=None, path=("foo4", "bar"), alias=None, use_targets=None, use_type=node.UseResultType.entity
        ): node.ImportSpec(
            ModuleID.from_path("", Path("foo4/bar.clk")),
            None,
            "bar_prime",
        ),
        node.Module.UseResult(
            repo=None, path=("foo4", "baz"), alias=None, use_targets=None, use_type=node.UseResultType.entity
        ): node.ImportSpec(ModuleID.from_path("", Path("foo4/baz.clk")), None, "baz"),
        node.Module.UseResult(
            repo=None, path=("foo4", "bang"), alias=None, use_targets=None, use_type=node.UseResultType.entity
        ): node.ImportSpec(
            ModuleID.from_path("", Path("foo4/bang.clk")),
            None,
            "bang",
        ),
        node.Module.UseResult(
            repo="ext",
            path=("foo5", "extbar"),
            alias=None,
            use_targets=None,
            use_type=node.UseResultType.module,
        ): node.ImportSpec(
            ModuleID.from_path("ext", Path("foo5/extbar.clk")),
            None,
            "extbar",
        ),
        node.Module.UseResult(
            repo="ext",
            path=("foo6", "extbar"),
            alias=None,
            use_targets=None,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("ext", Path("foo6/extbar.clk")),
            None,
            "extbar_prime",
        ),
        node.Module.UseResult(
            repo="ext",
            path=("foo6", "extbaz"),
            alias=None,
            use_targets=None,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("ext", Path("foo6/extbaz.clk")),
            None,
            "extbaz",
        ),
        node.Module.UseResult(
            repo=None,
            path=("my_lib1", "module", "sub_module1"),
            alias=None,
            use_targets=None,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib1/module/sub_module1.clk")),
            None,
            "sub_module1",
        ),
        node.Module.UseResult(
            repo=None,
            path=("my_lib1", "module", "sub_module2"),
            alias=None,
            use_targets=None,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib1/module/sub_module2.clk")),
            None,
            "sub_module2",
        ),
        node.Module.UseResult(
            repo=None,
            path=("my_lib1", "utils"),
            alias=None,
            use_targets=None,
            use_type=node.UseResultType.module,
        ): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib1/utils.clk")),
            None,
            "utils",
        ),
        node.Module.UseResult(
            repo=None,
            path=("my_lib2", "module", "something"),
            alias="alias",
            use_targets=None,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib2/module/something.clk")),
            None,
            "alias",
        ),
        node.Module.UseResult(
            repo=None,
            path=("my_lib3", "module", "sub", "Item1"),
            alias="Alias1",
            use_targets=None,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib3/module/sub/Item1.clk")),
            "Item1",
            "Alias1",
        ),
        node.Module.UseResult(
            repo=None,
            path=("my_lib3", "module", "sub", "Item2"),
            alias=None,
            use_targets=None,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib3/module/sub/Item2.clk")),
            "Item2",
            "Item2",
        ),
        node.Module.UseResult(
            repo=None,
            path=("my_lib3", "utils", "Tool"),
            alias=None,
            use_targets=None,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib3/utils/Tool.clk")),
            "Tool",
            "Tool",
        ),
        node.Module.UseResult(
            repo=None,
            path=("my_lib3", "utils", "Helper"),
            alias="H",
            use_targets=None,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib3/utils/Helper.clk")),
            "Helper",
            "H",
        ),
        node.Module.UseResult(
            repo=None,
            path=("my_lib4", "module", "Item"),
            alias=None,
            use_targets=None,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib4/module/Item.clk")),
            "Item",
            "Item",
        ),
        node.Module.UseResult(
            repo=None,
            path=("my_lib4", "module", "sub_module", "Item"),
            alias=None,
            use_targets=None,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib4/module/sub_module/Item.clk")),
            "Item",
            "ItemPrime",
        ),
    }
    assert module.unresolved_imports == list(mock_data.keys())

    importer = MockImporter()
    importer.import_specs = mock_data

    module.resolve_imports(importer)

    for name, entity in importer.result_entities.items():
        assert module.inner_scope.parent is not None
        assert module.inner_scope.lookup(name, recursive=False) is None
        result = module.inner_scope.parent.lookup(name, recursive=False)
        assert result is not None
        assert module.inner_scope.lookup(name, recursive=True) is result
        if isinstance(result, node.NamespacedModule):
            assert result.module is module
            assert result.extern_module is entity
        else:
            assert result is entity


def test_clk_imports() -> None:
    test_source = """
#![generate(proto, cpp)]
use[] foo1;
use foo2::bar as bar_alias;
use foo3::{bar};
use[] foo4::{bar, baz, bang};
use @ext::foo5::extbar;
use @ext::foo6::{extbar, extbaz};
use[cpp] my_lib2::module::{something as alias};
unused_value = 123;
"""

    parse_result = parse.clk_string_to_cst(test_source)
    module = node.Module.from_cst(
        module_id=ModuleID("", ""),
        builtins=clkbuiltins.BUILTINS_SCOPE,
        cst_node=parse_result.cst,
        terminals=parse_result.terminals,
    )
    assert module.generates == {node.GenerateTarget.cpp, node.GenerateTarget.proto}
    assert module.inner_attrs == node.ClkAttributes(
        module=module,
        cpp_attr=node.ClkCppAttribute(),
        proto_attr=node.ClkProtoAttribute(),
        proto_conv_attr=node.ClkProtoConvAttribute(),
        exe_attr=node.ClkExeAttribute(),
        py_cog_attr=node.ClkPyCogAttribute(),
    )
    default_use_targets = frozenset({node.GenerateTarget.cpp, node.GenerateTarget.proto})
    empty_use_targets = frozenset()
    cpp_use_targets = frozenset({node.GenerateTarget.cpp})
    mock_data = {
        node.Module.UseResult(
            repo=None, path=("foo1",), alias=None, use_targets=empty_use_targets, use_type=node.UseResultType.module
        ): node.ImportSpec(ModuleID.from_path("", Path("foo1.clk")), None, "foo1"),
        node.Module.UseResult(
            repo=None,
            path=("foo2", "bar"),
            alias="bar_alias",
            use_targets=default_use_targets,
            use_type=node.UseResultType.module,
        ): node.ImportSpec(
            ModuleID.from_path("", Path("foo2/bar.clk")),
            None,
            "bar_alias",
        ),
        node.Module.UseResult(
            repo=None,
            path=("foo3", "bar"),
            alias=None,
            use_targets=default_use_targets,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(ModuleID.from_path("", Path("foo3/bar.clk")), None, "bar"),
        node.Module.UseResult(
            repo=None,
            path=("foo4", "bar"),
            alias=None,
            use_targets=empty_use_targets,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("", Path("foo4/bar.clk")),
            None,
            "bar_prime",
        ),
        node.Module.UseResult(
            repo=None,
            path=("foo4", "baz"),
            alias=None,
            use_targets=empty_use_targets,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(ModuleID.from_path("", Path("foo4/baz.clk")), None, "baz"),
        node.Module.UseResult(
            repo=None,
            path=("foo4", "bang"),
            alias=None,
            use_targets=empty_use_targets,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("", Path("foo4/bang.clk")),
            None,
            "bang",
        ),
        node.Module.UseResult(
            repo="ext",
            path=("foo5", "extbar"),
            alias=None,
            use_targets=default_use_targets,
            use_type=node.UseResultType.module,
        ): node.ImportSpec(
            ModuleID.from_path("ext", Path("foo5/extbar.clk")),
            None,
            "extbar",
        ),
        node.Module.UseResult(
            repo="ext",
            path=("foo6", "extbar"),
            alias=None,
            use_targets=default_use_targets,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("ext", Path("foo6/extbar.clk")),
            None,
            "extbar_prime",
        ),
        node.Module.UseResult(
            repo="ext",
            path=("foo6", "extbaz"),
            alias=None,
            use_targets=default_use_targets,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("ext", Path("foo6/extbaz.clk")),
            None,
            "extbaz",
        ),
        node.Module.UseResult(
            repo=None,
            path=("my_lib2", "module", "something"),
            alias="alias",
            use_targets=cpp_use_targets,
            use_type=node.UseResultType.entity,
        ): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib2/module/something.clk")),
            None,
            "alias",
        ),
    }
    assert module.unresolved_imports == list(mock_data.keys())

    importer = MockImporter()
    importer.import_specs = mock_data

    module.resolve_imports(importer)

    for name, entity in importer.result_entities.items():
        assert module.inner_scope.parent is not None
        assert module.inner_scope.lookup(name, recursive=False) is None
        result = module.inner_scope.parent.lookup(name, recursive=False)
        assert result is not None
        assert module.inner_scope.lookup(name, recursive=True) is result
        if isinstance(result, node.NamespacedModule):
            assert result.module is module
            assert result.extern_module is entity
        else:
            assert result is entity


def test_clk_attributes() -> None:  # noqa: PLR0915 (test code)
    test_source = """
#![generate(proto, proto_conv, cpp, cpp_exe, py_cog, py)]
#![cpp(namespace=a::b::c, type_namespace=a::b::c::d, type_header="foo/bar/baz.hh", type_factory=widgeter, generate_cog_metrics=true)]
#![proto(package=a.b.c, go_package=widgets.com/a/b/d, validate=false, prefix_enum_value_names=true)]
#![proto_conv(namespace=e::f::g, protobuf_to_tap=false)]
#![exe(offline=true)]
#![py_cog(wrapper_type=nanobind)]
unused_value = 123;
"""

    parse_result = parse.clk_string_to_cst(test_source)
    module = node.Module.from_cst(
        module_id=ModuleID("", ""),
        builtins=clkbuiltins.BUILTINS_SCOPE,
        cst_node=parse_result.cst,
        terminals=parse_result.terminals,
    )
    assert module.generates == {
        node.GenerateTarget.cpp,
        node.GenerateTarget.proto,
        node.GenerateTarget.proto_conv,
        node.GenerateTarget.py_cog,
        node.GenerateTarget.cpp_exe,
        node.GenerateTarget.py,
    }

    expected_attributes = node.ClkAttributes(
        module=module,
        cpp_attr=node.ClkCppAttribute(
            namespace="a::b::c",
            type_namespace="a::b::c::d",
            type_header="foo/bar/baz.hh",
            type_factory="widgeter",
            generate_cog_metrics=True,
        ),
        proto_attr=node.ClkProtoAttribute(
            package="a.b.c",
            go_package="widgets.com/a/b/d",
            validate=False,
            prefix_enum_value_names=True,
        ),
        proto_conv_attr=node.ClkProtoConvAttribute(
            protobuf_to_tap=False,
            tap_to_protobuf=None,
            namespace="e::f::g",
        ),
        exe_attr=node.ClkExeAttribute(
            offline=True,
        ),
        py_cog_attr=node.ClkPyCogAttribute(
            wrapper_type=node.PyCogWrapperType.nanobind,
        ),
    )
    assert module.inner_attrs == expected_attributes

    dflt_attrs = node.ClkAttributes(module=module)

    assert dflt_attrs.get_cpp_namespace() is None
    assert dflt_attrs.get_cpp_type_namespace() is None
    assert dflt_attrs.get_cpp_type_header() is None
    assert dflt_attrs.get_cpp_type_factory() is None
    assert dflt_attrs.get_cpp_generate_cog_metrics() is False
    assert dflt_attrs.get_proto_package() is None
    assert dflt_attrs.get_proto_go_package() is None
    assert dflt_attrs.get_proto_validate() is True
    assert dflt_attrs.get_proto_prefix_enum_value_names() is False
    assert dflt_attrs.get_proto_conv_protobuf_to_tap() is True
    assert dflt_attrs.get_proto_conv_tap_to_protobuf() is True
    assert dflt_attrs.get_proto_conv_namespace() is None
    assert dflt_attrs.get_exe_offline() is False
    assert dflt_attrs.get_py_cog_wrapper_type() == node.PyCogWrapperType.python

    dflt_attrs.merge_with_inner(None)
    assert dflt_attrs == node.ClkAttributes(
        module=module,
        cpp_attr=node.ClkCppAttribute(),
        proto_attr=node.ClkProtoAttribute(),
        proto_conv_attr=node.ClkProtoConvAttribute(),
        exe_attr=node.ClkExeAttribute(),
        py_cog_attr=node.ClkPyCogAttribute(),
    )

    assert dflt_attrs.get_cpp_namespace() is None
    assert dflt_attrs.get_cpp_type_namespace() is None
    assert dflt_attrs.get_cpp_type_header() is None
    assert dflt_attrs.get_cpp_type_factory() is None
    assert dflt_attrs.get_cpp_generate_cog_metrics() is False
    assert dflt_attrs.get_proto_package() is None
    assert dflt_attrs.get_proto_go_package() is None
    assert dflt_attrs.get_proto_validate() is True
    assert dflt_attrs.get_proto_prefix_enum_value_names() is False
    assert dflt_attrs.get_proto_conv_protobuf_to_tap() is True
    assert dflt_attrs.get_proto_conv_tap_to_protobuf() is True
    assert dflt_attrs.get_proto_conv_namespace() is None
    assert dflt_attrs.get_exe_offline() is False
    assert dflt_attrs.get_py_cog_wrapper_type() == node.PyCogWrapperType.python

    dflt_attrs = node.ClkAttributes(module=module)
    dflt_attrs.merge_with_inner(expected_attributes)
    assert dflt_attrs == expected_attributes

    assert dflt_attrs.get_cpp_namespace() == "a::b::c"
    assert dflt_attrs.get_cpp_type_namespace() == "a::b::c::d"
    assert dflt_attrs.get_cpp_type_header() == "foo/bar/baz.hh"
    assert dflt_attrs.get_cpp_type_factory() == "widgeter"
    assert dflt_attrs.get_cpp_generate_cog_metrics() is True
    assert dflt_attrs.get_proto_package() == "a.b.c"
    assert dflt_attrs.get_proto_go_package() == "widgets.com/a/b/d"
    assert dflt_attrs.get_proto_validate() is False
    assert dflt_attrs.get_proto_prefix_enum_value_names() is True
    assert dflt_attrs.get_proto_conv_protobuf_to_tap() is False
    assert dflt_attrs.get_proto_conv_tap_to_protobuf() is True
    assert dflt_attrs.get_proto_conv_namespace() == "e::f::g"
    assert dflt_attrs.get_exe_offline() is True
    assert dflt_attrs.get_py_cog_wrapper_type() == node.PyCogWrapperType.nanobind

    assert dflt_attrs.proto_conv_attr is not None
    dflt_attrs.proto_conv_attr.namespace = None

    assert dflt_attrs.get_proto_conv_namespace() == "a::b::c"


def test_import_duplicate_name() -> None:
    test_source = """
use foo1::bar;
use foo2::bar;

// Schema
schema Schema
{
    fields
    {
        // foo
        #1 foo: Int64;
    }
}
"""

    parse_result = parse.clk_string_to_cst(test_source)
    module = node.Module.from_cst(
        module_id=ModuleID("repo", ""),
        builtins=clkbuiltins.BUILTINS_SCOPE,
        cst_node=parse_result.cst,
        terminals=parse_result.terminals,
    )
    mock_data = {
        node.Module.UseResult(
            repo=None, path=("foo1", "bar"), alias=None, use_targets=None, use_type=node.UseResultType.module
        ): node.ImportSpec(ModuleID.from_path("", Path("foo1/bar.clk")), None, "bar"),
        node.Module.UseResult(
            repo=None, path=("foo2", "bar"), alias=None, use_targets=None, use_type=node.UseResultType.module
        ): node.ImportSpec(
            ModuleID.from_path("", Path("foo2/bar.clk")),
            None,
            "bar",
        ),
    }
    assert module.unresolved_imports == list(mock_data.keys())

    importer = MockImporter()
    importer.import_specs = mock_data

    with pytest.raises(
        ValueError,
        match=re.escape(
            """
Redefinition of name "bar"

In (unknown location):3:5:
use foo2::bar;
    ^
Original definition here:

In (unknown location):2:5:
use foo1::bar;
    ^""".strip(),
        ),
    ):
        module.resolve_imports(importer)


def test_attribute_errors() -> None:  # noqa: PLR0915 (Lots of statements for test cases)
    # cpp namespace in outer attribute
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(cpp)]
        // TEST
        #[cpp(namespace=test)]
        extern_type Foo;
        """,
    )
    with pytest.raises(
        ValueError,
        match="cpp namespace is only allowed in inner attributes",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # cpp type_namespace repeats inner attribute
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(cpp)]
        #![cpp(type_namespace=test)]
        // TEST
        #[cpp(type_namespace=test)]
        extern_type Foo;
        """,
    )
    with pytest.raises(
        ValueError,
        match="Value for type_namespace repeats inner attribute value",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # cpp type_header repeats inner attribute
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(cpp)]
        #![cpp(type_header="test")]
        // TEST
        #[cpp(type_header="test")]
        extern_type Foo;
        """,
    )
    with pytest.raises(
        ValueError,
        match="Value for type_header repeats inner attribute value",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # cpp type_factory repeats inner attribute
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(cpp)]
        #![cpp(type_factory=test)]
        // TEST
        #[cpp(type_factory=test)]
        extern_type Foo;
        """,
    )
    with pytest.raises(
        ValueError,
        match="Value for type_factory repeats inner attribute value",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # cpp generate_cog_metrics repeats default
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(cpp)]
        #![cpp(generate_cog_metrics=false)]
        unused = 5;
        """,
    )
    with pytest.raises(
        ValueError,
        match="Value for generate_cog_metrics repeats default attribute value",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # outer proto package attribute
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(proto)]
        // TEST
        #[proto(package=test.proto)]
        extern_type Foo;
        """,
    )
    with pytest.raises(
        ValueError,
        match="proto package is only allowed in inner attributes",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # outer proto go_package attribute
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(proto)]
        // TEST
        #[proto(go_package=test/proto)]
        extern_type Foo;
        """,
    )
    with pytest.raises(
        ValueError,
        match="proto go_package is only allowed in inner attributes",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # outer proto validate attribute
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(proto)]
        // TEST
        #[proto(validate=false)]
        extern_type Foo;
        """,
    )
    with pytest.raises(
        ValueError,
        match="proto validate is only allowed in inner attributes",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # proto validate repeats default
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(proto)]
        #![proto(validate=true)]
        unused = 5;
        """,
    )
    with pytest.raises(
        ValueError,
        match="Value for validate repeats default attribute value",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # proto prefix_enum_value_names repeats default
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(proto)]
        #![proto(prefix_enum_value_names=false)]
        unused = 5;
        """,
    )
    with pytest.raises(
        ValueError,
        match="Value for prefix_enum_value_names repeats default attribute value",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # proto_conv namespace repeats inner attribute
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(cpp, proto, proto_conv)]
        #![cpp(namespace=test)]
        #![proto(package=test)]
        #![proto_conv(namespace=test2)]
        // TEST
        #[proto_conv(namespace=test2)]
        extern_type Foo;
        """,
    )
    with pytest.raises(
        ValueError,
        match="Value for namespace repeats inner attribute value",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # proto_conv tap_to_protobuf repeats inner attribute
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(cpp, proto, proto_conv)]
        #![cpp(namespace=test)]
        #![proto(package=test)]
        #![proto_conv(tap_to_protobuf=false)]
        // TEST
        #[proto_conv(tap_to_protobuf=false)]
        extern_type Foo;
        """,
    )
    with pytest.raises(
        ValueError,
        match="Value for tap_to_protobuf repeats inner attribute value",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # proto_conv protobuf_to_tap repeats inner attribute
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(cpp, proto, proto_conv)]
        #![cpp(namespace=test)]
        #![proto(package=test)]
        #![proto_conv(protobuf_to_tap=false)]
        // TEST
        #[proto_conv(protobuf_to_tap=false)]
        extern_type Foo;
        """,
    )
    with pytest.raises(
        ValueError,
        match="Value for protobuf_to_tap repeats inner attribute value",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # proto validate repeats default
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(proto)]
        #![proto(validate=true)]
        unused = 5;
        """,
    )
    with pytest.raises(
        ValueError,
        match="Value for validate repeats default attribute value",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # proto_conv tap_to_protobuf repeats default
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(cpp, proto, proto_conv)]
        #![proto_conv(tap_to_protobuf=true)]
        unused = 5;
        """,
    )
    with pytest.raises(
        ValueError,
        match="Value for tap_to_protobuf repeats default attribute value",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # proto_conv protobuf_to_tap repeats default
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(cpp, proto, proto_conv)]
        #![proto_conv(protobuf_to_tap=true)]
        unused = 5;
        """,
    )
    with pytest.raises(
        ValueError,
        match="Value for protobuf_to_tap repeats default attribute value",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # proto_conv namespace repeats default
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(cpp, proto, proto_conv)]
        #![cpp(namespace=test)]
        #![proto_conv(namespace=test)]
        unused = 5;
        """,
    )
    with pytest.raises(
        ValueError,
        match="Value for namespace repeats default attribute value",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # outer exe attribute
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(cpp_exe)]
        #[exe(offline=true)]
        unused = 5;
        """,
    )
    with pytest.raises(
        TypeError,
        match="exe attribute is only allowed in inner attributes",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # exe offline repeats default
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(cpp_exe)]
        #![exe(offline=false)]
        unused = 5;
        """,
    )
    with pytest.raises(
        ValueError,
        match="Value for offline repeats default attribute value",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # outer py_cog attribute
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(cpp, py_cog)]
        #[py_cog(wrapper_type=python)]
        unused = 5;
        """,
    )
    with pytest.raises(
        TypeError,
        match="py_cog attribute is only allowed in inner attributes",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # py_cog wrapper_type repeats default
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        #![generate(cpp, py_cog)]
        #![py_cog(wrapper_type=python)]
        unused = 5;
        """,
    )
    with pytest.raises(
        ValueError,
        match="Value for wrapper_type repeats default attribute value",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)
