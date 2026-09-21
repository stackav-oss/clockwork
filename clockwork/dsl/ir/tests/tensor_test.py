# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for ir.constant."""

from __future__ import annotations

import re
from typing import Final

import pytest
from clockwork.dsl.ir import clkbuiltins, clkenum, compiler, schema, strongtypes, tensor_builtins, typesys
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_valid_tensor(fs_importer: FilesystemImporter) -> None:
    source: Final = """
dim1 = 5;
stride3 = 1;
// Doc.
strong_type Float16
{
  underlying_type: UInt16;
}

// Doc.
schema TensorMessage
{
    fields
    {
        // A tensor.
        #0 tensor0: Tensor<Float32, [dim1, 6, 7], TensorLayout::column_major>;

        // Another tensor.
        #1 tensor1: Tensor<Int16, [5, 6, 7]>;

        // A tensor with a custom stride list.
        #2 tensor2: Tensor<Float64, [5, 6, 7], [42, 7, stride3]>;

        // A tensor with a custom stride list.
        #3 tensor3: Tensor<Float16, [1, 2, 3]>;
    }
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    assert module is not None

    msg_ir = module.inner_scope.lookup("TensorMessage", recursive=False)
    assert isinstance(msg_ir, schema.Schema)
    resolved_msg = msg_ir.get_resolved()

    tensor0 = resolved_msg.fields[0]
    assert isinstance(tensor0.type_info, typesys.Instantiation)
    assert tensor0.type_info.instantiates is tensor_builtins.TENSOR
    assert isinstance(tensor0.type_info.arguments["dimensions"], typesys.Values)
    assert tensor0.type_info.arguments["dimensions"].value_key() == "[@clockwork::foo::dim1,6,7]"
    assert isinstance(tensor0.type_info.arguments["layout"], clkenum.ValueRef)
    assert tensor0.type_info.arguments["layout"] is tensor_builtins.TENSOR_LAYOUT_ENUM.lookup("column_major")
    assert tensor_builtins.get_tensor_element_type(tensor0.type_info) is clkbuiltins.FLOAT32
    assert tensor_builtins.get_tensor_shape(tensor0.type_info) == [5, 6, 7]
    assert tensor_builtins.get_tensor_strides(tensor0.type_info) == [1, 5, 30]

    tensor1 = resolved_msg.fields[1]
    assert isinstance(tensor1.type_info, typesys.Instantiation)
    assert tensor1.type_info.instantiates is tensor_builtins.TENSOR
    assert isinstance(tensor1.type_info.arguments["dimensions"], typesys.Values)
    assert tensor1.type_info.arguments["dimensions"].value_key() == "[5,6,7]"
    assert isinstance(tensor1.type_info.arguments["layout"], clkenum.ValueRef)
    assert tensor1.type_info.arguments["layout"] is tensor_builtins.TENSOR_LAYOUT_ENUM.lookup("row_major")
    assert tensor_builtins.get_tensor_element_type(tensor1.type_info) is clkbuiltins.INT16
    assert tensor_builtins.get_tensor_shape(tensor1.type_info) == [5, 6, 7]
    assert tensor_builtins.get_tensor_strides(tensor1.type_info) == [42, 7, 1]

    tensor2 = resolved_msg.fields[2]
    assert isinstance(tensor2.type_info, typesys.Instantiation)
    assert tensor2.type_info.instantiates is tensor_builtins.TENSOR
    assert isinstance(tensor2.type_info.arguments["dimensions"], typesys.Values)
    assert tensor2.type_info.arguments["dimensions"].value_key() == "[5,6,7]"
    assert isinstance(tensor2.type_info.arguments["layout"], typesys.Values)
    assert tensor_builtins.get_tensor_element_type(tensor2.type_info) is clkbuiltins.FLOAT64
    assert tensor_builtins.get_tensor_shape(tensor2.type_info) == [5, 6, 7]
    assert tensor_builtins.get_tensor_strides(tensor2.type_info) == [42, 7, 1]

    tensor3 = resolved_msg.fields[3]
    assert isinstance(tensor3.type_info, typesys.Instantiation)
    assert tensor3.type_info.instantiates is tensor_builtins.TENSOR
    assert isinstance(tensor_builtins.get_tensor_element_type(tensor3.type_info), strongtypes.StrongType)


def test_invalid_tensor(fs_importer: FilesystemImporter) -> None:
    source = """
// Doc.
schema TensorMessage
{
    fields
    {
        // A tensor.
        #0 tensor0: Tensor<Float32, [1, 2, 3], 456>;
    }
}
"""
    with pytest.raises(TypeError):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)

    source = """
// Doc.
schema TensorMessage
{
    fields
    {
        // A tensor.
        #0 tensor0: Tensor<Float32, 123>;
    }
}
"""
    with pytest.raises(TypeError):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)

    source = """
// Doc.
schema TensorMessage
{
    fields
    {
        // A tensor.
        #0 tensor0: Tensor<Float32, [1, 2, "a string!?"]>;
    }
}
"""
    with pytest.raises(TypeError, match=re.escape("Type inference failed: ::String and ::UInt64 are disjoint")):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)

    source = """
// Doc.
schema TensorMessage
{
    fields
    {
        // A tensor.
        #0 tensor0: Tensor<Float32, []>;
    }
}
"""
    with pytest.raises(SyntaxError):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
