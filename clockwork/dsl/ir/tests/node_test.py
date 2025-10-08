# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Test the Node IR module."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from unittest.mock import MagicMock

import pytest
from clockwork.dsl import compiler_context
from clockwork.dsl.ir import clkbuiltins, node, parse
from clockwork.dsl.ir.module_id import ModuleID
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
    def resolve_import(self, _: node.Module, use_result: node.Module.UseResult) -> node.ImportSpec:  # pyright: ignore[reportIncompatibleMethodOverride] # TODO(DX-2384): Fix incompatible override errors # fmt: skip
        """Mock."""
        return self.import_specs[use_result]

    @override
    def execute_import(  # pyright: ignore[reportIncompatibleMethodOverride] # TODO(DX-2384): Fix incompatible override errors
        self, spec: node.ImportSpec, _module: node.Module, _use_result: node.Module.UseResult
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
use my_lib1::{module::{sub_module1, sub_module2}, utils};
use my_lib2::module::{something as alias};
use my_lib3::{module::sub::{Item1 as Alias1, Item2}, utils::{Tool, Helper as H}};
use my_lib4::{module::{Item, sub_module::{Item}}};

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
    mock_data = {
        node.Module.UseResult(repo=None, path=("foo1",), alias=None): node.ImportSpec(
            ModuleID.from_path("", Path("foo1.clk")), None, "foo1"
        ),
        node.Module.UseResult(repo=None, path=("foo2", "bar"), alias="bar_alias"): node.ImportSpec(
            ModuleID.from_path("", Path("foo2/bar.clk")),
            None,
            "bar_alias",
        ),
        node.Module.UseResult(repo=None, path=("foo3", "bar"), alias=None): node.ImportSpec(
            ModuleID.from_path("", Path("foo3/bar.clk")), None, "bar"
        ),
        node.Module.UseResult(repo=None, path=("foo4", "bar"), alias=None): node.ImportSpec(
            ModuleID.from_path("", Path("foo4/bar.clk")),
            None,
            "bar_prime",
        ),
        node.Module.UseResult(repo=None, path=("foo4", "baz"), alias=None): node.ImportSpec(
            ModuleID.from_path("", Path("foo4/baz.clk")), None, "baz"
        ),
        node.Module.UseResult(repo=None, path=("foo4", "bang"), alias=None): node.ImportSpec(
            ModuleID.from_path("", Path("foo4/bang.clk")),
            None,
            "bang",
        ),
        node.Module.UseResult(repo="ext", path=("foo5", "extbar"), alias=None): node.ImportSpec(
            ModuleID.from_path("ext", Path("foo5/extbar.clk")),
            None,
            "extbar",
        ),
        node.Module.UseResult(repo="ext", path=("foo6", "extbar"), alias=None): node.ImportSpec(
            ModuleID.from_path("ext", Path("foo6/extbar.clk")),
            None,
            "extbar_prime",
        ),
        node.Module.UseResult(repo="ext", path=("foo6", "extbaz"), alias=None): node.ImportSpec(
            ModuleID.from_path("ext", Path("foo6/extbaz.clk")),
            None,
            "extbaz",
        ),
        node.Module.UseResult(repo=None, path=("my_lib1", "module", "sub_module1"), alias=None): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib1/module/sub_module1.clk")),
            None,
            "sub_module1",
        ),
        node.Module.UseResult(repo=None, path=("my_lib1", "module", "sub_module2"), alias=None): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib1/module/sub_module2.clk")),
            None,
            "sub_module2",
        ),
        node.Module.UseResult(repo=None, path=("my_lib1", "utils"), alias=None): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib1/utils.clk")),
            None,
            "utils",
        ),
        node.Module.UseResult(repo=None, path=("my_lib2", "module", "something"), alias="alias"): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib2/module/something.clk")),
            None,
            "alias",
        ),
        node.Module.UseResult(repo=None, path=("my_lib3", "module", "sub", "Item1"), alias="Alias1"): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib3/module/sub/Item1.clk")),
            "Item1",
            "Alias1",
        ),
        node.Module.UseResult(repo=None, path=("my_lib3", "module", "sub", "Item2"), alias=None): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib3/module/sub/Item2.clk")),
            "Item2",
            "Item2",
        ),
        node.Module.UseResult(repo=None, path=("my_lib3", "utils", "Tool"), alias=None): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib3/utils/Tool.clk")),
            "Tool",
            "Tool",
        ),
        node.Module.UseResult(repo=None, path=("my_lib3", "utils", "Helper"), alias="H"): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib3/utils/Helper.clk")),
            "Helper",
            "H",
        ),
        node.Module.UseResult(repo=None, path=("my_lib4", "module", "Item"), alias=None): node.ImportSpec(
            ModuleID.from_path("", Path("my_lib4/module/Item.clk")),
            "Item",
            "Item",
        ),
        node.Module.UseResult(repo=None, path=("my_lib4", "module", "sub_module", "Item"), alias=None): node.ImportSpec(
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
        node.Module.UseResult(repo=None, path=("foo1", "bar"), alias=None): node.ImportSpec(
            ModuleID.from_path("", Path("foo1/bar.clk")), None, "bar"
        ),
        node.Module.UseResult(repo=None, path=("foo2", "bar"), alias=None): node.ImportSpec(
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
