# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Basic sanity tests that nanobind target is parsed correctly and the nanobind registry is populated."""

from __future__ import annotations

from pathlib import Path

import pytest
from clockwork.dsl.bazel.nanobind_targets import PyCcBinding
from clockwork.dsl.bazel.targets import Label
from clockwork.dsl.ir import (
    clkenum,
    compiler,
    nanobind_binding,
    nanobind_target,
    primitive,
    typesys,
)
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.nanobind_target_render import NanobindDependencies, get_dependent_targets
from clockwork.dsl.ir.nanobinding_registry import lookup_binding
from clockwork.dsl.ir.statement import ImmutableBinding


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_nanobind_target(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomsg.clk")),
        fs_importer,
    )
    nanobind_target_ir = module.inner_scope.lookup("hello_msg_clk_nb", recursive=False)
    assert nanobind_target_ir is not None

    # parsed the right number of nodes
    assert isinstance(nanobind_target_ir, nanobind_target.NanobindTarget)
    assert len(nanobind_target_ir.constants) == 1
    assert len(nanobind_target_ir.nanobind_bindings) == 4

    # check the constant
    assert "some_constant" in nanobind_target_ir.constants
    assert isinstance(nanobind_target_ir.constants["some_constant"], ImmutableBinding)
    assert isinstance(nanobind_target_ir.constants["some_constant"].value, primitive.DecimalLiteral)
    assert int(nanobind_target_ir.constants["some_constant"].value.value) == 42

    # check the registry
    for binding in nanobind_target_ir.nanobind_bindings:
        assert isinstance(binding, nanobind_binding.NanobindBinding)
        assert binding.resolved is not None
        target_info = lookup_binding(binding.resolved.original_type, nanobind_target_ir.module.context)
        assert target_info is not None
        assert target_info.target_id.name == "hello_msg_clk_nb"

    # enum sanity check
    assert isinstance(nanobind_target_ir.nanobind_bindings[0], nanobind_binding.NanobindBinding)
    assert nanobind_target_ir.nanobind_bindings[0].resolved is not None
    assert isinstance(nanobind_target_ir.nanobind_bindings[0].resolved.original_type, clkenum.ResolvedEnum)
    assert (
        lookup_binding(
            nanobind_target_ir.nanobind_bindings[0].resolved.original_type, nanobind_target_ir.module.context
        )
        is not None
    )
    nanobind_target_ir.nanobind_bindings[0].resolved.original_type.name = "foo"

    # sanity check on the 3 schemas
    assert isinstance(nanobind_target_ir.nanobind_bindings[1], nanobind_binding.NanobindBinding)
    assert nanobind_target_ir.nanobind_bindings[1].resolved is not None
    assert isinstance(nanobind_target_ir.nanobind_bindings[1].resolved.original_type, typesys.Instantiation)

    assert isinstance(nanobind_target_ir.nanobind_bindings[2], nanobind_binding.NanobindBinding)
    assert nanobind_target_ir.nanobind_bindings[2].resolved is not None
    assert isinstance(nanobind_target_ir.nanobind_bindings[2].resolved.original_type, typesys.Instantiation)

    assert isinstance(nanobind_target_ir.nanobind_bindings[3], nanobind_binding.NanobindBinding)
    assert nanobind_target_ir.nanobind_bindings[3].resolved is not None
    assert isinstance(nanobind_target_ir.nanobind_bindings[3].resolved.original_type, typesys.Instantiation)


def test_target_outputs(fs_importer: FilesystemImporter) -> None:
    source = """
        // trivial base
        schema BaseA
        {
            uuid: 6c816f32-6088-4b50-a1a6-7cfd73c567f3;

            fields
            {
               // Doc
               #0 value: UInt64;
            }
        }

        // enum that will be contained in a schema
        enum EnumB
        {
            uuid: e150cedf-533b-4c3b-a2ba-132964a4248f;

            values
            {
                // Doc
                #0 value_a default;

                // Doc
                #1 value_b;
            }
        }

        // schema containing an enum
        schema BaseB
        {
            uuid: 67ab0f10-42c8-4023-9342-c30673941c11;

            fields
            {
               // Doc
               #0 value: EnumB;
            }
        }

        // enum that won't be contained in a schema
        enum EnumC
        {
            uuid: ac1cc4bb-82de-49e2-af49-aa093aa534a8;

            values
            {
                // Doc
                #0 value_a default;

                // Doc
                #1 value_b;
            }
        }

        // Enum that represents bit flags
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
                // Bit0 + Bit1
                #4 flag12 { underlying_value: 3; }
            }
        }

        // Doc
        schema Derived
        {
            uuid: 8140f867-b273-418d-92c5-671d8ed17155;

            fields
            {
               // Doc
               #0 base_a_field: BaseA;

               // Doc
               #2 base_b_field: BaseB;

               // Doc
               #3 enum_c_field: EnumC;
            }
        }

        cpp_target base_a_cpp
        {
          options
          {
            namespace base_a;
          }
          schema BaseA;
          representation Tachyon<BaseA>;
          interface Tappy<BaseA>;
        }

        cpp_target base_b_cpp
        {
          options
          {
            namespace base_b;
          }
          enum EnumB;
          schema BaseB;
          representation Tachyon<BaseB>;
          interface Tappy<BaseB>;
        }

        cpp_target enum_c_cpp
        {
          options
          {
            namespace enum_c;
          }
          enum EnumC;
        }

        cpp_target derived_cpp
        {
          options
          {
            namespace derived;
          }
          schema Derived;
          representation Tachyon<Derived>;
          interface Tappy<Derived>;
        }

        nanobind_target base_a_nb
        {
          binding Tappy<BaseA>;
        }

        nanobind_target base_b_nb
        {
          binding EnumB;
          binding Tappy<BaseB>;
        }

        nanobind_target enum_c_nb
        {
          binding EnumC;
        }

        nanobind_target enum_flags_nb
        {
          binding EnumFlags;
        }

        nanobind_target derived_nb
        {
          binding Tappy<Derived>;
        }
        """

    # parse the module and make sure we got both nanobind targets
    module = compiler.compile_source_text(
        source, ModuleID(CLK_REPO, "path::to::mod::nanobind_target_test"), fs_importer
    )

    base_a_ir = module.inner_scope.lookup("base_a_nb", recursive=False)
    assert isinstance(base_a_ir, nanobind_target.NanobindTarget)

    base_b_ir = module.inner_scope.lookup("base_b_nb", recursive=False)
    assert isinstance(base_b_ir, nanobind_target.NanobindTarget)

    enum_c_ir = module.inner_scope.lookup("enum_c_nb", recursive=False)
    assert isinstance(enum_c_ir, nanobind_target.NanobindTarget)

    enum_flags_ir = module.inner_scope.lookup("enum_flags_nb", recursive=False)
    assert isinstance(enum_flags_ir, nanobind_target.NanobindTarget)

    derived_ir = module.inner_scope.lookup("derived_nb", recursive=False)
    assert isinstance(derived_ir, nanobind_target.NanobindTarget)

    # compute target dependencies and check them
    base_a_dependent_targets = get_dependent_targets(
        [binding.get_resolved() for binding in base_a_ir.nanobind_bindings],
        base_a_ir._get_target_id(),
    )
    assert base_a_dependent_targets == NanobindDependencies()

    base_b_dependent_targets = get_dependent_targets(
        [binding.get_resolved() for binding in base_b_ir.nanobind_bindings],
        base_b_ir._get_target_id(),
    )
    assert base_b_dependent_targets == NanobindDependencies()

    enum_c_dependent_targets = get_dependent_targets(
        [binding.get_resolved() for binding in enum_c_ir.nanobind_bindings],
        enum_c_ir._get_target_id(),
    )
    assert enum_c_dependent_targets == NanobindDependencies()

    derived_dependent_targets = get_dependent_targets(
        [binding.get_resolved() for binding in derived_ir.nanobind_bindings],
        derived_ir._get_target_id(),
    )

    assert derived_dependent_targets == NanobindDependencies(
        python_targets={other._get_target_id() for other in [base_a_ir, base_b_ir, enum_c_ir]}
    )

    remove_whitespace = str.maketrans("", "", " \t\n")

    # output targets and check them
    # bases
    for dep_ir, dep_name in [(base_a_ir, "base_a"), (base_b_ir, "base_b"), (enum_c_ir, "enum_c")]:
        (dep_target,) = dep_ir.output_targets()
        assert dep_target == PyCcBinding(
            name=f"{dep_name}_nb",
            srcs=[Path(f"{dep_name}_nb.{ext}") for ext in ("inl", "cc", "hh")],
            deps=[
                Label("//jewels/nanobind/clk_bindings/common:common_cc"),
                Label(f"//path/to/mod:{dep_name}_cpp"),
            ],
            py_deps=[
                Label("//jewels/nanobind/clk_bindings/common:common_py"),
            ],
            data=[
                Label("//path/to/mod:nanobind_target_test_clk"),
            ],
        )

        assert (
            str(dep_target).translate(remove_whitespace)
            == f"""
            py_cc_binding(
                name = '{dep_name}_nb',
                srcs = [
                    '{dep_name}_nb.inl',
                    '{dep_name}_nb.cc',
                    '{dep_name}_nb.hh'
                ],
                deps = [
                    '//jewels/nanobind/clk_bindings/common:common_cc',
                    '//path/to/mod:{dep_name}_cpp'
                ],
                py_deps = [
                    '//jewels/nanobind/clk_bindings/common:common_py'
                ],
                data=[
                    '//path/to/mod:nanobind_target_test_clk'
                ],
            )""".translate(remove_whitespace)
        )

    # derived
    (derived_target,) = derived_ir.output_targets()
    assert derived_target == PyCcBinding(
        name="derived_nb",
        srcs=[Path("derived_nb.inl"), Path("derived_nb.cc"), Path("derived_nb.hh")],
        deps=[
            Label("//jewels/nanobind/clk_bindings/common:common_cc"),
            Label("//path/to/mod:derived_cpp"),
        ],
        py_deps=[
            Label("//jewels/nanobind/clk_bindings/common:common_py"),
            Label("//path/to/mod:base_a_nb"),
            Label("//path/to/mod:base_b_nb"),
            Label("//path/to/mod:enum_c_nb"),
        ],
        data=[
            Label("//path/to/mod:nanobind_target_test_clk"),
        ],
    )

    assert (
        str(derived_target).translate(remove_whitespace)
        == """
        py_cc_binding(
            name = 'derived_nb',
            srcs = [
                'derived_nb.inl',
                'derived_nb.cc',
                'derived_nb.hh'
            ],
            deps = [
                '//jewels/nanobind/clk_bindings/common:common_cc',
                '//path/to/mod:derived_cpp'
            ],
            py_deps = [
                '//jewels/nanobind/clk_bindings/common:common_py',
                '//path/to/mod:base_a_nb',
                '//path/to/mod:base_b_nb',
                '//path/to/mod:enum_c_nb'
            ],
            data=[
                '//path/to/mod:nanobind_target_test_clk'
            ],
        )""".translate(remove_whitespace)
    )
