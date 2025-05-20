# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Test ExternalState."""

from textwrap import dedent

from clockwork.dsl.cpp import context, typereg, types
from clockwork.dsl.ir import cog, compiler, cpp_extern, cpp_target, extern_type, importer
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


def test_extern_state() -> None:
    """Test ExternState."""
    source = dedent(
        """
        // Doc
        extern_type CxxState;

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
