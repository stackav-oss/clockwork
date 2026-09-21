# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Test the Cog IR module."""

import re
from decimal import Decimal
from pathlib import Path
from typing import Final
from unittest.mock import MagicMock

import pytest
from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl import compiler_context
from clockwork.dsl.ir import aligner, box, clkbuiltins, cog, compiler, expr, node, parse, primitive, typesys
from clockwork.dsl.ir.cog_components import CogAlignedInputDef
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def hellocog_cst() -> parse.CstParseContext[cst.Module]:
    return parse.clk_source_to_cst("clockwork/dsl/tests/support/hellocog.clk")


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


@pytest.fixture()
def hellocog_ir(fs_importer: FilesystemImporter) -> cog.Cog:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellocog.clk")),
        importer=fs_importer,
    )
    cog_ir = module.inner_scope.lookup("HelloCog")
    assert isinstance(cog_ir, cog.Cog)
    return cog_ir


def test_cog_inputs(hellocog_ir: cog.Cog) -> None:
    assert not hellocog_ir.is_generic()
    assert len(hellocog_ir.inputs) == 4
    latest = hellocog_ir.inputs["latest_hello"]
    multi_publisher_hello = hellocog_ir.inputs["multi_publisher_hello"]
    multi_connect_hello = hellocog_ir.inputs["multi_connect_hello"]
    history = hellocog_ir.inputs["history_of_hellos"]

    assert latest.doc is None
    assert multi_publisher_hello.doc == node.Doc(
        module=hellocog_ir.module, cst_node=None, value="Multi-publisher input"
    )
    assert multi_connect_hello.doc == node.Doc(module=hellocog_ir.module, cst_node=None, value="Multi-connect input")
    assert isinstance(multi_connect_hello.elements, list)
    assert len(multi_connect_hello.elements) == 2
    assert multi_connect_hello.elements[0].name == "multi_connect_hello__0"
    assert multi_connect_hello.elements[1].name == "multi_connect_hello__1"
    assert history.doc == node.Doc(module=hellocog_ir.module, cst_node=None, value="Make sure larger views work")


@pytest.fixture()
def param_cog_ir(fs_importer: FilesystemImporter) -> cog.Cog:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/parameterized_box.clk")),
        importer=fs_importer,
    )
    cog_ir = module.inner_scope.lookup("ParamTestCog")
    assert isinstance(cog_ir, cog.Cog)
    return cog_ir


def test_cog_parameters(param_cog_ir: cog.Cog) -> None:
    assert param_cog_ir.is_generic()
    assert len(param_cog_ir.parameters) == 6

    assert "group_id" in param_cog_ir.parameters
    group_id = param_cog_ir.parameters["group_id"]
    assert group_id.get_typeval() is clkbuiltins.STRING

    assert "instance_id" in param_cog_ir.parameters
    instance_id = param_cog_ir.parameters["instance_id"]
    assert instance_id.get_typeval() is clkbuiltins.STRING

    assert "config_type" in param_cog_ir.parameters
    config_type = param_cog_ir.parameters["config_type"]
    assert config_type.get_typeval() is clkbuiltins.TYPE_TYPE

    assert "state_type" in param_cog_ir.parameters
    state_type = param_cog_ir.parameters["state_type"]
    assert state_type.get_typeval() is clkbuiltins.TYPE_TYPE

    assert "input_type" in param_cog_ir.parameters
    input_type = param_cog_ir.parameters["input_type"]
    assert input_type.get_typeval() is clkbuiltins.TYPE_TYPE

    assert "output_type" in param_cog_ir.parameters
    output_type = param_cog_ir.parameters["output_type"]
    assert output_type.get_typeval() is clkbuiltins.TYPE_TYPE


def test_invalid_messages_condition() -> None:
    """Tests invalid bounds throw exception."""
    lower_bound: Final = 3
    upper_bound: Final = 2
    type_info = clkbuiltins.UINT64
    module = node.Module(
        doc=None,
        module_id=ModuleID(CLK_REPO, "testmod"),
        inner_scope=node.Scope(parent=clkbuiltins.BUILTINS_SCOPE, uniq_path="testmod", module_id_for_errors=None),
        terminals=None,
        cst_node=None,
        unresolved_imports=[],
        context=compiler_context.CompilerContext(),
        generates=None,
        inner_attrs=None,
    )
    lower_bound_expr = expr.SimpleExpr(
        value=MagicMock(spec=typesys.Value),
        resolved_value=primitive.DecimalValue(type_info=type_info, value=Decimal(value=lower_bound)),
        cst_node=MagicMock(spec=cst.Expr),
        module=module,
        type_info=clkbuiltins.UINT64,
    )
    upper_bound_expr = expr.SimpleExpr(
        value=MagicMock(spec=typesys.Value),
        resolved_value=primitive.DecimalValue(type_info=type_info, value=Decimal(value=upper_bound)),
        cst_node=MagicMock(spec=cst.Expr),
        module=module,
        type_info=clkbuiltins.UINT64,
    )
    cond = cog.MessagesPresent(module, None, "input_name", lower_bound_expr, upper_bound_expr)
    with pytest.raises(
        ValueError,
        match=re.escape(f"Invalid bounds: max {upper_bound} is smaller than min {lower_bound}"),
    ):
        cond.resolve()


def test_skip_threshold(fs_importer: FilesystemImporter) -> None:
    """Test skip threshold syntax."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog SkipCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>
        {
            skip_threshold: 123;
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
// Doc.
cog PlainCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>;
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "skip_threshold_test"), importer=fs_importer)
    skip_cog = module.inner_scope.lookup("SkipCog")
    assert isinstance(skip_cog, cog.Cog)
    plain_cog = module.inner_scope.lookup("PlainCog")
    assert isinstance(plain_cog, cog.Cog)
    plain_cog.resolve()
    assert "message_in" in plain_cog.inputs
    assert plain_cog.inputs["message_in"].view_params.skip_threshold is None
    skip_cog.resolve()
    assert "message_in" in skip_cog.inputs
    assert isinstance(skip_cog.inputs["message_in"].view_params.skip_threshold, int)
    assert skip_cog.inputs["message_in"].view_params.skip_threshold == 123


def test_invalid_skip_threshold(fs_importer: FilesystemImporter) -> None:
    """Test skip threshold errors."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog SkipCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>
        {
            skip_threshold: "no";
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    with pytest.raises(TypeError, match=r"Type inference failed: ::UInt64 and ::String are disjoint"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "skip_threshold_bad_type"), importer=fs_importer)

    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog SkipCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>
        {
            skip_threshold: -1;
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    with pytest.raises(TypeError, match=r"Attempt to unify NumericType\.SIGNED_INTEGER"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "negative_skip_threshold"), importer=fs_importer)


def test_copy_inputs(fs_importer: FilesystemImporter) -> None:
    """Test syntax for input copying."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog CopyCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>
        {
            copy_inputs: true;
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

// Doc.
cog NoCopyCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>
        {
            copy_inputs: false;
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

// Doc.
cog PlainCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>;
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "copy_inputs_test"), importer=fs_importer)
    copy_cog = module.inner_scope.lookup("CopyCog")
    assert isinstance(copy_cog, cog.Cog)
    no_copy_cog = module.inner_scope.lookup("NoCopyCog")
    assert isinstance(no_copy_cog, cog.Cog)
    plain_cog = module.inner_scope.lookup("PlainCog")
    assert isinstance(plain_cog, cog.Cog)
    plain_cog.resolve()
    assert "message_in" in plain_cog.inputs
    assert not plain_cog.inputs["message_in"].view_params.copy_inputs
    copy_cog.resolve()
    assert "message_in" in copy_cog.inputs
    assert copy_cog.inputs["message_in"].view_params.copy_inputs
    no_copy_cog.resolve()
    assert "message_in" in no_copy_cog.inputs
    assert not no_copy_cog.inputs["message_in"].view_params.copy_inputs


def test_use_device_ptr(fs_importer: FilesystemImporter) -> None:
    """Test syntax for device pointer."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog YesUseDevicePtrCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>
        {
            use_device_ptr: true;
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

// Doc.
cog NoUseDevicePtrCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>
        {
            use_device_ptr: false;
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

// Doc.
cog DefaultCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>;
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "use_device_ptr_test"), importer=fs_importer)
    yes_cog = module.inner_scope.lookup("YesUseDevicePtrCog")
    assert isinstance(yes_cog, cog.Cog)
    no_cog = module.inner_scope.lookup("NoUseDevicePtrCog")
    assert isinstance(no_cog, cog.Cog)
    default_cog = module.inner_scope.lookup("DefaultCog")
    assert isinstance(default_cog, cog.Cog)
    default_cog.resolve()
    assert "message_in" in default_cog.inputs
    assert not default_cog.inputs["message_in"].view_params.use_device_ptr
    yes_cog.resolve()
    assert "message_in" in yes_cog.inputs
    assert yes_cog.inputs["message_in"].view_params.use_device_ptr
    no_cog.resolve()
    assert "message_in" in no_cog.inputs
    assert not no_cog.inputs["message_in"].view_params.use_device_ptr


def test_optional_inputs_and_outputs(fs_importer: FilesystemImporter) -> None:
    """Test syntax for input copying."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog OptionalInputCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>
        {
            connect_optional: true;
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

// Doc
cog OptionalOutputCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>;
    }
    outputs
    {
        message_out: Tappy<HelloMsg>
        {
            connect_optional: true;
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}


// Doc.
cog PlainCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>;
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

// Doc
cog OptionalFalseCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>
        {
            connect_optional: false;
        }
    }
    outputs
    {
        message_out: Tappy<HelloMsg>
        {
            connect_optional: false;
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "copy_inputs_test"), importer=fs_importer)
    optional_input_cog = module.inner_scope.lookup("OptionalInputCog")
    assert isinstance(optional_input_cog, cog.Cog)
    optional_input_cog.resolve()
    assert "message_in" in optional_input_cog.inputs
    assert optional_input_cog.inputs["message_in"].view_params.is_optional
    optional_output_cog = module.inner_scope.lookup("OptionalOutputCog")
    assert isinstance(optional_output_cog, cog.Cog)
    optional_output_cog.resolve()
    assert "message_out" in optional_output_cog.outputs
    assert optional_output_cog.outputs["message_out"].is_optional
    assert not optional_output_cog.inputs["message_in"].view_params.is_optional
    plain_cog = module.inner_scope.lookup("PlainCog")
    assert isinstance(plain_cog, cog.Cog)
    plain_cog.resolve()
    assert "message_in" in plain_cog.inputs
    assert not plain_cog.inputs["message_in"].view_params.is_optional
    optional_false_cog = module.inner_scope.lookup("OptionalFalseCog")
    assert isinstance(optional_false_cog, cog.Cog)
    optional_false_cog.resolve()
    assert "message_in" in optional_false_cog.inputs
    assert not optional_false_cog.inputs["message_in"].view_params.is_optional
    assert "message_out" in optional_false_cog.outputs
    assert not optional_false_cog.outputs["message_out"].is_optional


def test_invalid_copy_inputs(fs_importer: FilesystemImporter) -> None:
    """Test skip threshold errors."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog CopyCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>
        {
            copy_inputs: "huh?";
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    with pytest.raises(TypeError, match=r"Type inference failed: ::Bool and ::String are disjoint"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "copy_inputs_bad_type"), importer=fs_importer)


def test_safety_margin(fs_importer: FilesystemImporter) -> None:
    """Test safety margin syntax."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog SafeCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>
        {
            safety_margin: 123;
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        condition new_msg: new_message(max=1, input=message_in);
        execute when: periodic or new_msg;
    }
}

// Doc.
cog DefaultCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>;
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        condition new_msg: new_message(max=1, input=message_in);
        execute when: periodic or new_msg;
    }
}

// Doc.
cog PlainCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>;
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "safety_margin_test"), importer=fs_importer)
    safe_cog = module.inner_scope.lookup("SafeCog")
    assert isinstance(safe_cog, cog.Cog)
    default_cog = module.inner_scope.lookup("DefaultCog")
    assert isinstance(default_cog, cog.Cog)
    plain_cog = module.inner_scope.lookup("PlainCog")
    assert isinstance(plain_cog, cog.Cog)
    plain_cog.resolve()
    assert "message_in" in plain_cog.inputs
    assert plain_cog.inputs["message_in"].view_params.safety_margin is None
    safe_cog.resolve()
    assert "message_in" in safe_cog.inputs
    assert isinstance(safe_cog.inputs["message_in"].view_params.safety_margin, int)
    assert safe_cog.inputs["message_in"].view_params.safety_margin == 123
    default_cog.resolve()
    assert "message_in" in default_cog.inputs
    # We'll detect this when connecting to a channel and replace with max(view.size, channel.size/2)
    assert isinstance(default_cog.inputs["message_in"].view_params.safety_margin, int)
    assert default_cog.inputs["message_in"].view_params.safety_margin == -1


@pytest.mark.parametrize("maybe_min", [None, -2, -1, 0, 1, 2, 3])
@pytest.mark.parametrize("maybe_max", [None, -2, -1, 0, 1, 2, 3])
@pytest.mark.parametrize("condition_type", ["new_message", "any_message"])
def test_illegal_execution_condition_values(
    fs_importer: FilesystemImporter,
    condition_type: str,
    maybe_min: int | None,
    maybe_max: int | None,
) -> None:
    """Test that the proper error message is raised for illegal execution conditions."""
    cog_name = "MaybeIllegalCog"

    maybe_min_str = f", min={maybe_min}" if maybe_min is not None else ""
    maybe_max_str = f", max={maybe_max}" if maybe_max is not None else ""
    condition = f"{condition_type}(message_in{maybe_min_str}{maybe_max_str})"  # e.g. 'new_message(message_in)' or 'new_message(message_in, min=22)'

    source = f"""
use clockwork::dsl::tests::support::hellomsg::{{HelloMsg}};
// Doc.
cog {cog_name}
{{
    inputs
    {{
        message_in: Tappy<HelloMsg>;
    }}
    execution
    {{
        condition msg_in: {condition};
        execute when: msg_in;
    }}
}}
"""

    def compile_source_text() -> None:
        """Compile the clockwork module."""
        module = compiler.compile_source_text(
            source, ModuleID(CLK_REPO, "illegal_execution_condition"), importer=fs_importer
        )
        my_cog = module.inner_scope.lookup(cog_name)
        assert isinstance(my_cog, cog.Cog)
        my_cog.resolve()
        assert "message_in" in my_cog.inputs
        assert my_cog.inputs["message_in"].view_params.max_msgs == 1

    if maybe_min == 1:
        # Expect an error that 1 is the default and you should omit it.
        expected_error_msg = re.escape(f"""\
Execution condition has 'min' explicitly set to the default (1).
To prevent ambiguity, this is not allowed. Please remove 'min=1' and trust the default.
In @clockwork: illegal_execution_condition.clk:12:55:
        condition msg_in: {condition};
                                                      ^
""")
        with pytest.raises(ValueError, match=expected_error_msg):
            compile_source_text()
    elif maybe_min is not None and maybe_min <= 0:
        # Expect an error that 0 is not allowed.
        expected_error_msg = re.escape(f"""\
Execution condition has invalid 'min' {maybe_min}. It must be at least 1
In @clockwork: illegal_execution_condition.clk:12:55:
        condition msg_in: {condition};
                                                      ^
""")
        with pytest.raises(ValueError, match=expected_error_msg):
            compile_source_text()
    elif maybe_max is not None:
        min_msgs = 1 if maybe_min is None else maybe_min
        max_msgs = maybe_max
        if max_msgs >= min_msgs:
            # Should compile without error.
            compile_source_text()
        else:
            # Expect an error that max is less than min.
            column = 55
            if maybe_min is not None:
                column += len(f", min={maybe_min}")
            expected_error_msg = re.escape(f"""\
Invalid bounds: max {max_msgs} is smaller than min {min_msgs}
In @clockwork: illegal_execution_condition.clk:12:{column}:
        condition msg_in: {condition};
""")
            with pytest.raises(ValueError, match=expected_error_msg):
                compile_source_text()
    else:
        # Should compile without error.
        compile_source_text()


@pytest.mark.parametrize("maybe_max_msgs", [None, 0, 1, 2, 3])
@pytest.mark.parametrize("condition_type", ["new_message", "any_message"])
def test_illegal_input_view_max_msgs(
    fs_importer: FilesystemImporter,
    condition_type: str,
    maybe_max_msgs: int | None,
) -> None:
    """Test that the proper error message is raised for illegal execution conditions."""
    cog_name = "MaybeIllegalCog"

    condition = f"{condition_type}(message_in)"  # e.g. 'new_message(message_in)' or 'new_message(message_in, min=22)'

    maybe_max_msgs_str = f"max_msgs: {maybe_max_msgs};" if maybe_max_msgs is not None else ""
    source = f"""
use clockwork::dsl::tests::support::hellomsg::{{HelloMsg}};
// Doc.
cog {cog_name}
{{
    inputs
    {{
        message_in: Tappy<HelloMsg>
        {{
            {maybe_max_msgs_str}
        }}
    }}
    execution
    {{
        condition msg_in: {condition};
        execute when: msg_in;
    }}
}}
"""

    def compile_source_text() -> None:
        """Compile the clockwork module."""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "illegal_max_msgs"), importer=fs_importer)
        my_cog = module.inner_scope.lookup(cog_name)
        assert isinstance(my_cog, cog.Cog)
        my_cog.resolve()
        assert "message_in" in my_cog.inputs
        assert my_cog.inputs["message_in"].view_params.max_msgs == (maybe_max_msgs if maybe_max_msgs is not None else 1)

    if maybe_max_msgs is not None and maybe_max_msgs <= 0:
        expected_error_msg = re.escape(f"""\
Input view has invalid 'max_msgs' {maybe_max_msgs}. It must be at least 1.
In @clockwork: illegal_max_msgs.clk:10:13:
            max_msgs: {maybe_max_msgs};
            ^
""")
        with pytest.raises(ValueError, match=expected_error_msg):
            compile_source_text()
    elif maybe_max_msgs == 1:
        # Expect an error that 1 is the default and you should omit it.
        expected_error_msg = re.escape(
            """\
Input view has 'max_msgs' explicitly set to the default (1).
To prevent ambiguity, this is not allowed. Please remove 'max_msgs: 1' and trust the default.
In @clockwork: illegal_max_msgs.clk:10:13:
            max_msgs: 1;
            ^
"""
        )
        with pytest.raises(ValueError, match=expected_error_msg):
            compile_source_text()
    else:
        # Should compile without error.
        compile_source_text()


def test_invalid_safety_margin(fs_importer: FilesystemImporter) -> None:
    """Test safety margin errors."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog SafeCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>
        {
            safety_margin: "what?";
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    with pytest.raises(TypeError, match=r"Type inference failed: ::UInt64 and ::String are disjoint"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "safety_margin_bad_type"), importer=fs_importer)

    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog SafeCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>
        {
            safety_margin: 123;
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    with pytest.raises(
        ValueError,
        match=r"Inputs may only specify a safety margin when associated with a new_message condition that uses the 'max' parameter",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "safety_margin_no_max"), importer=fs_importer)


def test_rate_limit(fs_importer: FilesystemImporter) -> None:
    """Test rate limit syntax."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog RateLimitCog
{
    outputs
    {
        foo: Tappy<HelloMsg>;
        bar: Tappy<HelloMsg>;
    }
    execution
    {
        rate limit foo: 10 every 1s;
        rate limit bar: 30 every 10ms;
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
// Doc.
cog PlainCog
{
    outputs
    {
        foo: Tappy<HelloMsg>;
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "rate_limit_test"), importer=fs_importer)
    rate_limit_cog = module.inner_scope.lookup("RateLimitCog")
    assert isinstance(rate_limit_cog, cog.Cog)
    plain_cog = module.inner_scope.lookup("PlainCog")
    assert isinstance(plain_cog, cog.Cog)
    plain_cog.resolve()
    assert "foo" in plain_cog.outputs
    assert len(plain_cog.rate_limits) == 0
    assert "foo" in rate_limit_cog.outputs
    assert "foo" in rate_limit_cog.rate_limits
    assert "bar" in rate_limit_cog.outputs
    assert "bar" in rate_limit_cog.rate_limits
    foo_limit = rate_limit_cog.rate_limits["foo"].get_resolved()
    assert foo_limit.limit == 10
    assert foo_limit.output.name == "foo"
    assert foo_limit.period_s == 1
    bar_limit = rate_limit_cog.rate_limits["bar"].get_resolved()
    assert bar_limit.limit == 30
    assert bar_limit.output.name == "bar"
    assert bar_limit.period_s == 1e-2


def test_invalid_rate_limit(fs_importer: FilesystemImporter) -> None:
    """Test rate limit errors."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog RateLimitCog
{
    outputs
    {
        foo: Tappy<HelloMsg>;
    }
    execution
    {
        rate limit bar: 10 every 1s;
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    with pytest.raises(
        ValueError,
        match=re.escape("Undefined identifier bar"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "rate_limit_invalid_output"), importer=fs_importer)

    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog RateLimitCog
{
    outputs
    {
        foo: Tappy<HelloMsg>;
    }
    execution
    {
        rate limit foo: 10 every 1bit;
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    with pytest.raises(
        TypeError,
        match=re.escape("Rate limit period must have time units, but got bit"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "rate_limit_invalid_unit"), importer=fs_importer)

    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog RateLimitCog
{
    outputs
    {
        foo: Tappy<HelloMsg>;
    }
    execution
    {
        rate limit foo: 10 every "foo";
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    with pytest.raises(
        TypeError,
        match=re.escape("Expected time literal for rate limit period, but got literal"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "rate_limit_bad_period_type"), importer=fs_importer)

    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog RateLimitCog
{
    outputs
    {
        foo: Tappy<HelloMsg>;
    }
    execution
    {
        rate limit foo: 10 every 1s;
        rate limit foo: 20 every 1s;
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    with pytest.raises(
        ValueError,
        match=re.escape("Outputs can only have one rate limit, but got multiple for foo"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "rate_limit_too_many"), importer=fs_importer)

    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog RateLimitCog
{
    outputs
    {
        foo: Tappy<HelloMsg>;
    }
    execution
    {
        rate limit foo: 0 every 1s;
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    with pytest.raises(
        ValueError,
        match=re.escape("Rate limit must be positive, but got 0"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "rate_limit_zero"), importer=fs_importer)


def test_metrics_options(fs_importer: FilesystemImporter) -> None:
    """Test syntax for input copying."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog MetricsCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>
        {
            copy_inputs: true;
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
    metrics_options
    {
        enabled: true;
        batch_size: 42;
    }
}
// Doc.
cog DisabledMetricsCog
{
    inputs
    {
        message_in: Tappy<HelloMsg>
        {
            copy_inputs: true;
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
    metrics_options
    {
        enabled: false;
    }
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "metrics_options_test"), importer=fs_importer)
    metrics_cog = module.inner_scope.lookup("MetricsCog")
    assert isinstance(metrics_cog, cog.Cog)
    assert metrics_cog.metrics_options.metrics_enabled
    assert metrics_cog.metrics_options.batch_size == 42
    disabled_metrics_cog = module.inner_scope.lookup("DisabledMetricsCog")
    assert isinstance(disabled_metrics_cog, cog.Cog)
    assert not disabled_metrics_cog.metrics_options.metrics_enabled


def test_cog_metrics_policy_compiles(fs_importer: FilesystemImporter) -> None:
    """Test that CogEventMetricsPolicy and CogTelemetryMetricsPolicy can be applied to a cog."""
    source = """
use std::cog_metrics_policy::{CogEventMetricsPolicy, CogTelemetryMetricsPolicy};

// Doc.
cog MyCog
{
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

// Telemetry metrics policy for MyCog
policy CogTelemetryMetricsPolicy for MyCog
{
}

// Event metrics policy for MyCog
policy CogEventMetricsPolicy for MyCog
{
}
"""
    compiler.compile_source_text(source, ModuleID(CLK_REPO, "cog_metrics_policy_test"), importer=fs_importer)


_ALIGNED_INPUTS_PREAMBLE = """\
#![generate(cpp, cpp_cog)]
#![cpp(namespace=clockwork::aligned_input_test)]
use clockwork::dsl::tests::support::clk_hellomsg::{HelloMsg};

// Sensor schema
schema SensorData {
    uuid: aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa;
    fields {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}

// Pose schema
schema PoseData {
    uuid: bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb;
    fields {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}

// First aligner
aligner SensorAligner {
    inputs {
        // Sensor data
        sensor: Tappy<SensorData>
        {
            max_msgs: 10;
            arbitrary_selection: true;
        }
    }
}

// Second aligner
aligner PoseAligner {
    inputs {
        // Pose data
        pose: Tappy<PoseData>
        {
            max_msgs: 10;
            arbitrary_selection: true;
        }
    }
}
"""


class TestAlignedInputs:
    """Tests for aligned_inputs block on cogs."""

    def test_aligned_inputs_comprehensive(self, fs_importer: FilesystemImporter) -> None:
        """Test aligned inputs: parsing, doc, scope, exec conditions, mixed with regular inputs, multiple aligners."""
        source = (
            _ALIGNED_INPUTS_PREAMBLE
            + """
// A consumer cog with regular inputs, multiple aligned inputs, and exec conditions
cog ConsumerCog
{
    inputs
    {
        raw_data: Tappy<HelloMsg>;
    }
    aligned_inputs
    {
        // Aligned sensor data
        sensor_aligned: SensorAligner;
        // Aligned pose data
        pose_aligned: PoseAligner;
    }
    execution
    {
        condition new_sensor: new_message(sensor_aligned);
        execute when: new_sensor;
    }
}
"""
        )
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_aligned_comprehensive"), fs_importer)
        cog_ir = module.inner_scope.lookup("ConsumerCog")
        assert isinstance(cog_ir, cog.Cog)

        assert len(cog_ir.inputs) == 1
        assert "raw_data" in cog_ir.inputs

        assert len(cog_ir.aligned_inputs) == 2
        assert "sensor_aligned" in cog_ir.aligned_inputs
        assert "pose_aligned" in cog_ir.aligned_inputs

        sensor_input = cog_ir.aligned_inputs["sensor_aligned"]
        assert isinstance(sensor_input, CogAlignedInputDef)
        assert isinstance(sensor_input.aligned_type, aligner.Aligner)
        assert sensor_input.aligned_type.name == "SensorAligner"

        pose_input = cog_ir.aligned_inputs["pose_aligned"]
        assert isinstance(pose_input.aligned_type, aligner.Aligner)
        assert pose_input.aligned_type.name == "PoseAligner"

        assert sensor_input.doc is not None
        assert "sensor" in sensor_input.doc.value.lower()

        assert isinstance(cog_ir.inner_scope.lookup("sensor_aligned", recursive=False), CogAlignedInputDef)
        assert isinstance(cog_ir.inner_scope.lookup("pose_aligned", recursive=False), CogAlignedInputDef)

        # Verify expanded class-level InputDefs are created for each upstream aligner input
        assert len(cog_ir.expanded_aligned_input_defs) == 2
        assert "sensor_aligned.sensor" in cog_ir.expanded_aligned_input_defs
        assert "pose_aligned.pose" in cog_ir.expanded_aligned_input_defs

    def test_init_cog_rejection(self, fs_importer: FilesystemImporter) -> None:
        """Test init cog with aligned_inputs block raises error."""
        source = (
            _ALIGNED_INPUTS_PREAMBLE
            + """
// An init cog with aligned inputs (should fail)
cog BadInitCog
{
    aligned_inputs
    {
        // Aligned data
        aligned: SensorAligner;
    }
    execution
    {
        execute when: init;
    }
}
"""
        )
        with pytest.raises(ValueError, match="may not have message inputs"):
            compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_init_aligned"), fs_importer)

    def test_non_aligner_type_rejected(self, fs_importer: FilesystemImporter) -> None:
        """Test aligned_inputs with a non-aligner type raises error."""
        source = (
            _ALIGNED_INPUTS_PREAMBLE
            + """
// A cog using a schema in aligned_inputs (should fail)
cog BadCog
{
    aligned_inputs
    {
        // Not an aligner
        wrong: SensorData;
    }
    execution
    {
        condition c: new_message(wrong);
        execute when: c;
    }
}
"""
        )
        with pytest.raises(TypeError, match="aligned_inputs type must be an aligner"):
            compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_non_aligner_type"), fs_importer)


class TestAlignedInputUpstreamOverrides:
    """Tests for per-upstream consumer-view overrides on ``aligned_inputs`` blocks."""

    def _consumer_input(
        self, source: str, module_id: str, cog_name: str, input_name: str, fs_importer: FilesystemImporter
    ) -> cog.InputDef:
        """Compile and return the expanded consumer-side InputDef for ``input_name``."""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, module_id), fs_importer)
        cog_ir = module.inner_scope.lookup(cog_name)
        assert isinstance(cog_ir, cog.Cog)
        return cog_ir.expanded_aligned_input_defs[input_name]

    def test_consumer_max_msgs_auto_sized(self, fs_importer: FilesystemImporter) -> None:
        """Default consumer ``max_msgs`` is auto-sized to ``max(n+1, ceil(1.2*n))``."""
        source = (
            _ALIGNED_INPUTS_PREAMBLE
            + """
// Consumer without overrides — expects auto-sized upstream view
cog AutoSizedConsumer
{
    aligned_inputs
    {
        // No overrides — consumer view is auto-sized from the aligner's max_msgs
        sensor_aligned: SensorAligner;
    }
    execution
    {
        condition s: new_message(sensor_aligned);
        execute when: s;
    }
}
"""
        )
        # Aligner max_msgs = 10 -> consumer default = max(11, ceil(12)) = 12.
        upstream = self._consumer_input(
            source, "test_auto_sized", "AutoSizedConsumer", "sensor_aligned.sensor", fs_importer
        )
        assert upstream.view_params.max_msgs == 12

    def test_upstream_override_sets_max_msgs(self, fs_importer: FilesystemImporter) -> None:
        """An explicit ``max_msgs`` override wins over the auto-sized default."""
        source = (
            _ALIGNED_INPUTS_PREAMBLE
            + """
// Consumer with explicit per-upstream max_msgs override
cog OverrideConsumer
{
    aligned_inputs
    {
        // Override upstream max_msgs to 300
        sensor_aligned: SensorAligner
        {
            max_msgs: 4;
            sensor
            {
                max_msgs: 300;
            }
        }
    }
    execution
    {
        condition s: new_message(sensor_aligned);
        execute when: s;
    }
}
"""
        )
        upstream = self._consumer_input(
            source, "test_override_max", "OverrideConsumer", "sensor_aligned.sensor", fs_importer
        )
        assert upstream.view_params.max_msgs == 300

    def test_upstream_override_below_aligner_rejected(self, fs_importer: FilesystemImporter) -> None:
        """Consumer ``max_msgs`` below the aligner's value is rejected."""
        source = (
            _ALIGNED_INPUTS_PREAMBLE
            + """
// Consumer requesting fewer messages than the aligner keeps
cog TooSmallConsumer
{
    aligned_inputs
    {
        // Aligner has max_msgs=10; requesting 3 is invalid
        sensor_aligned: SensorAligner
        {
            sensor
            {
                max_msgs: 3;
            }
        }
    }
    execution
    {
        condition s: new_message(sensor_aligned);
        execute when: s;
    }
}
"""
        )
        with pytest.raises(ValueError, match="less than the aligner's max_msgs=10"):
            compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_override_too_small"), fs_importer)

    def test_unknown_upstream_rejected(self, fs_importer: FilesystemImporter) -> None:
        """An override for an unknown upstream name is rejected at IR construction."""
        source = (
            _ALIGNED_INPUTS_PREAMBLE
            + """
// Consumer referencing an upstream that does not exist on the aligner
cog BadUpstreamConsumer
{
    aligned_inputs
    {
        // SensorAligner only has `sensor`; `nonexistent` is invalid
        sensor_aligned: SensorAligner
        {
            nonexistent
            {
                max_msgs: 20;
            }
        }
    }
    execution
    {
        condition s: new_message(sensor_aligned);
        execute when: s;
    }
}
"""
        )
        with pytest.raises(ValueError, match="unknown upstream 'nonexistent'"):
            compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_unknown_upstream"), fs_importer)

    def test_duplicate_upstream_override_rejected(self, fs_importer: FilesystemImporter) -> None:
        """Two override blocks for the same upstream are rejected."""
        source = (
            _ALIGNED_INPUTS_PREAMBLE
            + """
// Consumer with two override blocks for the same upstream
cog DupConsumer
{
    aligned_inputs
    {
        // Duplicate `sensor` overrides
        sensor_aligned: SensorAligner
        {
            sensor
            {
                max_msgs: 20;
            }
            sensor
            {
                max_msgs: 30;
            }
        }
    }
    execution
    {
        condition s: new_message(sensor_aligned);
        execute when: s;
    }
}
"""
        )
        with pytest.raises(ValueError, match="specified more than once"):
            compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_duplicate_upstream"), fs_importer)

    def test_non_max_msgs_field_inherits_from_aligner(self, fs_importer: FilesystemImporter) -> None:
        """Fields the user does not override on the upstream inherit from the aligner view."""
        # The aligner view enables copy_inputs; consumer should inherit it.
        source = """\
#![generate(cpp, cpp_cog)]
#![cpp(namespace=clockwork::aligned_input_inherit_test)]
use clockwork::dsl::tests::support::clk_hellomsg::{HelloMsg};

// Sensor schema
schema SensorData
{
    uuid: aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa;
    fields
    {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}

// Aligner that sets copy_inputs on its upstream view
aligner SensorAligner
{
    inputs
    {
        // Sensor data with copy_inputs
        sensor: Tappy<SensorData>
        {
            max_msgs: 10;
            copy_inputs: true;
            arbitrary_selection: true;
        }
    }
}

// Consumer that only overrides max_msgs
cog InheritingConsumer
{
    aligned_inputs
    {
        // Only override max_msgs; copy_inputs should inherit from the aligner
        sensor_aligned: SensorAligner
        {
            sensor
            {
                max_msgs: 50;
            }
        }
    }
    execution
    {
        condition s: new_message(sensor_aligned);
        execute when: s;
    }
}
"""
        upstream = self._consumer_input(
            source, "test_inherit_fields", "InheritingConsumer", "sensor_aligned.sensor", fs_importer
        )
        assert upstream.view_params.max_msgs == 50
        assert upstream.view_params.copy_inputs is True


def test_invalid_output_option(fs_importer: FilesystemImporter) -> None:
    """Test rate limit errors."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog UnknownOutputOption
{
    outputs
    {
        foo: Tappy<HelloMsg>
        {
            not_real: true;
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    with pytest.raises(
        NotImplementedError,
        match=re.escape("Unsupported cog output parameter 'not_real'"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "invalid_output_option"), importer=fs_importer)


def test_missing_python_dial(fs_importer: FilesystemImporter) -> None:
    """Test missing python options."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog MissingPythonDial
{
    outputs
    {
        foo: Tappy<HelloMsg>;
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
    python
    {
        impl: "foo";
    }
}
"""
    with pytest.raises(
        ValueError,
        match=re.escape("Missing required python option 'dial'"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "missing_python_dial"), importer=fs_importer)


def test_missing_python_impl(fs_importer: FilesystemImporter) -> None:
    """Test missing python options."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog MissingPythonImpl
{
    outputs
    {
        foo: Tappy<HelloMsg>;
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
    python
    {
        dial: "foo";
    }
}
"""
    with pytest.raises(
        ValueError,
        match=re.escape("Missing required python option 'impl'"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "missing_python_impl"), importer=fs_importer)


def test_missing_sim_duration(fs_importer: FilesystemImporter) -> None:
    """Test missing python options."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog MissingSimDuration
{
    outputs
    {
        foo: Tappy<HelloMsg>;
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
    simulation_options
    {
    }
}
"""
    with pytest.raises(
        ValueError,
        match=re.escape("Missing required simluation option 'execution_duration'"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "missing_sim_duration"), importer=fs_importer)


def test_unexpcted_resource_type(fs_importer: FilesystemImporter) -> None:
    """Test missing python options."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog UnexpectedResourceType
{
    resources
    {
        resource_a: wrong;
    }
    outputs
    {
        foo: Tappy<HelloMsg>;
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    with pytest.raises(
        NotImplementedError,
        match=re.escape("Cog resource 'resource_a' has unsupported type "),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "unexpected_resource_type"), importer=fs_importer)


def test_missing_exec_when(fs_importer: FilesystemImporter) -> None:
    """Test missing python options."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog MissingExecuteWhen
{
    outputs
    {
        foo: Tappy<HelloMsg>;
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
    }
}
"""
    with pytest.raises(
        ValueError,
        match=re.escape("Cog is missing required 'execute when' statement"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "missing_exec_when"), importer=fs_importer)


def test_multiple_exec_when(fs_importer: FilesystemImporter) -> None:
    """Test missing python options."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
// Doc.
cog MissingExecuteWhen
{
    outputs
    {
        foo: Tappy<HelloMsg>;
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
        execute when: periodic;
    }
}
"""
    with pytest.raises(
        ValueError,
        match=re.escape("Cogs must have exactly one 'execute when' statement"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "multiple_exec_when"), importer=fs_importer)


def test_cog_conditional_statements(fs_importer: FilesystemImporter) -> None:
    """Test conditional statements in cogs."""
    source = """
use clockwork::dsl::tests::support::hellomsg::{HelloMsg};
use std::traits;

// Doc.
cog ConditionalCog
{
    parameters
    {
        mode: String;
    }
    resources
    {
        if mode == "useful" then
        {
            memres: persistent;
        }
        else
        {
        }
    }
    configs
    {
        if mode == "useful" then
        {
            hello_config: Tappy<HelloMsg>;
        }
        else
        {
        }
    }
    states
    {
        if mode == "useful" then
        {
            hello_state: Tappy<HelloMsg>;
        }
        else
        {
        }
    }
    inputs
    {
        if mode == "useful" then
        {
            hello_in: Tappy<HelloMsg>;
        }
        else
        {
        }
    }
    outputs
    {
        if mode == "useful" then
        {
            hello_out: Tappy<HelloMsg>;
        }
        else
        {
        }
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

box TheBox
{
    new useful_cog: ConditionalCog<mode="useful">;
    new useless_cog: ConditionalCog<mode="useless">;
}

cpp_target cog_conditional_statements_clk_cc
{
    options
    {
        namespace clockwork::testing;
    }
    instantiate ConditionalCog<mode="useful">;
    instantiate ConditionalCog<mode="useless">;
}
"""
    module = compiler.compile_source_text(
        source, ModuleID(CLK_REPO, "cog_conditional_statements"), importer=fs_importer
    )
    cog_ir = module.inner_scope.lookup("ConditionalCog")
    assert isinstance(cog_ir, cog.Cog)
    # All of ConditionalCog's components contain statements that reference
    # parameters. So, they shouldn't exist as useable entities until
    # instantiation time.
    assert len(cog_ir.resources) == 0
    assert len(cog_ir.configs) == 0
    assert len(cog_ir.states) == 0
    assert len(cog_ir.inputs) == 0
    assert len(cog_ir.outputs) == 0
    assert len(cog_ir.guarded_components) == 5

    box_template = module.inner_scope.lookup("TheBox")
    assert isinstance(box_template, box.BoxTemplate)
    the_box = box_template.make_instance(
        cst_node=None, module=module, scope=module.inner_scope, name="thebox", doc=None
    )
    resolved_box = the_box.get_resolved()
    assert len(resolved_box.instances) == 2

    useful_cog = resolved_box.instances[0]
    assert isinstance(useful_cog, cog.CogInstance)
    assert isinstance(useful_cog.cog_class, cog.InstantiatedCog)
    assert "memres" in useful_cog.cog_class.resources
    assert "hello_config" in useful_cog.cog_class.configs
    assert "hello_state" in useful_cog.cog_class.states
    assert "hello_in" in useful_cog.cog_class.inputs
    assert "hello_out" in useful_cog.cog_class.outputs

    useless_cog = resolved_box.instances[1]
    assert isinstance(useless_cog, cog.CogInstance)
    assert isinstance(useless_cog.cog_class, cog.InstantiatedCog)
    assert "memres" not in useless_cog.cog_class.resources
    assert "hello_config" not in useless_cog.cog_class.configs
    assert "hello_state" not in useless_cog.cog_class.states
    assert "hello_in" not in useless_cog.cog_class.inputs
    assert "hello_out" not in useless_cog.cog_class.outputs
