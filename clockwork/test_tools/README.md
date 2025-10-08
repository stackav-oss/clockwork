# Clockwork System Runner

The Clockwork System Runner (CSR) is a testing utility designed for running deterministic Clockwork systems within unit and integration tests that are fully compatible with CI frameworks.
It provides multiple configuration approaches to accommodate different testing needs and complexity levels.

## Overview

The CSR provides two main APIs and four primary testing approaches:

### APIs Available

1. **ClockworkSystemRunner** - Core API for log-based and Test Cog-based configurations
2. **MessageInjectorSystemRunner** - Specialized API for programmatic message injection

### Testing Approaches

1. **Log-based configuration** - Replay recorded message logs for deterministic testing
2. **Test Cog-based configuration** - Use custom test driver Cogs for interactive testing
3. **Message Injector approach** - Programmatically inject messages via a clean API
4. **Hybrid approach** - Combine multiple methods for comprehensive test coverage

Choose the API and approach that best fits your testing requirements, system complexity, and maintainability needs.

## Log-based Configuration

The log-based configuration provides a deterministic testing approach by replaying previously recorded message logs.
This method is particularly useful for regression testing and reproducing specific scenarios.

### How It Works

1. **Input Processing**: The CSR reads a test log file containing timestamped messages
2. **Message Replay**: Messages are published to configured channels according to their original timestamps
3. **System Execution**: The Clockwork system processes the replayed messages deterministically
4. **Output Capture**: System outputs are written to a log file for validation
5. **Test Validation**: Tests examine the output log to verify expected behavior

### Benefits

- **Deterministic Behavior**: Same inputs always produce the same outputs
- **Regression Testing**: Easily reproduce and test against known scenarios
- **CI/CD Friendly**: No external dependencies or real-time constraints
- **Historical Data**: Test against real-world message patterns from production logs

### Use Cases

- Validating system behavior against recorded production scenarios
- Regression testing after code changes
- Performance benchmarking with consistent input patterns
- Debugging specific failure scenarios captured in logs

## Test Cog-based Configuration

The Test Cog-based configuration allows you to create custom test driver Cogs that actively participate in the Clockwork system during testing.
This approach provides maximum flexibility for complex testing scenarios that require dynamic interaction.

### How It Works

1. **Test Driver Cogs**: Custom Cogs designed specifically for testing purposes
2. **Message Generation**: Test Cogs publish messages to stimulate the system under test
3. **Response Validation**: Test Cogs receive and validate outputs from the system
4. **Interactive Testing**: Test logic can adapt based on system responses
5. **Comprehensive Coverage**: Test multiple interaction patterns and edge cases

### Benefits

- **Dynamic Interaction**: Test logic can react to system outputs in real-time
- **Complex Scenarios**: Handle sophisticated test cases with branching logic
- **Stateful Testing**: Maintain test state across multiple message exchanges
- **Full Integration**: Test Cogs participate as first-class citizens in the system

## Hybrid Approach

The hybrid approach combines multiple testing methods to leverage the strengths of each configuration type.
This flexibility allows you to create comprehensive test suites that address different aspects of system behavior.

### Common Hybrid Patterns

#### Pattern 1: Test Cog + Log Validation

- **Input**: Test Cogs publish messages to stimulate the system
- **Output**: Log files capture system responses for detailed analysis
- **Benefits**: Dynamic input generation with comprehensive output validation

#### Pattern 2: Log Replay + Test Cog Validation

- **Input**: Log files provide deterministic message replay
- **Output**: Test Cogs receive and validate system responses in real-time
- **Benefits**: Consistent inputs with dynamic validation logic

#### Pattern 3: Message Injector + Log Analysis

- **Input**: Message Injector API provides precise message control
- **Output**: Log files enable detailed post-execution analysis
- **Benefits**: Programmatic control with comprehensive result inspection

## ClockworkSystemRunner API

The `ClockworkSystemRunner` class is the core API for implementing log-based and Test Cog-based configurations.
It provides a flexible interface that can be configured for various testing scenarios, from simple log replay to complex multi-component system testing.

### Core API

The `ClockworkSystemRunner` class provides the following key methods:

- **`create(config)`**: Factory method to instantiate the runner with a configuration
- **`run()`**: Execute the simulation according to the configured approach

### Configuration Options

The `ClockworkSystemRunnerConfig` struct supports flexible configuration through optional parameters:

#### Required Configuration

- **`process_description_path`**: Path to the Clockwork system's process description file

#### Optional Configuration

- **`start_time`** and **`end_time`**: Simulation time bounds (required if no input log is provided)
- **`input_log_config`**: Configuration for log-based input (LogConfig with URI and config path)
- **`output_log_config`**: Configuration for capturing system outputs to logs

### Usage Patterns

#### Log-based Testing

For log replay scenarios, configure both input and output logs:

```cpp
  auto config = ClockworkSystemRunnerConfig{
    .process_description_path = process_description_path,
    .input_log_config = LogConfig(input_log_path, channel_publisher_config_path),
    .output_log_config = LogConfig(output_log_path, log_writer_config_path)};

  auto system_runner = ClockworkSystemRunner::create(config);
  REQUIRE(system_runner);
  REQUIRE(system_runner->run());
```

#### Test Cog-based Testing

For Test Cog-driven scenarios, specify time bounds without input logs:

```cpp
  auto config = ClockworkSystemRunnerConfig{
    .process_description_path = process_description_path,
    .start_time = start_time,
    .end_time = end_time,
    .output_log_config = LogConfig(output_log_path, log_writer_config_path)};

  auto system_runner = ClockworkSystemRunner::create(config);
  REQUIRE(system_runner);
  REQUIRE(system_runner->run());
```

### LogConfig Structure

The `LogConfig` helper struct simplifies log configuration:

```cpp
struct LogConfig
{
  LogConfig(std::string_view uri, std::string_view config_path);
  std::string log_uri;           // Path to the log file
  std::string log_config_path;   // Path to the system-generated log configuration file
};
```

The `log_config_path` parameter expects a path to a system-generated configuration file (e.g., `channel_publisher_config` for input logs or `log_writer_config` for output logs).
These files are automatically created by the Clockwork compiler when processing your Clockwork system definition.

### Error Handling

The `ClockworkSystemRunner` uses the `jewels::expected` pattern for error handling:

- **Creation Errors**: Issues with process description loading, configuration validation
- **Runtime Errors**: Problems during system execution, channel setup, or log operations
- **Error Types**: Enumerated in `ClockworkSystemRunnerError` for specific error identification

### When to Use ClockworkSystemRunner

Use the standard `ClockworkSystemRunner` when:

- **Log Replay**: You have existing log files to replay for testing
- **Test Cog Integration**: Your system includes custom test driver Cogs
- **Complex Configuration**: You need fine-grained control over logging and metrics
- **Hybrid Workflows**: You want to combine multiple testing approaches
- **Production Debugging**: You need to reproduce specific scenarios from production logs

## Message Injector Approach

The Message Injector System Runner provides a streamlined API for testing Clockwork systems by programmatically injecting messages without requiring the creation of dedicated test driver Cogs.
This approach offers several advantages over traditional test Cog-based configurations:

### Key Benefits

1. **Simplified Test Setup**: No need to create and maintain separate test driver Cogs for message injection
2. **Precise Timing Control**: Messages can be injected at specific timestamps during simulation
3. **Flexible Message Ordering**: Messages can be added out of chronological order and will be played back in the correct temporal sequence
4. **Direct API Access**: Clean, type-safe API for both message injection and result validation

### Core API

The `MessageInjectorSystemRunner` class provides the following key methods:

- **`create(config, memory_resource)`**: Factory method to instantiate the runner with configuration
- **`add_message(message, timestamp, channel_name)`**: Inject a message to be published at a specific time
- **`run()`**: Execute the simulation with all queued messages
- **`try_pop_message()`**: Retrieve output messages in FIFO order for validation

### Configuration

The `MessageInjectorSystemRunnerConfig` struct requires:

- **`process_description_path`**: Path to the Clockwork system's process description file
- **`start_time`** and **`end_time`**: Simulation time bounds
- **`channel_publisher_config`**: Path to system-generated configuration for publishing to channels
- **`log_writer_config`**: Path to system-generated configuration for capturing output messages

**Note**: The `channel_publisher_config` and `log_writer_config` files are automatically generated by the Clockwork compiler alongside your process description.
These are not manually created configuration files.

### Example Usage

```C++
  auto config = MessageInjectorSystemRunnerConfig{
    .process_description_path = process_description_path,
    .start_time = start_time,
    .end_time = end_time,
    .channel_publisher_config_ = channel_publisher_config_path,
    .log_writer_config_ = log_writer_config_path};

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  auto injector_system_runner = MessageInjectorSystemRunner::create(config, memory_resource);
  REQUIRE(injector_system_runner);

  // Create test messages
  Tappy<system_runner::AdditionMessage> addition_message1;
  addition_message1.set_addend_one(2);
  addition_message1.set_addend_two(3);

  Tappy<system_runner::AdditionMessage> addition_message2;
  addition_message2.set_addend_one(42);
  addition_message2.set_addend_two(467);
  const std::string channel_name = "AdditionChannelIn";

  SECTION("Add in order")
  {
    // Queue messages for injection at specific times
    injector_system_runner->add_message(addition_message1, start_time + std::chrono::milliseconds(1), channel_name);
    injector_system_runner->add_message(addition_message2, start_time + std::chrono::milliseconds(10), channel_name);

    // Run the simulation
    REQUIRE(injector_system_runner->run());

    // Validate output messages
    auto message1 = injector_system_runner->try_pop_message();
    REQUIRE(message1);
    auto maybe_sum_message1 = as_message<Tappy<system_runner::SumMessage>>(*message1);
    REQUIRE(maybe_sum_message1);
    auto sum_message1 = *maybe_sum_message1;
    REQUIRE(addition_message1.get_addend_one() + addition_message1.get_addend_two() == sum_message1->get_sum());

    auto message2 = injector_system_runner->try_pop_message();
    REQUIRE(message2);
    auto maybe_sum_message2 = as_message<Tappy<system_runner::SumMessage>>(*message2);
    REQUIRE(maybe_sum_message2);
    auto sum_message2 = *maybe_sum_message2;
    REQUIRE(addition_message2.get_addend_one() + addition_message2.get_addend_two() == sum_message2->get_sum());
  }
```

### Message Ordering and Timing

The Message Injector handles temporal ordering automatically.
Messages can be added in any order to the runner, but they will be published during simulation according to their specified timestamps:

```C++
  SECTION("Add out of order")
  {
    // Add messages in reverse chronological order
    injector_system_runner->add_message(addition_message2, start_time + std::chrono::milliseconds(10), channel_name);
    injector_system_runner->add_message(addition_message1, start_time + std::chrono::milliseconds(1), channel_name);

    // Messages will still be published in correct temporal order during simulation
    REQUIRE(injector_system_runner->run());

    // Output validation remains the same...
  }
```

### When to Use Message Injector

The Message Injector approach is ideal when:

- **Testing specific message sequences**: You need precise control over input timing and content
- **Isolated component testing**: Testing individual Cogs or small subsystems
- **Regression testing**: Reproducing specific scenarios with known inputs
- **Performance testing**: Measuring system response to controlled message patterns

Consider using Test Cog-based configuration instead when:

- **Complex interactive behavior**: Test logic needs to react dynamically to system outputs
- **Long-running simulations**: Tests require ongoing message generation based on system state
- **Integration testing**: Testing communication patterns between multiple Cogs

## API Comparison and Selection Guide

### ClockworkSystemRunner vs MessageInjectorSystemRunner

| Feature                      | ClockworkSystemRunner            | MessageInjectorSystemRunner    |
| ---------------------------- | -------------------------------- | ------------------------------ |
| **Primary Use Case**         | Log replay, Test Cog integration | Programmatic message injection |
| **Configuration Complexity** | High - Flexible but verbose      | Low - Simplified, focused API  |
| **Input Methods**            | Log files, Test Cogs             | Programmatic API calls         |
| **Timing Control**           | Log timestamps, Test Cog logic   | Precise programmatic timing    |

## Configuring Channels

Channel configuration is a critical aspect of setting up CSR tests, as it defines how messages flow between the test infrastructure and your Clockwork system.
The configuration files used by CSR are automatically generated by the Clockwork compiler based on your Clockwork system definition.

### System-Generated Configuration Files

When you define a Clockwork system with channels, the Clockwork compiler automatically generates several configuration files:

- **Channel Publisher Config**: Controls which channels receive messages during log replay or message injection
- **Log Writer Config**: Specifies which channels are captured in output logs for validation
- **Metrics Channel Metadata Config**: Defines metrics collection channels (optional)

These files follow naming patterns like:

- `<system_name>.<cpu_domain>_channel_publisher_config.tachyon`
- `<system_name>.<cpu_domain>_telemetry_logger_config.tachyon`
- `<system_name>.<cpu_domain>_metrics_channel_metadata_config.tachyon`

### Log-based Configuration

For log-based tests, channels are configured through system-generated files:

- **Channel Publisher Config**: Defines which channels the CSR should publish to when replaying log messages
- **Log Writer Config**: Specifies which channels should be captured in output logs for validation

### Message Injector Configuration

The Message Injector approach uses the same system-generated channel configuration files:

- **`channel_publisher_config_`**: Enables message injection into specific channels
- **`log_writer_config_`**: Captures system outputs for programmatic validation

## Configuring a Clockwork System Runner Test in Bazel

### Overview of Components

A complete CSR test setup involves:

1. **Clockwork System Definition** (`.clk` files) - Defines your system architecture
2. **Process Description** - Generated artifact describing system runtime configuration
3. **Executable Targets** - Compiled binaries for system execution
4. **Library Targets** - Linkable components for test integration
5. **Test Targets** - The actual test executables with proper dependencies

### Example: Testing an Addition System

This example demonstrates testing an `AdditionCog` that receives two numbers via `AdditionMessage`, computes their sum, and outputs the result via `SumMessage`.

#### Test Architecture

- **`AdditionCog`**: The component under test - performs addition operations
  - **Definition**: `tests/support/addition_cogs.clk`
  - **Implementation**: `tests/support/addition_cog_impl.cc`
- **`AdditionTesterCog`**: Test driver that generates test inputs and validates outputs
  - **Definition**: `tests/support/addition_tester_cog.clk`
  - **Implementation**: `tests/support/addition_tester_cog_impl.cc`
- **Channel Flow**: `AdditionTesterCog` → `AdditionChannel` → `AdditionCog` → `SumChannel` → `AdditionTesterCog`

These reference implementations demonstrate how to structure Cogs for testing purposes and can serve as templates for your own test Cogs.

### Clockwork System Definition

The system definition file (`tests/support/addition_test_system.clk`) declares the complete test architecture:

```clockwork
use clockwork::test_tools::tests::support::addition_tester_cog::AdditionTesterCog;
use clockwork::test_tools::tests::support::addition_cogs::AdditionCog;
use clockwork::test_tools::tests::support::messages::{AdditionMessage, SumMessage};

// Channel for publishing the addition message
channel AdditionChannel
{
    message_type: Tachyon<AdditionMessage>;
    max_num_messages: 10;
}

// Channel for the sum messages
channel SumChannel
{
    message_type: Tachyon<SumMessage>;
    max_num_messages: 10;
}

box AdditionSystemBox
{
    new addition_cog: AdditionCog;
    new addition_tester_cog: AdditionTesterCog;

    connect addition_tester_cog.addition_message to AdditionChannel;
    connect AdditionChannel to addition_cog.addition_message;

    connect addition_cog.sum_message to SumChannel;
    connect SumChannel to addition_tester_cog.sum_message;
}

cpu_domain SimCpu;

box AdditionSystem
{
    new box: AdditionSystemBox;
    new proc: Process(executable=addition_system_exe);
    apply HostProcess(process=proc) in box;
    apply HostCpuDomain(cpu_domain=SimCpu) to proc;
}

cpp_executable addition_system_exe
{
    casing
    {
        box AdditionSystemBox;
    }
}

system_target addition_system
{
    box: AdditionSystem;
}
```

### Generated Artifacts and Bazel Targets

The Clockwork compiler processes the Clockwork system definition and generates several important artifacts:

#### Process Description File

- **Filename**: `clockwork.clockwork.test_tools.tests.support.addition_test_system.addition_system.proc.tachyon`

#### Channel Configuration Files

In addition to the process description, the Clockwork compiler automatically generates channel configuration files:

- **Channel Publisher Config**: `addition_test_system.addition_system.SimCpu_channel_publisher_config.tachyon`
- **Log Writer Config**: `addition_test_system.addition_system.SimCpu_telemetry_logger_config.tachyon`
- **Metrics Config**: `addition_test_system.addition_system.SimCpu_metrics_channel_metadata_config.tachyon`

These files are referenced in your test code and control how messages flow through the CSR.

#### Executable Generation

The clk-deps tool generates a binary target for running a Clockwork system.
A corresponding library target needs to be manually added to accommodate offline use cases.

**Binary Target (`addition_system_exe`)**:

- Complete executable with main function
- Suitable for standalone system execution
- Includes `//clockwork/scaffolding:online_main` dependency

**Library Target (`addition_system_lib`)**:

- Library version without main function
- Required for linking with test executables
- Excludes `//clockwork/scaffolding:online_main` dependency

```python
cc_binary(
    name = "addition_system_exe",
    srcs = [
        "addition_system_exe.cc",
        "addition_system_exe.hh",
        "addition_system_exe.inl",
    ],
    deps = [
        ":addition_cog",
        ":addition_message",
        ":addition_tester_cog",
        ":sum_message",
        "//clockwork:repr_iface",
        "//clockwork/scaffolding:abstract_casing",
        "//clockwork/scaffolding:casing",
        "//clockwork/scaffolding:main",
        "//jewels/memory:memory_resource",
        "//jewels/memory:pmr_shared_ptr",
    ],
)

cc_library(
    name = "addition_system_lib",
    srcs = [
        "addition_system_exe.cc",
        "addition_system_exe.inl",
    ],
    hdrs = ["addition_system_exe.hh"],
    deps = [
        ":addition_cog",
        ":addition_message",
        ":addition_tester_cog",
        ":sum_message",
        "//clockwork:repr_iface",
        "//clockwork/scaffolding:abstract_casing",
        "//clockwork/scaffolding:casing",
        "//jewels/memory:memory_resource",
        "//jewels/memory:pmr_shared_ptr",
    ],
)
```

### Test Target Configuration

The final step is creating the test target that integrates all components.
Pay special attention to the dependency configuration and data files.

```python
cc_test(
    name = "clockwork_system_runner_no_log_test",
    srcs = ["clockwork_system_runner_no_log_test.cc"],
    data = ["//clockwork/test_tools/tests/support:clockwork.clockwork.test_tools.tests.support.addition_test_system.addition_system.proc.tachyon"],
    deps = [
        "//clockwork/scaffolding:offline_scaffolding",  #keep
        "//clockwork/test_tools:clockwork_system_runner",
        "//clockwork/test_tools/tests/support:addition_system_lib",  #keep
        "//jewels/std:expected",
        "//jewels/time:sync_time",
        "//jewels/utility:fix_clockwork_path",
        "@catch2//:catch2_main",
    ],
)
```

#### Key Configuration Elements

**Source Files**: Include your test implementation files

**Data Dependencies**: The process description file must be available at runtime

**Library Dependencies**:

- Core CSR functionality: `//clockwork/test_tools:clockwork_system_runner`
- System library: `addition_system_lib` (marked with `#keep` for gazelle)
- Test framework: `@catch2//:catch2_main`
- Supporting libraries as needed

**Important Notes**:

- The `#keep` annotation prevents gazelle from removing the system library dependency
- The system library dependency is required for linking but not directly referenced in source code
- Process description files should be referenced with their full generated path

```python
cc_test(
    name = "clockwork_system_runner_no_log_test",
    srcs = ["clockwork_system_runner_no_log_test.cc"],
    data = ["//clockwork/test_tools/tests/support:clockwork.clockwork.test_tools.tests.support.addition_test_system.addition_system.proc.tachyon"],
    deps = [
        "//clockwork/test_tools:clockwork_system_runner",
        "//clockwork/test_tools/tests/support:addition_system_lib",  #keep
        "//jewels/std:expected",
        "//jewels/time:sync_time",
        "@catch2//:catch2_main",
    ],
)
```

#### Test File Examples

- **`clockwork_system_runner_no_log_test.cc`**: Demonstrates Test Cog-based testing without log files
- **`message_injector_test.cc`**: Shows Message Injector API usage for programmatic testing
- **`addition_tester_cog_impl.cc`**: Example Test Cog implementation for interactive testing
- **`clockwork_system_runner_test.cc`**: Log-based testing using the log runner example system
