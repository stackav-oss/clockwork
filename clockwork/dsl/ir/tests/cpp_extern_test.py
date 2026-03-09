# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Test the Schema IR module."""

from textwrap import dedent

import pytest
from clockwork.dsl.cpp import context, typereg, types
from clockwork.dsl.ir import (
    clkbuiltins,
    compiler,
    cpp_target,
    importer,
    schema,
    strongtypes,
)
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.serialization import tap


def test_cpp_extern() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Docs
        strong_type MyStrongType
        {
          underlying_type: Float32;
        }

        cpp_target test_target
        {
          options
          {
            namespace unused;
          }

          extern
          {
            header_file: "a/b/c.hh";
            namespace: a::b::c;
            type
            {
              name: MyStrongType;
              factory: create_my_strong_type;
            }
          }
        }
        """,
    )
    with pytest.raises(
        ValueError, match=r"Use of cpp_extern is restricted and not allowed in module @clockwork::not_on_the_allowlist"
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "not_on_the_allowlist"), importer=fs_importer)

    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "cpp_extern_test"), importer=fs_importer)

    my_strong_type = module.inner_scope.lookup("MyStrongType")
    assert isinstance(my_strong_type, strongtypes.StrongType)
    assert my_strong_type.typespec == clkbuiltins.FLOAT32

    cpp_type = typereg.get_cpp_type(module.context, my_strong_type)
    # This would have been the registration if without an extern.
    assert cpp_type != typereg.get_cpp_type(module.context, clkbuiltins.FLOAT32)

    assert cpp_type == types.CppType([context.Header(CLK_REPO, "test_target.hh")], "MyStrongType", "a::b::c")

    test_target = module.inner_scope.lookup("test_target")
    assert isinstance(test_target, cpp_target.CppTarget)

    cpp_mod = test_target.render_cpp_entities()

    header = cpp_mod.header_chunk.render_str(render_includes=True).strip()
    expected_header = dedent("""
      #include "a/b/c.hh" // IWYU pragma: export
      namespace unused
      {
      } // namespace unused
      namespace unused
      {
      // Interface and instantiation aliases
      } // namespace unused
    """).strip()
    assert header == expected_header

    implementation = cpp_mod.implementation_chunk.render_str(render_includes=True).strip()
    expected_implementation = dedent("""
      #include "test_target.hh"
      namespace unused
      {
      } // namespace unused
      static_assert(alignof(::a::b::c::MyStrongType) == 4);
      static_assert(sizeof(::a::b::c::MyStrongType) == 4);
      static_assert(std::is_same<decltype(::a::b::c::create_my_strong_type(std::declval<float>())), ::a::b::c::MyStrongType>::value);
      namespace unused
      {
      // Interface and instantiation aliases
      } // namespace unused
    """).strip()
    assert implementation == expected_implementation


def test_cpp_extern_init_value() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Docs
        strong_type MyInitStrongType
        {
          underlying_type: Int32;
        }

        // Docs
        schema MySchema
        {
          fields
          {
            // Docs
            #1 strong_type: MyInitStrongType = 1.0;
          }
        }

        cpp_target test_target
        {
          options
          {
            namespace unused;
          }

          extern
          {
            header_file: "a/b/c.hh";
            namespace: a::b::c;
            type
            {
              name: MyInitStrongType;
              factory: create_my_strong_type;
            }
          }
        }
        """,
    )
    with pytest.raises(
        TypeError, match=r"Attempt to unify NumericType\.FLOAT type with StrongType\(name='MyInitStrongType'"
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "cpp_extern_test"), importer=fs_importer)


def test_cpp_extern_init_value_no_factory() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Docs
        strong_type MyInitStrongTypeNoFactory
        {
          underlying_type: Int32;
        }

        // Docs
        schema MySchema
        {
          fields
          {
            // Docs
            #1 strong_type: MyInitStrongTypeNoFactory = 1;
          }
        }

        cpp_target test_target
        {
          options
          {
            namespace unused;
          }

          extern
          {
            header_file: "a/b/c.hh";
            namespace: a::b::c;
            type
            {
              name: MyInitStrongTypeNoFactory;
            }
          }
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "cpp_extern_test"), importer=fs_importer)
    my_schema = module.inner_scope.lookup("MySchema")
    assert isinstance(my_schema, schema.Schema)

    strong_type = my_schema.fields[1]
    assert strong_type.cur_name == "strong_type"
    with pytest.raises(
        RuntimeError,
        match=r"Unable to convert to literal for StrongType 'MyInitStrongTypeNoFactory'\.  Need to register an appropriate factory function in the cpp_target extern block\.",
    ):
        tap.value_to_cpp(strong_type.init_value)
