# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for FmtString functionality."""

from decimal import Decimal
from pathlib import Path
from typing import Final

import pytest
from clockwork.dsl import compiler_context
from clockwork.dsl.ir import box, clkbuiltins, compiler, fmt_string, node, primitive, statement, system_target, typesys
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.primitive import StringValue


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


@pytest.fixture()
def mock_scope() -> node.Scope:
    """Create a mock scope for direct testing of helper functions."""
    return node.Scope(parent=None, uniq_path="test.scope", module_id_for_errors=None)


@pytest.fixture()
def mock_module(mock_scope: node.Scope) -> node.Module:
    return node.Module(
        doc=None,
        module_id=ModuleID("", "testmod"),
        inner_scope=mock_scope,
        terminals=None,
        cst_node=None,
        unresolved_imports=[],
        context=compiler_context.CompilerContext(),
        generates=None,
        inner_attrs=None,
    )


def make_string_literal(value: str, module: node.Module) -> primitive.StringLiteral:
    """Helper to create a StringLiteral for testing."""
    return primitive.StringLiteral(
        module=module,
        cst_node=None,
        value=value,
        type_info=clkbuiltins.STRING,
    )


def make_decimal_value(value: float) -> primitive.DecimalValue:
    """Helper to create a DecimalValue for testing."""
    return primitive.DecimalValue(
        type_info=clkbuiltins.INT64 if isinstance(value, int) else clkbuiltins.FLOAT32,
        value=Decimal(value),
    )


@pytest.mark.parametrize(
    ("format_str", "expected"),
    [
        ("hello/{world}", {"world"}),
        ("{a} and {b}", {"a", "b"}),
        ("{x}-{y}-{z}", {"x", "y", "z"}),
        ("no substitutions", set()),
        ("", set()),
        ("{same} {same}", {"same"}),
        ("{{literal}} {field}", {"field"}),
    ],
)
def test_extract_substitutions(mock_module: node.Module, format_str: str, expected: list[str]) -> None:
    """Test extraction of field names from format strings."""
    string_literal = make_string_literal(format_str, mock_module)
    result = fmt_string.extract_substitutions(string_literal)
    assert result == expected


def test_extract_substitutions_invalid_format(mock_module: node.Module) -> None:
    """Test that invalid format strings raise ValueError."""
    string_literal = make_string_literal("{unclosed", mock_module)
    with pytest.raises(ValueError, match="Invalid format string"):
        fmt_string.extract_substitutions(string_literal)


@pytest.mark.parametrize(
    ("value", "expected"),
    [
        (primitive.StringValue.make("hello"), "hello"),
        (primitive.StringValue.make(""), ""),
        (make_decimal_value(42), 42),
        (make_decimal_value(42.0), 42),
        (make_decimal_value(3.14), 3.14),
        (make_decimal_value(0), 0),
        (clkbuiltins.TRUE_VALUE, True),
        (clkbuiltins.FALSE_VALUE, False),
    ],
)
def test_value_to_python(value: typesys.Value, expected: str | float | bool) -> None:
    """Test conversion of Clockwork values to Python primitives."""
    result = fmt_string.value_to_python(value)
    assert result == expected
    assert type(result) is type(expected)


def test_value_to_python_non_primitive() -> None:
    """Test that non-primitive values return None."""

    class TestType(typesys.ObjectIdentityValue):
        pass

    result = fmt_string.value_to_python(TestType(type_info=clkbuiltins.TYPE_TYPE))
    assert result is None


@pytest.mark.parametrize(
    ("source", "expected"),
    [
        (
            """
some_string = "abc";
some_number = 42;
some_bool = false;
result = fmt!("Multiple types: {some_string} {some_number} {some_bool}");
""",
            "Multiple types: abc 42 False",
        ),
        (
            """
value = "test";
result = fmt!("Repeated values: {value} {value}");
""",
            "Repeated values: test test",
        ),
        (
            """
value = 3.14;
result = fmt!("Custom formatting: {value:07.04f}");
""",
            "Custom formatting: 03.1400",
        ),
        (
            """
result = fmt!("No substitutions");
""",
            "No substitutions",
        ),
        (
            """
world = "Earth";
result = fmt!("Escaped braces: {{hello}} {world}");
""",
            "Escaped braces: {hello} Earth",
        ),
    ],
)
def test_fmt_string_substitutions(fs_importer: FilesystemImporter, source: str, expected: str) -> None:
    """Test fmt strings with substitutions."""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "module"), fs_importer)
    result = module.inner_scope.lookup("result")
    assert result is not None
    assert isinstance(result, statement.ImmutableBinding)
    fmt_str = result.value
    assert isinstance(fmt_str, StringValue)
    assert fmt_str.value == expected


def test_fmt_string_missing_field_error(fs_importer: FilesystemImporter) -> None:
    """Test that referencing undefined field raises KeyError."""
    source: Final = r"""
result = fmt!("hello/{undefined}");
"""
    with pytest.raises(KeyError, match=r"\\\'undefined\\\' not found in scope"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "module"), fs_importer)


def test_evaluate_from_dict_missing_key(mock_module: node.Module) -> None:
    """Test that missing substitution key raises ValueError."""
    string_literal = make_string_literal("{missing}", mock_module)
    unevaluated = fmt_string.UnevaluatedFmtString(
        type_info=clkbuiltins.STRING,
        format_string_cst=string_literal,
    )

    with pytest.raises(ValueError, match="Error formatting string"):
        unevaluated.evaluate_from_dict({})


def test_evaluate_from_scope_missing_field(mock_scope: node.Scope, mock_module: node.Module) -> None:
    """Test that missing field in scope raises KeyError."""
    string_literal = make_string_literal("{undefined}", mock_module)
    unevaluated = fmt_string.UnevaluatedFmtString(
        type_info=clkbuiltins.STRING,
        format_string_cst=string_literal,
    )

    with pytest.raises(KeyError, match="Substitution field 'undefined' not found in scope"):
        unevaluated.evaluate_from_scope(mock_scope)


def test_fmt_string_factory_wrong_arg_count(fs_importer: FilesystemImporter) -> None:
    """Test that fmt with wrong number of arguments raises ValueError."""
    source: Final = r"""
result = fmt!("hello", "extra");
"""
    with pytest.raises(ValueError, match="fmt takes exactly one argument"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "module"), fs_importer)


def test_fmt_string_factory_non_string_arg(fs_importer: FilesystemImporter) -> None:
    """Test that fmt with non-string argument raises TypeError."""
    source: Final = r"""
result = fmt!(42);
"""
    with pytest.raises(TypeError, match="fmt argument must be a string"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "module"), fs_importer)


@pytest.mark.parametrize(
    ("source", "expected"),
    [
        (
            """
cpu_domain MyCpuHost1;
domain = MyCpuHost1;
result = fmt!("CPU name is {name(domain)}");
""",
            "CPU name is MyCpuHost1",
        ),
        (
            """
cpu_domain my_cpu_host1;
domain = my_cpu_host1;
result = fmt!("CPU name is {name(domain)}");
""",
            "CPU name is my_cpu_host1",
        ),
        (
            """
cpu_domain MyCpuHost1;
domain = MyCpuHost1;
result = fmt!("CPU camel case name is {camel_case_name(domain)}");
""",
            "CPU camel case name is MyCpuHost1",
        ),
        (
            """
cpu_domain my_cpu_host1;
domain = my_cpu_host1;
result = fmt!("CPU camel case name is {camel_case_name(domain)}");
""",
            "CPU camel case name is MyCpuHost1",
        ),
        (
            """
cpu_domain MyCpuHost1;
domain = MyCpuHost1;
result = fmt!("CPU snake case name is {snake_case_name(domain)}");
""",
            "CPU snake case name is my_cpu_host1",
        ),
        (
            """
cpu_domain my_cpu_host1;
domain = my_cpu_host1;
result = fmt!("CPU snake case name is {snake_case_name(domain)}");
""",
            "CPU snake case name is my_cpu_host1",
        ),
        (
            """
cpu_domain MyCpuHost1;
domain = MyCpuHost1;
result = fmt!("CPU short name is {short_name(domain)}");
""",
            "CPU short name is host1",
        ),
        (
            """
cpu_domain my_cpu_host1;
domain = my_cpu_host1;
result = fmt!("CPU short name is {short_name(domain)}");
""",
            "CPU short name is host1",
        ),
    ],
)
def test_fmt_string_name_substitution(fs_importer: FilesystemImporter, source: str, expected: str) -> None:
    """Test fmt strings with CPU name substitutions."""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "module"), fs_importer)
    result = module.inner_scope.lookup("result")
    assert result is not None
    assert isinstance(result, statement.ImmutableBinding)
    fmt_str = result.value
    assert isinstance(fmt_str, StringValue)
    assert fmt_str.value == expected


def test_fmt_string_system_target_substitution(fs_importer: FilesystemImporter) -> None:
    """Test fmt string with system target path strings and CPU name substitutions."""
    source = """
// Docs.
schema TestSchema
{
  fields
  {
    // Docs.
    #1 field: Int32;
  }
}
box box1
{
  new config: SerializedDataFile(representation=Tachyon<TestSchema>, path=fmt!("{system_file_path_base()}.{system_instance_name()}.config.tachyon"));
}
system_target target_sys
{
  box: box1;
}
"""
    module = compiler.compile_source_text(
        source, ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/ir/tests/fmt_string_test.clk")), fs_importer
    )
    target = module.inner_scope.lookup("target_sys")
    assert isinstance(target, system_target.UnresolvedSystemTarget)
    resolved_target = target.resolved
    assert isinstance(resolved_target, system_target.SystemTarget)
    target_box = resolved_target.box_instance
    assert isinstance(target_box, box.ResolvedBox)
    assert target_box.name == "target_sys"
    assert len(target_box.instances) == 1
    config = target_box.instances[0]
    assert isinstance(config, box.SerializedDataFileInstance)
    assert config.name == "config"
    assert str(config.file_path) == "clockwork/dsl/ir/tests/fmt_string_test.target_sys.config.tachyon"


@pytest.mark.parametrize(
    ("source", "expected"),
    [
        (
            """
value = "hello";
result = fmt!("{value ?? 'default'}");
""",
            "hello",
        ),
        (
            """
value = "world";
result = fmt!("prefix_{value ?? 'fallback'}_suffix");
""",
            "prefix_world_suffix",
        ),
    ],
)
def test_fmt_nullish_coalescing_present_value(fs_importer: FilesystemImporter, source: str, expected: str) -> None:
    """Fmt! ?? operator returns the field value when it is present in scope."""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "module"), fs_importer)
    result = module.inner_scope.lookup("result")
    assert isinstance(result, statement.ImmutableBinding)
    assert isinstance(result.value, StringValue)
    assert result.value.value == expected


def test_fmt_nullish_coalescing_absent_key_uses_default(fs_importer: FilesystemImporter) -> None:
    """Fmt! ?? operator returns the default when the field is absent from scope."""
    source = """
result = fmt!("{missing_var ?? 'fallback'}");
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "module"), fs_importer)
    result = module.inner_scope.lookup("result")
    assert isinstance(result, statement.ImmutableBinding)
    assert isinstance(result.value, StringValue)
    assert result.value.value == "fallback"


def test_fmt_nullish_uses_scoped_variables_when_no_quotes_present(fs_importer: FilesystemImporter) -> None:
    """Fmt! ?? operator uses scoped variable as default when no quotes are present."""
    source = """
default_val = "scoped_default";
result = fmt!("{missing_var ?? default_val}");
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "module"), fs_importer)
    result = module.inner_scope.lookup("result")
    assert isinstance(result, statement.ImmutableBinding)
    assert isinstance(result.value, StringValue)
    assert result.value.value == "scoped_default"


def test_fmt_nullish_handles_empty_string_as_falsy(fs_importer: FilesystemImporter) -> None:
    """Fmt! ?? operator treats unscoped value as falsy and uses default."""
    source = """
result = fmt!("{value ?? 'default_for_empty'}");
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "module"), fs_importer)
    result = module.inner_scope.lookup("result")
    assert isinstance(result, statement.ImmutableBinding)
    assert isinstance(result.value, StringValue)
    assert result.value.value == "default_for_empty"


def test_nullish_handled_empty_replacement(fs_importer: FilesystemImporter) -> None:
    """Fmt! ?? operator can replace missing value with empty string."""
    source = """
result = fmt!("{missing_var ?? ''}");
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "module"), fs_importer)
    result = module.inner_scope.lookup("result")
    assert isinstance(result, statement.ImmutableBinding)
    assert isinstance(result.value, StringValue)
    assert result.value.value == ""


def test_nullish_handled_mismatched_quotes(fs_importer: FilesystemImporter) -> None:
    """Fmt! ?? operator raises error when default value has mismatched quotes."""
    source = """
result = fmt!("{missing_var ?? 'unmatched}");
"""
    with pytest.raises(SyntaxError, match="mismatched single quotes"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "module"), fs_importer)

    source = """
non_missing_var = "present";
result = fmt!("{non_missing_var ?? unmatched'}");
"""
    with pytest.raises(SyntaxError, match="mismatched single quotes"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "module"), fs_importer)


def test_nullish_handles_optional(fs_importer: FilesystemImporter) -> None:
    """Fmt! ?? operator can handle optional values."""
    source = """
// test
schema TestSchema
{
  fields
  {
    // Docs.
    #1 field: Int32;
  }
}

// doc
box box1
{
    parameters
    {
        optional_param: Optional<String>;
        other_param: String;
    }
    new config: SerializedDataFile(representation=Tachyon<TestSchema>, path=fmt!("clockwork/path.{optional_param ?? 'default_for_none'}.tachyon"));
    new config2: SerializedDataFile(representation=Tachyon<TestSchema>, path=fmt!("clockwork/path.{optional_param ?? other_param}.tachyon"));
}

box outer
{
    new inner_box: box1<optional_param="provided", other_param="unused">;
    new inner_box2: box1<other_param="used">;
}

system_target target_sys
{
  box: outer;
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "module"), fs_importer)
    target = module.inner_scope.lookup("target_sys")
    assert isinstance(target, system_target.UnresolvedSystemTarget)
    resolved_target = target.resolved
    assert isinstance(resolved_target, system_target.SystemTarget)
    target_box = resolved_target.box_instance
    assert isinstance(target_box, box.ResolvedBox)
    assert target_box.name == "target_sys"
    assert len(target_box.instances) == 2
    inner_box = target_box.instances[0]
    inner_box2 = target_box.instances[1]

    assert inner_box.name == "inner_box"
    assert isinstance(inner_box, box.Box)
    assert len(inner_box.instances) == 2
    config = inner_box.instances[0]
    config2 = inner_box.instances[1]
    assert isinstance(config, box.SerializedDataFileInstance)
    assert str(config.file_path) == "clockwork/path.provided.tachyon"
    assert isinstance(config2, box.SerializedDataFileInstance)
    assert str(config2.file_path) == "clockwork/path.provided.tachyon"

    assert isinstance(inner_box2, box.Box)
    config3 = inner_box2.instances[0]
    config4 = inner_box2.instances[1]
    assert isinstance(config3, box.SerializedDataFileInstance)
    assert str(config3.file_path) == "clockwork/path.default_for_none.tachyon"
    assert isinstance(config4, box.SerializedDataFileInstance)
    assert str(config4.file_path) == "clockwork/path.used.tachyon"
