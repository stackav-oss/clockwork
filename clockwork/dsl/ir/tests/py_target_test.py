# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for cpp_target."""

from __future__ import annotations

from pathlib import Path
from textwrap import dedent
from unittest.mock import MagicMock, patch

import pytest
from clockwork.dsl.bazel.py_targets import PyLibrary
from clockwork.dsl.bazel.targets import Label
from clockwork.dsl.ir import compiler, py_target
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_non_generic(fs_importer: FilesystemImporter) -> None:
    source = dedent(
        """
        // Doc
        schema Schema
        {
          fields
          {
            // Doc
            #1 value: UInt64;
          }
        }

        py_target foo
        {
          representation Tachyon<Schema>;
          interface Tap<Tachyon<Schema>>;
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo::bar"), fs_importer)

    target = module.inner_scope.lookup("foo")
    assert isinstance(target, py_target.PyTarget)
    expected_str = dedent(
        """
        import pathlib

        from clockwork.dsl.ir.module_id import ModuleID
        import clockwork.dsl.ir.compiler
        import clockwork.dsl.ir.importer
        import clockwork.serialization.py.tachyon_dyn

        _module = clockwork.dsl.ir.compiler.compile_source_file(ModuleID.from_path("clockwork", pathlib.Path("foo/bar.clk")), clockwork.dsl.ir.importer.FilesystemImporter(compile_fn=clockwork.dsl.ir.compiler.compile_source_file))
        Schema, _Schema = clockwork.serialization.py.tachyon_dyn.get_schema_dataclass(_module.context, _module, "Schema")
        """,
    )
    assert target.render_to_str().strip() == expected_str.strip()


def test_generic_missing_repr(fs_importer: FilesystemImporter) -> None:
    source = dedent(
        """
        // Doc
        schema NoReprSchema
        {
          parameters
          {
            // Doc
            #1 param: UInt64;
          }
          fields
          {
            // Doc
            #2 value: UInt64;
          }
        }

        py_target foo
        {
          interface NoReprSchema0: Tap<Tachyon<NoReprSchema<0>>>;
        }
        """,
    )
    with pytest.raises(
        ValueError,
        match="Unable to locate representation for interface.",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_generic_unsupported_param(fs_importer: FilesystemImporter) -> None:
    source = dedent(
        """
        // Doc
        schema TypeParamSchema
        {
          parameters
          {
            // Doc
            #1 Param: Type;
          }
          fields
          {
            // Doc
            #2 value: Param;
          }
        }

        py_target foo
        {
          representation Tachyon<TypeParamSchema<FixedArray<UInt64, 2>>>;
          interface TypeParamSchemaFixedArray: Tap<Tachyon<TypeParamSchema<FixedArray<UInt64, 2>>>>;
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)

    target = module.inner_scope.lookup("foo")
    assert isinstance(target, py_target.PyTarget)
    with pytest.raises(
        NotImplementedError,
        match="Unsupported parameter type:",
    ):
        target.render_to_str()


def test_generic_named_entity_type_param(fs_importer: FilesystemImporter) -> None:
    source = dedent(
        """
        // Doc
        schema TypeParamSchema
        {
          parameters
          {
            // Doc
            #1 Param: Type;
          }
          fields
          {
            // Doc
            #2 value: Param;
          }
        }

        py_target foo
        {
          representation Tachyon<TypeParamSchema<Float32>>;
          interface TypeParamSchemaFloat32: Tap<Tachyon<TypeParamSchema<Float32>>>;
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)

    target = module.inner_scope.lookup("foo")
    assert isinstance(target, py_target.PyTarget)
    expected_str = dedent(
        """
        import pathlib

        from clockwork.dsl.ir.module_id import ModuleID
        import clockwork.dsl.ir.compiler
        import clockwork.dsl.ir.importer
        import clockwork.serialization.py.tachyon_dyn

        _module = clockwork.dsl.ir.compiler.compile_source_file(ModuleID.from_path("clockwork", pathlib.Path("foo.clk")), clockwork.dsl.ir.importer.FilesystemImporter(compile_fn=clockwork.dsl.ir.compiler.compile_source_file))
        TypeParamSchemaFloat32, _TypeParamSchemaFloat32 = clockwork.serialization.py.tachyon_dyn.get_instantiation_dataclass(_module.context, _module, "TypeParamSchema", Param = clockwork.serialization.py.tachyon_dyn.ScopeLookup("Float32", module=None))
        """,
    )
    assert target.render_to_str().strip() == expected_str.strip()


def test_generic_named_entity_type_param_au(fs_importer: FilesystemImporter) -> None:
    source = dedent(
        """
        use jewels::units::clk::au as au;
        // Doc
        schema TypeParamSchema
        {
          parameters
          {
            // Doc
            #1 Param: Type;
          }
          fields
          {
            // Doc
            #2 value: Param;
          }
        }

        py_target foo
        {
          representation Tachyon<TypeParamSchema<au::MetersF>>;
          interface TypeParamSchemaMetersF: Tap<Tachyon<TypeParamSchema<au::MetersF>>>;
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)

    target = module.inner_scope.lookup("foo")
    assert isinstance(target, py_target.PyTarget)
    expected_str = dedent(
        """
        import pathlib

        from clockwork.dsl.ir.module_id import ModuleID
        import clockwork.dsl.ir.compiler
        import clockwork.dsl.ir.importer
        import clockwork.serialization.py.tachyon_dyn

        _module = clockwork.dsl.ir.compiler.compile_source_file(ModuleID.from_path("clockwork", pathlib.Path("foo.clk")), clockwork.dsl.ir.importer.FilesystemImporter(compile_fn=clockwork.dsl.ir.compiler.compile_source_file))
        _module_jewels_units_clk_au = clockwork.dsl.ir.compiler.compile_source_file(ModuleID.from_path("clockwork", pathlib.Path("jewels/units/clk/au.clk")), clockwork.dsl.ir.importer.FilesystemImporter(compile_fn=clockwork.dsl.ir.compiler.compile_source_file))
        TypeParamSchemaMetersF, _TypeParamSchemaMetersF = clockwork.serialization.py.tachyon_dyn.get_instantiation_dataclass(_module.context, _module, "TypeParamSchema", Param = clockwork.serialization.py.tachyon_dyn.ScopeLookup("MetersF", module=_module_jewels_units_clk_au))
        """,
    )
    assert target.render_to_str().strip() == expected_str.strip()


def test_generic_named_entity_type_param_schema(fs_importer: FilesystemImporter) -> None:
    source = dedent(
        """
        // Doc
        schema FooMessage
        {
            fields
            {
                // Doc
                #1 an_int: Int32;
            }
        }
        // Doc
        schema TypeParamSchema
        {
          parameters
          {
            // Doc
            #1 Param: Type;
          }
          fields
          {
            // Doc
            #2 value: Param;
          }
        }

        py_target foo
        {
          representation Tachyon<TypeParamSchema<FooMessage>>;
          interface TypeParamSchemaFooMessage: Tap<Tachyon<TypeParamSchema<FooMessage>>>;
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)

    target = module.inner_scope.lookup("foo")
    assert isinstance(target, py_target.PyTarget)
    expected_str = dedent(
        """
        import pathlib

        from clockwork.dsl.ir.module_id import ModuleID
        import clockwork.dsl.ir.compiler
        import clockwork.dsl.ir.importer
        import clockwork.serialization.py.tachyon_dyn

        _module = clockwork.dsl.ir.compiler.compile_source_file(ModuleID.from_path("clockwork", pathlib.Path("foo.clk")), clockwork.dsl.ir.importer.FilesystemImporter(compile_fn=clockwork.dsl.ir.compiler.compile_source_file))
        TypeParamSchemaFooMessage, _TypeParamSchemaFooMessage = clockwork.serialization.py.tachyon_dyn.get_instantiation_dataclass(_module.context, _module, "TypeParamSchema", Param = clockwork.serialization.py.tachyon_dyn.ScopeLookup("FooMessage", module=None))
        """,
    )
    assert target.render_to_str().strip() == expected_str.strip()


def test_generic_no_alias(fs_importer: FilesystemImporter) -> None:
    source = dedent(
        """
        // Doc
        schema IntegralParamSchema
        {
          parameters
          {
            // Doc
            #1 param: UInt64;
          }
          fields
          {
            // Doc
            #2 value: UInt64;
          }
        }

        py_target foo
        {
          representation Tachyon<IntegralParamSchema<3>>;
          interface Tap<Tachyon<IntegralParamSchema<3>>>;
        }
        """,
    )
    with pytest.raises(
        ValueError,
        match="Python interfaces for generic schemas must have an alias.",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_generic_integral_param(fs_importer: FilesystemImporter) -> None:
    source = dedent(
        """
        // Doc
        schema IntegralParamSchema
        {
          parameters
          {
            // Doc
            #1 param: UInt64;
            // Doc
            #2 another_param: UInt64;
          }
          fields
          {
            // Doc
            #3 value: UInt64;
          }
        }

        py_target foo
        {
          representation Tachyon<IntegralParamSchema<3, 7>>;
          interface IntegralParamSchema3: Tap<Tachyon<IntegralParamSchema<3, 7>>>;
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)

    target = module.inner_scope.lookup("foo")
    assert isinstance(target, py_target.PyTarget)
    expected_str = dedent(
        """
        import pathlib

        from clockwork.dsl.ir.module_id import ModuleID
        import clockwork.dsl.ir.compiler
        import clockwork.dsl.ir.importer
        import clockwork.serialization.py.tachyon_dyn

        _module = clockwork.dsl.ir.compiler.compile_source_file(ModuleID.from_path("clockwork", pathlib.Path("foo.clk")), clockwork.dsl.ir.importer.FilesystemImporter(compile_fn=clockwork.dsl.ir.compiler.compile_source_file))
        IntegralParamSchema3, _IntegralParamSchema3 = clockwork.serialization.py.tachyon_dyn.get_instantiation_dataclass(_module.context, _module, "IntegralParamSchema", param = 3, another_param = 7)
        """,
    )
    assert target.render_to_str().strip() == expected_str.strip()


def test_enum(fs_importer: FilesystemImporter) -> None:
    source = dedent(
        """
        // Doc
        enum SomeEnum
        {
          values
          {
            // Doc
            #1 first default;
          }
        }

        py_target foo
        {
          enum SomeEnum;
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo::bar"), fs_importer)

    target = module.inner_scope.lookup("foo")
    assert isinstance(target, py_target.PyTarget)
    expected_str = dedent(
        """
        import pathlib

        from clockwork.dsl.ir.module_id import ModuleID
        import clockwork.dsl.ir.compiler
        import clockwork.dsl.ir.importer
        import clockwork.serialization.py.tachyon_dyn

        _module = clockwork.dsl.ir.compiler.compile_source_file(ModuleID.from_path("clockwork", pathlib.Path("foo/bar.clk")), clockwork.dsl.ir.importer.FilesystemImporter(compile_fn=clockwork.dsl.ir.compiler.compile_source_file))
        SomeEnum, _SomeEnum = clockwork.serialization.py.tachyon_dyn.get_enum(_module.context, _module, "SomeEnum")
        """,
    )
    assert target.render_to_str().strip() == expected_str.strip()


def test_enum_flags(fs_importer: FilesystemImporter) -> None:
    source = dedent(
        """
        // Doc
        enum EnumFlags
        {
          options
          {
            bit_flags;
          }
          values
          {
            // No bits set
            #0 none default { underlying_value: 0; }
            // LSB
            #1 flag1 { underlying_value: 1; }
            // Bit1
            #2 flag2 { underlying_value: 2; }
            // Bit2
            #3 flag3 { underlying_value: 4; }
            // Bit0 + Bit1
            #4 flag12 { underlying_value: 3; }
          }
        }

        py_target foo
        {
          enum EnumFlags;
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo::bar"), fs_importer)

    target = module.inner_scope.lookup("foo")
    assert isinstance(target, py_target.PyTarget)
    expected_str = dedent(
        """
        import pathlib

        from clockwork.dsl.ir.module_id import ModuleID
        import clockwork.dsl.ir.compiler
        import clockwork.dsl.ir.importer
        import clockwork.serialization.py.tachyon_dyn

        _module = clockwork.dsl.ir.compiler.compile_source_file(ModuleID.from_path("clockwork", pathlib.Path("foo/bar.clk")), clockwork.dsl.ir.importer.FilesystemImporter(compile_fn=clockwork.dsl.ir.compiler.compile_source_file))
        EnumFlags, _EnumFlags = clockwork.serialization.py.tachyon_dyn.get_enum(_module.context, _module, "EnumFlags")
        """,
    )
    assert target.render_to_str().strip() == expected_str.strip()
    EnumFlags, _EnumFlags = tachyon_dyn.get_enum(module.context, module, "EnumFlags")  # noqa: N806 it's a type and should be camel case
    assert EnumFlags.flag12 == EnumFlags.flag1 | EnumFlags.flag2


@patch("clockwork.dsl.bazel.clk_targets.module_to_clk")
def test_output_targets(mock_module_to_clk: MagicMock, fs_importer: FilesystemImporter) -> None:
    mock_module_to_clk.side_effect = lambda _, module_id: Label(  # pyright: ignore[reportUnknownLambdaType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
        value=f"//{module_id.get_base_path().parent}:{module_id.get_base_path().stem}_clk"
    )

    source = """
        // Doc
        schema Base
        {
            fields
            {
                // Doc
                #0 value: Int64;
            }
        }

        // Doc
        schema Derived
        {
            fields
            {
                // Doc
                #0 value: Base;
            }
        }

        py_target base_target
        {
          representation Tachyon<Base>;
        }

        py_target derived_target
        {
          representation Tachyon<Derived>;
        }
        """

    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "a::b::c::foo"), fs_importer)

    base_target = module.inner_scope.lookup("base_target", recursive=False)
    assert isinstance(base_target, py_target.PyTarget)
    base_outputs = base_target.output_targets()
    assert base_outputs == [
        PyLibrary(
            name="base_target",
            srcs=[Path("base_target.py")],
            deps=[
                Label("//clockwork/dsl/ir:compiler"),
                Label("//clockwork/dsl/ir:importer"),
                Label("//clockwork/serialization/py:tachyon_dyn"),
            ],
            data=[
                Label(value="//a/b/c:foo_clk"),
            ],
        )
    ]

    remove_whitespace = str.maketrans("", "", " \t\n")
    assert (
        str(base_outputs[0]).translate(remove_whitespace)
        == """
        py_library(
            name = 'base_target',
            srcs = ['base_target.py'],
            deps = [
                '//clockwork/dsl/ir:compiler',
                '//clockwork/dsl/ir:importer',
                '//clockwork/serialization/py:tachyon_dyn'
            ],
            data = ['//a/b/c:foo_clk'],
        )""".translate(remove_whitespace)
    )

    derived_target = module.inner_scope.lookup("derived_target", recursive=False)
    assert isinstance(derived_target, py_target.PyTarget)
    derived_outputs = derived_target.output_targets()
    assert derived_outputs == [
        PyLibrary(
            name="derived_target",
            srcs=[Path("derived_target.py")],
            # All py bindings currently don't require explicit
            # instantiation of nested types. Dependencies are
            # therefore only infra required dependencies.
            deps=[
                Label("//clockwork/dsl/ir:compiler"),
                Label("//clockwork/dsl/ir:importer"),
                Label("//clockwork/serialization/py:tachyon_dyn"),
            ],
            data=[
                Label(value="//a/b/c:foo_clk"),
            ],
        )
    ]
