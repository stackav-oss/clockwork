# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Test the Cog IR module."""

import re
from decimal import Decimal
from pathlib import Path
from typing import Final
from unittest.mock import MagicMock

import pytest
from clockwork.dsl import compiler_context
from clockwork.dsl.ir import clkbuiltins, cog, compiler, expr, node, parse, primitive, typesys
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID

# These generated files must be imported on a separate line from the source file import above due to a pyright limitation:
# https://github.com/microsoft/pyright/issues/3630
from clockwork.dsl import cst  # isort: skip


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
    assert len(hellocog_ir.inputs) == 3
    latest = hellocog_ir.inputs["latest_hello"]
    multi_publisher_hello = hellocog_ir.inputs["multi_publisher_hello"]
    history = hellocog_ir.inputs["history_of_hellos"]

    assert latest.doc is None
    assert multi_publisher_hello.doc == node.Doc(
        module=hellocog_ir.module, cst_node=None, value="Multi-publisher input"
    )
    assert history.doc == node.Doc(module=hellocog_ir.module, cst_node=None, value="Make sure larger views work")


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
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
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
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
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
    with pytest.raises(TypeError, match="Type inference failed: ::UInt64 != ::String"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "skip_threshold_bad_type"), importer=fs_importer)

    source = """
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
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
    with pytest.raises(TypeError, match="Attempt to unify NumericType.SIGNED_INTEGER"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "negative_skip_threshold"), importer=fs_importer)


def test_copy_inputs(fs_importer: FilesystemImporter) -> None:
    """Test syntax for input copying."""
    source = """
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
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


def test_optional_inputs_and_outputs(fs_importer: FilesystemImporter) -> None:
    """Test syntax for input copying."""
    source = """
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
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
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
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
    with pytest.raises(TypeError, match="Type inference failed: ::Bool != ::String"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "copy_inputs_bad_type"), importer=fs_importer)


def test_safety_margin(fs_importer: FilesystemImporter) -> None:
    """Test safety margin syntax."""
    source = """
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
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
        condition new_msg: new_message(min=1, max=1, input=message_in);
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
        condition new_msg: new_message(min=1, max=1, input=message_in);
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


def test_invalid_safety_margin(fs_importer: FilesystemImporter) -> None:
    """Test safety margin errors."""
    source = """
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
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
    with pytest.raises(TypeError, match="Type inference failed: ::UInt64 != ::String"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "safety_margin_bad_type"), importer=fs_importer)

    source = """
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
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
        match="Inputs may only specify a safety margin when associated with a new_message condition that uses the 'max' parameter",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "safety_margin_no_max"), importer=fs_importer)


def test_rate_limit(fs_importer: FilesystemImporter) -> None:
    """Test rate limit syntax."""
    source = """
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
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
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
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
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
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
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
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
        match=re.escape("Expected time literal for rate limit period, but got <class 'clockwork.dsl.cst.Literal'>"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "rate_limit_bad_period_type"), importer=fs_importer)

    source = """
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
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


def test_metrics_options(fs_importer: FilesystemImporter) -> None:
    """Test syntax for input copying."""
    source = """
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
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
