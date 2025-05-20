# Clockwork system runner

The clockwork system runner (CSR) is a utility that allows for running a deterministic clockwork system in unit/integration tests compatible with CI frameworks.

The system runner can be used in a log based configuration, or a test Cog driven configuration.

## Log-based configuration

In a log-based configuration the CSR reads a test log provided by the test, and publishes the logged messages to the configured channel while running the provided example system.
Configured channels are written to an output log which can then be examined by the test to validate whether the output channels contain the expected outputs.

## Test Cog-based configuration

Rather than use log files to drive the test, test driver Cogs can be written to publish messages used to stimulate the system/cogs being tested.
The test driver Cogs can receive output from the Cogs being tested and validate the output is as expected.

## Hybrid approach

A combination of using test Cogs and logs for validation can also be used.
For example a test Cog can publish messages and a log can be read to validate the output.
Or a log file can be used to drive publishing messages and a test Cog can be used to validate the output of the system.

## Configuring channels

If using a log publisher or log writer, the channels are configured in the same way the log runner is configured.
See the [Log runner](../examples/log_runner/README.md) for how that is done.

## Configuring a Clockwork system runner test in Bazel

### Example overview

For our example, we have a Clockwork system runner test we are attempting to test an `AdditionCog`.
The addition Cog simply takes two numbers from the message it receives in the `AdditionMessage`, adds them and outputs the sum to the `SumMessage`.

The test driver Cog, called `AdditionTesterCog` is configured to periodically output an Addition message.
It also listens for the `SumMessage` and upon receiving the sum message validates that the sum is correct.

### The addition test system

To test the `AdditionCog`, we create an `AdditionSystem` in addition_test_system.clk

```clockwork
use @clockwork::clockwork::test_tools::tests::support::addition_tester_cog::AdditionTesterCog;
use @clockwork::clockwork::test_tools::tests::support::addition_cogs::AdditionCog;
use @clockwork::clockwork::test_tools::tests::support::messages::{AdditionMessage, SumMessage};

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

The `clk-deps` tool yields a `clk_target` in Bazel that is populated with the process description file as an `out`.
The file is called `platforms.clockwork.test_tools.tests.support.addition_test_system.addition_system.proc.tachyon`.
It also generates a `cc_binary` called `addition_system_exe`.
For our test case, we are not particularly interested in running that executable directly, but we do need to use it so we make an equivalent `cc_library` called `addition_system_lib`, but make sure to leave out the `//platforms/clockwork/scaffolding:main` dependency.

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
        "@clockwork//jewels/memory:memory_resource",
        "@clockwork//jewels/memory:pmr_shared_ptr",
        "@clockwork//clockwork:repr_iface",
        "@clockwork//clockwork/scaffolding:abstract_casing",
        "@clockwork//clockwork/scaffolding:casing",
        "@clockwork//clockwork/scaffolding:main",
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
        "@clockwork//jewels/memory:memory_resource",
        "@clockwork//jewels/memory:pmr_shared_ptr",
        "@clockwork//clockwork:repr_iface",
        "@clockwork//clockwork/scaffolding:abstract_casing",
        "@clockwork//clockwork/scaffolding:casing",
    ],
)
```

Now, we can create our test target.
Note how the dependency on `//platforms/clockwork/test_tools/tests/support:addition_system_lib` is marked with #keep.
This is because it is only there for the linker so gazelle does not recognize that it belongs there.

```python
cc_test(
    name = "clockwork_system_runner_no_log_test",
    srcs = ["clockwork_system_runner_no_log_test.cc"],
    data = ["@clockwork//clockwork/test_tools/tests/support:platforms.clockwork.test_tools.tests.support.addition_test_system.addition_system.proc.tachyon"],
    deps = [
        "//jewels/std:expected",
        "@clockwork//jewels/time:sync_time",
        "@clockwork//clockwork/test_tools:clockwork_system_runner",
        "@clockwork//clockwork/test_tools/tests/support:addition_system_lib",  #keep
        "@catch2//:catch2_main",
    ],
)
```

### Writing the tests

For the example tests see `clockwork_system_runner_no_log_test.cc` and `addition_tester_cog_impl.cc`

For the logging-based test, the example is the `clockwork_system_runner_tests.cc`.
That test leverages the log runner example system.
