# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for the Clockwork formatter."""

import io
from pathlib import Path

from clockwork.dsl import fmt


def test_format_source_normalizes_schema_layout() -> None:
    source = """// Example schema
schema Example{uuid: 12345678-1234-1234-1234-123456789abc;fields{// Value
#0 value:Int32;// Other value
#1 other_value:FixedArray<Byte,4>;}}
"""

    formatted = fmt.format_source(source)

    assert (
        formatted
        == """// Example schema
schema Example
{
  uuid: 12345678-1234-1234-1234-123456789abc;
  fields
  {
    // Value
    #0 value: Int32;
    // Other value
    #1 other_value: FixedArray<Byte, 4>;
  }
}
"""
    )


def test_format_source_is_idempotent() -> None:
    source = """#![generate(cpp, proto)]
#![cpp(namespace=clockwork::demo)]

// Example schema
schema Example
{
  uuid: 12345678-1234-1234-1234-123456789abc;
  fields
  {
    // Value
    #0 value: Int32;
  }
}
"""

    assert fmt.format_source(fmt.format_source(source)) == fmt.format_source(source)


def test_format_inputs_rewrites_files_by_default(tmp_path: Path) -> None:
    source_file = tmp_path / "example.clk"
    source_file.write_text(
        """// Example schema
schema Example{fields{// Value
#0 value:Int32;}}
""",
        encoding="utf-8",
    )

    result = fmt.format_inputs(
        (source_file,),
        check=False,
        options=fmt.FormatterOptions(),
        stdin=io.StringIO(),
        stdout=io.StringIO(),
        stderr=io.StringIO(),
    )

    assert result == []
    assert (
        source_file.read_text(encoding="utf-8")
        == """// Example schema
schema Example
{
  fields
  {
    // Value
    #0 value: Int32;
  }
}
"""
    )


def test_format_source_preserves_single_blank_line_between_fields() -> None:
    source = """// Example schema
schema Example
{
  fields
  {
    // First value
    #0 first_value: Int32;

    // Second value
    #1 second_value: Int32;
  }
}
"""

    formatted = fmt.format_source(source)

    assert (
        formatted
        == """// Example schema
schema Example
{
  fields
  {
    // First value
    #0 first_value: Int32;

    // Second value
    #1 second_value: Int32;
  }
}
"""
    )


def test_format_source_collapses_multiple_blank_lines_between_fields() -> None:
    source = """// Example schema
schema Example
{
  fields
  {
    // First value
    #0 first_value: Int32;


    // Second value
    #1 second_value: Int32;
  }
}
"""

    formatted = fmt.format_source(source)

    assert (
        formatted
        == """// Example schema
schema Example
{
  fields
  {
    // First value
    #0 first_value: Int32;

    // Second value
    #1 second_value: Int32;
  }
}
"""
    )


def test_format_source_preserves_single_blank_line_between_entities() -> None:
    source = """// First schema
schema First
{
  fields
  {
    // First value
    #0 first_value: Int32;
  }
}

// Second schema
schema Second
{
  fields
  {
    // Second value
    #0 second_value: Int32;
  }
}
"""

    formatted = fmt.format_source(source)

    assert "\n}\n\n// Second schema\n" in formatted


def test_format_source_preserves_single_blank_line_before_reformatted_line() -> None:
    source = """box Example
{
  new first: State(representation=Tachyon<Example>);

  new second: State(representation=Tachyon<Example>);
}
"""

    formatted = fmt.format_source(source)

    assert "  new first: State(representation=Tachyon<Example>);\n\n  new second: State(" in formatted


def test_format_source_keeps_documentation_attached_to_following_declaration() -> None:
    source = """box Example
{
  // First state
  new first: State(representation=Tachyon<Example>);

  // Second state
  // Continued documentation
  new second: State(representation=Tachyon<Example>);
}
"""

    formatted = fmt.format_source(source)

    assert "\n  // Second state\n  // Continued documentation\n  new second: State(" in formatted


def test_format_source_preserves_enum_value_details() -> None:
    source = """// Example enum
enum Example
{
  values
  {
    // Default value
    #0 unspecified default { underlying_value: 0; }
    // Sparse value
    #1 sparse { underlying_value: 100; }
  }
}
"""

    formatted = fmt.format_source(source)

    assert (
        formatted
        == """// Example enum
enum Example
{
  values
  {
    // Default value
    #0 unspecified default { underlying_value: 0; }
    // Sparse value
    #1 sparse { underlying_value: 100; }
  }
}
"""
    )


def test_format_source_preserves_use_blocks_on_one_line() -> None:
    source = """use autonomy::localization::se3::{SE3};

// Example schema
schema Example
{
  fields
  {
    // Value
    #0 value: SE3;
  }
}
"""

    formatted = fmt.format_source(source)

    assert formatted.startswith("use autonomy::localization::se3::{SE3};\n")


def test_format_source_preserves_use_block_alias_spacing() -> None:
    source = """use @clockwork::clockwork::diagnostics::report::{Report as DiagnosticsReport};

// Example schema
schema Example
{
  fields
  {
    // Value
    #0 value: DiagnosticsReport;
  }
}
"""

    formatted = fmt.format_source(source)

    assert formatted.startswith("use @clockwork::clockwork::diagnostics::report::{Report as DiagnosticsReport};\n")


def test_format_source_wraps_long_use_blocks() -> None:
    use_path = "autonomy::calibration::odometry::radar_inertial::clockwork::config::radar_data_preprocessing_config"
    source = f"""use {use_path}::{{RadarDataPreprocessingConfig,
WheelSpeedConverterParams}};

// Example schema
schema Example
{{
  fields
  {{
    // Value
    #0 value: RadarDataPreprocessingConfig;
  }}
}}
"""

    formatted = fmt.format_source(source)

    assert formatted.startswith(
        f"""use {use_path}::{{
  RadarDataPreprocessingConfig,
  WheelSpeedConverterParams,
}};
"""
    )


def test_format_source_preserves_proto_go_package_path() -> None:
    source = """#![generate(proto, go_proto)]
#![proto(package=stack.autonomy.mapping.data_model,
go_package=github.com/stack-av-llc/av/autonomy/mapping/data_model/enums)]

// Example enum
enum Example
{
  values
  {
    // Default value
    #0 unspecified default;
  }
}
"""

    formatted = fmt.format_source(source)

    assert (
        "#![proto(package=stack.autonomy.mapping.data_model,\n"
        "go_package=github.com/stack-av-llc/av/autonomy/mapping/data_model/enums)]\n"
    ) in formatted


def test_format_source_preserves_udp_socket_direction() -> None:
    source = """// Udp socket for outgoing messages
udp_socket ExampleSocket
{
  address: 10.0.1.111;
  port: 6004;
  direction: outgoing;
  message_type: Tachyon<Example>;
}
"""

    formatted = fmt.format_source(source)

    assert "  direction: outgoing;\n" in formatted


def test_format_source_preserves_nanobind_type_caster_spacing() -> None:
    source = """cpp_target my_enum_clk_cc
{
  options
  {
    namespace stack::my_enum::clk;
  }

  enum MyEnum;
  nanobind_type_caster for MyEnum from my_enum_clk_py;
}
"""

    formatted = fmt.format_source(source)

    assert "  nanobind_type_caster for MyEnum from my_enum_clk_py;\n" in formatted


def test_format_source_adds_rate_limit_spacing() -> None:
    source = """// Cog with a rate-limited output
cog Example
{
  outputs
  {
    output: Tappy<ExampleMessage>;
  }
  execution
  {
    rate limitoutput:14every1s;
    execute when: init;
  }
}
"""

    formatted = fmt.format_source(source)

    assert "    rate limit output: 14 every 1s;\n" in formatted


def test_format_source_preserves_dfl_impl_spacing() -> None:
    source = """impl And for Spec
{
  type Output = Spec;
}
"""

    formatted = fmt.format_source(source)

    assert formatted.startswith("impl And for Spec\n")


def test_format_source_preserves_aligner_dfl_calls() -> None:
    source = """// Test aligner
aligner TestAligner
{
  inputs
  {
    tick: Tappy<TestSensorMsg>;
    camera: Tappy<TestSensorMsg>;
    lidar: Tappy<TestSensorMsg>;
  }
  assume(is_strictly_increasing(tick.observation_time));
  require(|tick.observation_time - camera.observation_time| <= 100ms);
  minimize(|camera.observation_time - lidar.observation_time|);
}
"""

    formatted = fmt.format_source(source)

    assert (
        formatted
        == """// Test aligner
aligner TestAligner
{
  inputs
  {
    tick: Tappy<TestSensorMsg>;
    camera: Tappy<TestSensorMsg>;
    lidar: Tappy<TestSensorMsg>;
  }
  assume(is_strictly_increasing(tick.observation_time));
  require(|tick.observation_time - camera.observation_time| <= 100ms);
  minimize(|camera.observation_time - lidar.observation_time|);
}
"""
    )


def test_format_source_preserves_condition_expr_spacing_before_parens() -> None:
    source = """// Example cog
cog ExampleCog
{
  execution
  {
    condition ready: any_message(input);
    condition changed: new_message(input);
    condition periodic: time_since_last_exec(100ms);
    execute when: ready and (changed or periodic);
  }
}
"""

    formatted = fmt.format_source(source)

    assert "    execute when: ready and (changed or periodic);\n" in formatted


def test_format_source_wraps_long_execute_when_condition() -> None:
    source = """// Example cog
cog ExampleCog
{
  execution
  {
    condition new_delta_pose: new_message(delta_pose);
    condition any_trailer_status: any_message(trailer_status);
    condition any_steering_f0: any_message(steering_f0);
    condition any_vehicle_interface_status: any_message(vehicle_interface_status);
    condition any_etc2: any_message(etc2);
    condition any_ebc2_bsc: any_message(ebc2_bsc);
    condition any_lcmc: any_message(lcmc);
    condition any_apex_user_settings: any_message(apex_user_settings);
    execute when: new_delta_pose and any_trailer_status and any_steering_f0 and any_vehicle_interface_status and any_etc2 and any_ebc2_bsc and any_lcmc and any_apex_user_settings;
  }
}
"""

    formatted = fmt.format_source(source, options=fmt.FormatterOptions(max_width=120))

    assert (
        "    execute when: new_delta_pose\n"
        "      and any_trailer_status\n"
        "      and any_steering_f0\n"
        "      and any_vehicle_interface_status\n"
        "      and any_etc2\n"
        "      and any_ebc2_bsc\n"
        "      and any_lcmc\n"
        "      and any_apex_user_settings;\n"
    ) in formatted
    assert all(len(line) <= 120 for line in formatted.splitlines())


def test_format_source_omits_spaces_around_named_arg_equals() -> None:
    source = """// Example cog
cog ExampleCog
{
  execution
  {
    condition ready: new_message(input, max=1);
    execute when: ready;
  }
}
"""

    formatted = fmt.format_source(source)

    assert "    condition ready: new_message(input, max=1);\n" in formatted


def test_format_source_omits_spaces_around_type_arg_equals() -> None:
    source = """// Example schema
schema Example
{
  fields
  {
    // Value
    #2 value: VarArray<UInt64, max_size=3>;
  }
}
"""

    formatted = fmt.format_source(source)

    assert "    #2 value: VarArray<UInt64, max_size=3>;\n" in formatted


def test_format_source_trailing_comma_forces_multiline_type_args_and_lists() -> None:
    source = """// Example schema
schema Example
{
  fields
  {
    // Values
    #0 values: FixedArray<Int32, 2,> = [1, 2,];
  }
}
"""

    formatted = fmt.format_source(source)

    assert "    #0 values: FixedArray<\n" in formatted
    assert "      Int32,\n" in formatted
    assert "      2,\n" in formatted
    assert "    > = [\n" in formatted
    assert "      1,\n" in formatted
    assert "      2,\n" in formatted
    assert "    ];\n" in formatted


def test_format_source_wraps_history_lists_with_trailing_commas() -> None:
    source = """// Example schema
schema Example
{
  fields
  {
    // Value
    #0 value: Int32;
  }
  history
  {
    version: 16;
    legacy_became: [0->11, 1->12, 2->13, 3->14, 9->15, 4->16,];
    removed: [10, 15,];
  }
}
"""

    formatted = fmt.format_source(source, options=fmt.FormatterOptions(max_width=60))

    assert (
        formatted
        == """// Example schema
schema Example
{
  fields
  {
    // Value
    #0 value: Int32;
  }
  history
  {
    version: 16;
    legacy_became: [
      0 -> 11,
      1 -> 12,
      2 -> 13,
      3 -> 14,
      9 -> 15,
      4 -> 16,
    ];
    removed: [
      10,
      15,
    ];
  }
}
"""
    )


def test_format_source_trailing_separator_forces_multiline_use_block() -> None:
    source = """use autonomy::localization::se3::{SE3, Pose3,};

// Example schema
schema Example
{
  fields
  {
    // Value
    #0 value: SE3;
  }
}
"""

    formatted = fmt.format_source(source)

    assert formatted.startswith(
        """use autonomy::localization::se3::{
  SE3,
  Pose3,
};
"""
    )


def test_format_source_keeps_short_signal_aggregation_lists_on_one_line() -> None:
    source = """// Example signal
signal example_signal: UInt32
{
  pre_aggregation: ["min", "max"];
}

// Example cog
cog ExampleCog
{
  signals test_report_group
  {
    example_signal
    {
      post_aggregation: ["sum", "count"];
    }
  }

  execution
  {
    execute when: init;
  }
}
"""

    formatted = fmt.format_source(source)

    assert '  pre_aggregation: ["min", "max"];\n' in formatted
    assert '      post_aggregation: ["sum", "count"];\n' in formatted


def test_format_source_indents_wrapped_type_args() -> None:
    source = (
        "#![generate(cpp)]\n"
        "instantiate MapRangeConfigMps32WeightScheduledParameter32_2: "
        "MapRangeConfig<au::MetersPerSecondF, WeightScheduledParameter<Float32, 2>>;\n"
    )

    formatted = fmt.format_source(source, options=fmt.FormatterOptions(max_width=100))

    assert (
        formatted
        == """#![generate(cpp)]

instantiate MapRangeConfigMps32WeightScheduledParameter32_2: MapRangeConfig<
  au::MetersPerSecondF,
  WeightScheduledParameter<Float32, 2>,
>;
"""
    )


def test_format_source_wraps_new_statement_call_args() -> None:
    source = (
        "box Example\n"
        "{\n"
        "  new state: State("
        "representation=radar_velocity_dealiasing_cog::RadarVelocityDealiasingCogState, "
        "memory_resource=state_memory, "
        "init=init_cog.internal_state"
        ");\n"
        "}\n"
    )

    formatted = fmt.format_source(source, options=fmt.FormatterOptions(max_width=100))

    assert (
        formatted
        == """box Example
{
  new state: State(
    representation=radar_velocity_dealiasing_cog::RadarVelocityDealiasingCogState,
    memory_resource=state_memory,
    init=init_cog.internal_state,
  );
}
"""
    )


def test_format_source_wraps_serialized_data_file_call_args() -> None:
    source = (
        "box Example\n"
        "{\n"
        "  new lateral_acceleration_command_limiting: SerializedDataFile("
        "representation=Protobuf<LateralAccelerationCommandLimiting>, "
        'path="services/feature_flags/lateral_acceleration_command_limiting.textproto"'
        ");\n"
        "}\n"
    )

    formatted = fmt.format_source(source, options=fmt.FormatterOptions(max_width=120))

    assert (
        formatted
        == """box Example
{
  new lateral_acceleration_command_limiting: SerializedDataFile(
    representation=Protobuf<LateralAccelerationCommandLimiting>,
    path="services/feature_flags/lateral_acceleration_command_limiting.textproto",
  );
}
"""
    )


def test_format_source_preserves_spaces_around_assignment_equals() -> None:
    source = """impl Add for Celsius
{
  type Output = Celsius;
}
"""

    formatted = fmt.format_source(source)

    assert "  type Output = Celsius;\n" in formatted


def test_format_source_preserves_casing_element_spacing() -> None:
    source = """cpp_executable helloworld_exe
{
  casing
  {
    interface Tap<Tachyon<HelloMsg>>;
    extern hellocog::CxxState;
    representation HelloMsg;
    instantiate GenericState<HelloMsg>;
    box HelloBox;
  }
}
"""

    formatted = fmt.format_source(source)

    assert "    interface Tap<Tachyon<HelloMsg>>;\n" in formatted
    assert "    extern hellocog::CxxState;\n" in formatted
    assert "    representation HelloMsg;\n" in formatted
    assert "    instantiate GenericState<HelloMsg>;\n" in formatted
    assert "    box HelloBox;\n" in formatted
