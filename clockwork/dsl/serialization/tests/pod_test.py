# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pod module."""

from pathlib import Path

import pytest
from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl.cpp.context import CppContext
from clockwork.dsl.ir import compiler, importer, parse, schema
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.serialization import pod


@pytest.fixture()
def hellomsg_cst() -> parse.CstParseContext[cst.Module]:
    return parse.clk_source_to_cst("clockwork/dsl/tests/support/hellomsg.clk")


@pytest.fixture()
def hellomsg() -> schema.Schema:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    hellomsg_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomsg.clk")),
        fs_importer,
    )
    assert hellomsg_module.cst_node is not None
    result = hellomsg_module.inner_scope.lookup("HelloMsg")
    assert isinstance(result, schema.Schema)
    return result


def test_pod(hellomsg: schema.Schema) -> None:
    cpp_context = CppContext()
    hellopod = pod.SchemaPod(
        schema_ir=hellomsg,
        cpp_namespace="clockwork",
        class_name="HelloMsgPod",
        header_path=None,
    )
    pod_str = hellopod.render_cpp_definition(cpp_context)
    expected_output = """\
namespace clockwork
{

struct HelloMsgPod
{
  int64_t seqno;
  ::std::array<::std::byte, 1024U> data;
  ::jewels::Uuid<::clockwork::demo::SampleTag> greeting_id;
  ::jewels::Uuid<::clockwork::demo::HelloMsg> msg_id;
};
static_assert(std::is_pod_v<HelloMsgPod>);

}
"""
    assert pod_str.strip() == expected_output.strip()

    print("\n".join(cpp_context.render_includes()))
    print(pod_str)


def test_pod_empty_namespace(hellomsg: schema.Schema) -> None:
    cpp_context = CppContext()
    hellopod = pod.SchemaPod(
        schema_ir=hellomsg,
        cpp_namespace="",
        class_name="HelloMsgPod",
        header_path=None,
    )
    pod_str = hellopod.render_cpp_definition(cpp_context)
    expected_output = """\
struct HelloMsgPod
{
  int64_t seqno;
  ::std::array<::std::byte, 1024U> data;
  ::jewels::Uuid<::clockwork::demo::SampleTag> greeting_id;
  ::jewels::Uuid<::clockwork::demo::HelloMsg> msg_id;
};
static_assert(std::is_pod_v<HelloMsgPod>);"""
    assert "namespace {" not in pod_str
    assert pod_str.strip() == expected_output.strip()
