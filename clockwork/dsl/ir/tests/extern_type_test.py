# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Test ExternalState."""

from textwrap import dedent

import pytest
from clockwork.dsl.cpp import context, typereg, types
from clockwork.dsl.ir import clkbuiltins, cog, compiler, cpp_extern, cpp_target, extern_type, importer, schema, typesys
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


def test_extern_state() -> None:
    """Test ExternState."""
    source = dedent(
        """
        // Snapshot schema
        schema Snapshot
        {
            fields
            {
                // Snapshot value
                #0 value: UInt16;
            }
        }

        // Doc
        extern_type CxxState
        {
            serialized_form
            {
                representation: Tachyon<Snapshot>;
            }
        }

        // No serialized form
        extern_type CxxStateWithoutSerialization;

        //
        cog TestCog
        {
            states
            {
                state_hello: CxxState
                {
                    mutable: true;
                }
            }

            execution
            {
                condition periodic: time_since_last_exec(200ms);
                execute when: periodic;
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
              name: CxxState;
            }
          }
        }
        """
    )
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_extern_state"), fs_importer)
    cxx_state_ir = module.inner_scope.lookup("CxxState")
    assert isinstance(cxx_state_ir, extern_type.ExternType)
    assert cxx_state_ir.fqn == f"@{CLK_REPO}::test_extern_state.CxxState"
    assert isinstance(cxx_state_ir.serialized_form, typesys.Instantiation)
    assert cxx_state_ir.serialized_form.instantiates is clkbuiltins.TACHYON
    schema_arg = cxx_state_ir.serialized_form.arguments["schema"]
    assert isinstance(schema_arg, schema.Schema)
    assert schema_arg.name == "Snapshot"
    cxx_state_without_serialization = module.inner_scope.lookup("CxxStateWithoutSerialization")
    assert isinstance(cxx_state_without_serialization, extern_type.ExternType)
    assert cxx_state_without_serialization.serialized_form is None
    test_target_ir = module.inner_scope.lookup("test_target")
    assert isinstance(test_target_ir, cpp_target.CppTarget)
    cpp_extern_ir = test_target_ir.externs[0]
    assert isinstance(cpp_extern_ir, cpp_extern.CppExtern)
    assert isinstance(cpp_extern_ir.extern_types[0].subclass_handler, cpp_extern.CppExternHandler)
    registered_cpp_type = typereg.get_cpp_type(module.context, cxx_state_ir)
    assert registered_cpp_type == types.CppType(
        [context.Header(CLK_REPO, "a/b/c.hh", iwyu_pragma="IWYU pragma: export")], "CxxState", "a::b::c"
    )
    cog_ir = module.inner_scope.lookup("TestCog")
    assert isinstance(cog_ir, cog.Cog)
    assert isinstance(cog_ir.states["state_hello"].message_type, extern_type.ExternType)


@pytest.mark.parametrize("serialized_form", ["Tappy<Snapshot>", "Tachyon<UInt16>"])
def test_extern_state_rejects_invalid_serialized_form(serialized_form: str) -> None:
    """Test that serialized forms are concrete Tachyon schemas."""
    source = dedent(
        f"""
        // Snapshot schema
        schema Snapshot
        {{
            fields
            {{
                // Snapshot value
                #0 value: UInt16;
            }}
        }}

        // Doc
        extern_type CxxState
        {{
            serialized_form
            {{
                representation: {serialized_form};
            }}
        }}
        """
    )
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    with pytest.raises(TypeError, match="concrete Tachyon<Schema>"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_invalid_extern_state"), fs_importer)
