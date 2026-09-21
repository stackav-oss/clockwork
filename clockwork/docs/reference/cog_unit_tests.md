# Cog Unit Tests

Clockwork can generate cog unit test wrappers that provide an API for manipulating the cog's configuration and state, sending messages to the cog's inputs, and receiving messages from the cog's outputs.
Generating cog unit test wrappers is enabled by adding the `cpp_test_cogs` target to the generate attribute on the source file.

```clk
// example.clk
#![generate(cpp, cpp_cog, cpp_test_cog)]
#![cpp(namespace=clockwork::examples)]

// Doc
cog ExampleCog
{
  ...
}
```

Unit test wrappers are also supported in the legacy syntax.

```clk
// legacy_example.clk

// Doc
cog LegacyExampleCog
{
  ...
}

cpp_target legacy_example
{
  options
  {
    namespace clockwork::example;
    generate_cpp_test_cogs true;
  }
  cog LegacyExampleCog;
}
```

When cog unit test wrappers are enabled, the compiler will produce a library with a `_test` suffix that contains a generated unit test wrapper for each cog named `<name>TestWrapper`.
Every generated wrapper defines some basic methods for initializing and executing the cog.

```cpp
/// Initialize the cog
/// @param[in] timeval Cog initialization time
/// @return True if the cog was successfully initialized
[[nodiscard]] bool initialize(jewels::time::SyncTime timeval);l

/// Call the cog's execute method if the cog is ready to run.
/// @param[in] timeval Cog execution time
/// @param[in] exec_duration Optional exec duration, defaults to zero.
/// @return True if the cog was executed.
[[nodiscard]] bool execute(jewels::time::SyncTime timeval, std::chrono::microseconds exec_duration = {});
```

In order to test a cog the first step is to set up the cog's memory resources, configuration and state and then call the initialize method to get the cog ready for the test.
During initialize any resource, configuration or state that wasn't explicitly initialized is default constructed and the inputs and output channels are initialized.
After initialization the execute method is invoked execute the cog at a specific time.
The following is a minimal example of how to execute a cog using defaults for all initializaiton.

```cpp
#include "example_clk_cc_test.hh”

TEST_CASE(“ExampleCogTest”)
{
  ExampleCogTestWrapper test_cog;
  jewels::time::SyncTime now;
  REQUIRE(test_cog.initialize(now));
  now += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(now));
}
```

The rest of this document uses examples to document how the generated wrappers are used to test cogs.

## Memory Resources

The following Clockwork file defines a cog that runs at initialization and has a memory resource.

```clk
// example.clk
#![generate(cpp, cpp_cog, cpp_test_cog)
#![cpp_namespace=clockwork::example]

// Doc.
cog ResourceCog
{
  resources
  {
    memres: persistent;
  }
  execution
  {
    execute when: init;
  }
}
```

Setting the memory resources before calling initialize is optional.
The resources for the test cog are initialized by default with whatever comes back from `std::pmr::get_default_resource`.
You can declare your own memory resource and use that instead by setting the resource before initializing the test wrapper.
A unit test for the memory resources example might look something like the following.

```cpp
#include example_clk_cc_test.hh”

TEST_CASE(“ResourceCogTest”)
{
  ResourceCogTestWrapper test_cog;
  const auto memres = jewels::memory::MemoryResource{std::pmr::new_delete_memory_resource()};
  const auto time0 = jewels::time::SyncTime{};
  test_cog.get_resources().set_memres(memres);
  REQUIRE(test_cog.initialize(time0));
  REQUIRE(test_cog.get_resources().get_memres() == memres);
  REQUIRE(test_cog.execute(time0));
}
```

In this example the initialize method doesn’t do anything but you still need to call it.
The `execute` method checks the cogs conditions and invokes the cog’s `execute_cog` method if the cog is ready to run.

## Configuration

This next example shows a cog with configuration that runs at intialization.

```clk
// example.clk
#![generate(cpp, cpp_cog, cpp_test_cog)
#![cpp_namespace=clockwork::example]
use configs;


// Doc.
cog ConfigCog
{
  configs
  {
    config1: Tappy<configs::MyConfig>;
  }
  execution
  {
    execute when: init;
  }
}
```

If the configuration is not set before `initialize` is called then an instance is default constructed when `initialize` is called.
You can create your own instance by passing a shared pointer into the `set_xxx_handle` on the cog’s configs object.
Config can be shared between cogs by using `get_xxx_handle` to extract the configuration from one cog and passing the result into `set_xxx_handle` on the seconds cog.

```cpp
#include example_clk_cc_test.hh”

TEST_CASE(“ConfigCogTest”)
{
  ConfigCogTestWrapper test_cog;
  const auto time0 = jewels::time::SyncTime{};
  test_cog.initialize(time0);
  test_cog.get_configs().get_config1().set_field1(123);
  REQUIRE(test_cog.get_configs().get_config1().get_field1() == 123);
  REQUIRE(test_cog.execute(time0);

  // Configs can be shared between cogs
  ConfigCogTestWrapper test_cog2;
  test_cog2.set_config1_handle(test_cog.get_config1_handle());
  REQUIRE(test_cog2.get_configs().get_config1().get_field1() == 123);
}
```

## State

This section shows an initialization cog that has two states, one with a Clockwork type and one with a type defined in C++.

```clk
// example.clk
#![generate(cpp, cpp_cog, cpp_test_cog)
#![cpp_namespace=clockwork::example]
use states;

// Doc
#[cpp(type_header="cpp_state.hh", type_namespace=clockwork::example)]
extern_type CppState;

// Doc.
cog StateCog
{
  states
  {
    state1: Tappy<states::MyState>;
    state2: CppState;
  }
  execution
  {
    execute when: init;
  }
}
```

If the state is not set before `initialize` is called then an instance is default constructed when `initialize` is called.
The memory resource used to construct C++ state objects defaults to the default memory resource and can be overridden by passing a memory resource into `initialize`.
State can be shared between cogs by using `get_xxx_handle` to extract the state from one cog and passing the result into `set_xxx_handle` on the seconds cog.

```cpp
#include example_clk_cc_test.hh”

TEST_CASE(“StateCogTest”)
{
  StateCogTestWrapper test_cog;
  const auto time0 = jewels::time::SyncTime{};
  test_cog.initialize(time0);
  test_cog.get_states().get_state1().set_field1(123);
  test_cog.get_states().get_state2.field1 = 234;
  REQUIRE(test_cog.get_states().get_state1().get_field1() == 123);
  REQUIRE(test_cog.get_states().get_state2().field1 == 234);
  REQUIRE(test_cog.execute(time0);

  // State can be shared between cogs
  StateCogTestWrapper test_cog2;
  test_cog2.set_state1_handle(test_cog.get_state1_handle());
  test_cog2.set_state2_handle(test_cog.get_state2_handle());
  REQUIRE(test_cog2.get_states().get_state1().get_field1() == 123);
  REQUIRE(test_cog2.get_states().get_state2().field1 == 234);
}
```

## Inputs

This section shows a cog that runs for every other message received on an input.

```clk
// example.clk
#![generate(cpp, cpp_cog, cpp_test_cog)
#![cpp_namespace=clockwork::example]
use messages;

// Doc.
cog InputCog
{
  inputs
  {
    input1: Tappy<messages::Message>;
  }
  execution
  {
    condition new_input1: new_message(input1, min=2, max=2);
    execute when: new_input1;
  }
}
```

Each input has a synthetic channel with enough slots to hold the greater of the input view size, or the max on any conditions that reference the input.
If neither the input view size or a max is specified then the view size defaults to 1.
It is also possible to set the number of slots explicitly using the `set_num_slots` method on the input before the test wrapper is initialized.
In the above example the view size is 1 (the default) so the synthetic channel has 2 slots to match the `max=2` on the input condition.

Messages are published to input channels by calling a `publish` method on the input.
There are two `publish` methods provided, one takes a reference to a message object, the other takes a function that initializes the message in place.

```cpp
/// Publish a message on the input channel
/// @param[in] message Message to publish
/// @param[in] publish_time Message publish time
/// @throws runtime_error if the slot cannot be reserved
void publish(const typename InputType::MsgType& message, jewels::time::SyncTime publish_time);

/// Publish a message on the input channel using an initializer function
/// @param[in] init_fn Function to initialize the message to publish
/// @param[in] publish_time Message publish time
/// @throws runtime_error if the slot cannot be reserved
void publish(const std::function<void(typename InputType::MsgType&)>& init_fn,
                                      jewels::time::SyncTime publish_time);
```

A unit test for the above cog might look something like this.

```cpp
#include example/example_clk_test.hh”

TEST_CASE(“InputCogTest”)
{
  InputCogTestWrapper test_cog;
  test_cog.get_inputs().get_input().set_num_slots(42); // Explicit slot count
  auto now = jewels::time::SyncTime{};
  test_cog.initialize(now);
  test_cog.get_inputs().get_input1().publish([](auto& msg){ msg.set_field1(123); }, now);

  REQUIRE_FALSE(test_cog.execute(now); // Cog needs two messages to run
  now += std::chrono::milliseconds(100);
  Message msg2;
  msg2.set_field1(234);
  test_cog.get_inputs().get_input1().publish(msg2, now);
  REQUIRE(test_cog.execute(now);
}
```

## Outputs

This section shows a cog that publishes to an output channel every 100 milliseconds.

```clk
// example.clk
#![generate(cpp, cpp_cog, cpp_test_cog)
#![cpp_namespace=clockwork::example]
use messages;

// Doc.
cog OutputCog
{
  outputs
  {
    output1: Tappy<output::Message>;
  }
  execution
  {
    condition periodic: time_since_last_exec(100ms);
    execute when: periodic;
  }
}
```

Each output has a synthetic channel with one slot to hold any message published when a cog executes.
Messages are received from output channels by calling either `get_next_message` or `try_get_next_message` on the cog's output.

```cpp
/// Get the next message from the output channel
///
/// The message reference remains valid until the next cog execution.
///
/// @return The next message from the output channel
/// @throws runtime_error if the cog has not been initialized or no message is available
[[nodiscard]] const typename OutputViewPolicy::MsgType& get_next_message();

/// Get the next message from the output channel if available
///
///
/// The message reference remains valid until the next cog execution.
///
/// @return The next message from the output channel or nullopt if no message is available
/// @throws runtime_error if the cog has not been initialized
std::optional<std::reference_wrapper<const typename OutputViewPolicy::MsgType>> try_get_next_message();
```

A unit test for this example might look like this.

```cpp
#include example/example_clk_cc_test.hh”

TEST_CASE(“OutputCogTest”)
{
  auto now = jewels::time::SyncTime{};
  OutputCogTestWrapper test_cog;
  test_cog.initialize(now);

  // Nothing has been published
  REQUIRE_THROWS(test_cog.get_outputs().get_output1.get_next_message());

  now += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(now);
  REQUIRE(test_cog.get_outputs().get_output1.get_next_message().get_field1 == 123);

  now += std::chrono::milliseconds(100);
  const auto maybe_msg = test_cog.get_outputs().get_output1.try_get_next_message();
  REQUIRE(maybe_msg);
  REQUIRE(maybe_msg->get().get_field1 == 234);
}
```

## Alignment Unit Testing

The unit test framework supports three approaches for testing alignment pipelines, each at a different level of isolation:

| Approach                                                       | What it tests                                                          | When to use                                                                |
| -------------------------------------------------------------- | ---------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| [Aligner isolation](#aligners)                                 | Aligner only                                                           | Validating aligner behavior                                                |
| [Consumer isolation](#consumer-cogs-with-aligned-inputs)       | Consumer cog with hand-crafted aligned message batches                 | Validating consumer behavior independent of aligner behavior               |
| [Combo (aligner + consumer)](#combo-testing-aligner--consumer) | Full pipeline: upstream data → aligner → alignment → consumer → output | Validating end-to-end behavior of one aligner + one alignment consumer cog |

### Aligners

Aligner cogs can be unit tested by adding `cpp_test_cog` to the generate list alongside `cpp_aligner` (**not** `cpp_test_aligner`; aligners are generated as cogs and use the same basic unit test approach).

```clk
// example_aligner.clk
#![generate(cpp, cpp_aligner, cpp_test_cog)]
#![cpp(namespace=clockwork::example)]

use messages::{SensorMsg};
use std::aligners::state;

// Example aligner for unit testing.
aligner ExampleAligner
{
    inputs
    {
        tick: Tappy<SensorMsg> { max_msgs: 10; }
        sensor: Tappy<SensorMsg> { max_msgs: 10; }
    }
    assume(is_strictly_increasing(tick.observation_time));
    assume(is_strictly_increasing(sensor.observation_time));
    require(|tick.observation_time - sensor.observation_time| <= 100ms);
}
```

This generates a `_cc_test` Bazel target containing the test wrapper library.
Add this target as a dependency of your `cc_test` rule (e.g. `":example_aligner_clk_cc_test"`).
The test wrapper exposes the same API as regular cog test wrappers: `get_inputs()`, `get_outputs()`, `get_states()`, and so on.

A minimal unit test for an aligner cog looks like the following.

```cpp
#include "example_aligner_clk_cc_test.hh"

TEST_CASE("ExampleAlignerTest")
{
  ExampleAlignerTestWrapper test_cog;
  auto now = jewels::time::SyncTime{std::chrono::milliseconds{3000}};
  REQUIRE(test_cog.initialize(now));

  // Publish sensor data before the tick. publish() returns a MessageHandle
  // containing the sequence number assigned to the message.
  now += std::chrono::milliseconds{1};
  auto sensor = test_cog.get_inputs().get_sensor().publish(
    [](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{std::chrono::milliseconds{3100}}); },
    now);

  // Publish the tick that triggers alignment.
  now += std::chrono::milliseconds{1};
  auto tick = test_cog.get_inputs().get_tick().publish(
    [](auto& msg) { msg.set_observation_time(jewels::time::SyncTime{std::chrono::milliseconds{3100}}); },
    now);

  REQUIRE(test_cog.execute(now));

  // Verify the alignment output using the message handles.
  const auto& alignment = test_cog.get_outputs().get_alignment().get_next_message();
  CHECK(alignment.get_tick_seq() == tick.seqno);
  CHECK(alignment.get_sensor_seq() == sensor.seqno);
}
```

### Consumer Cogs with Aligned Inputs

Consumer cogs that declare `aligned_inputs` can be unit tested with the generated test wrapper.
Add `cpp_test_cog` to the generate list alongside `cpp_cog`:

```clk
#![generate(cpp, cpp_cog, cpp_test_cog)]
```

The test wrapper provides a `get_{group_name}()` accessor for each aligned input group.
This returns a group struct with `publish_{input_name}()` methods for each upstream channel and a batch builder API for constructing alignment messages.

```cpp
auto aligned = test_cog.get_aligned();

// Publish upstream messages — returns a MessageHandle with the assigned seqno
auto sensor1 = aligned.publish_sensor(
  [](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3100ms)); }, now);
auto cam1 = aligned.publish_camera(
  [](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3150ms)); }, now);

// Build and publish an alignment message using the batch builder
auto batch = aligned.make_batch();
batch.set_sensor(sensor1);
batch.set_camera(cam1);
batch.set_lidar_range(lid1, lid2);
batch.set_radar(rad1);              // optional input: set_* or unset_*
batch.publish(now);

REQUIRE(test_cog.execute(now));
```

The `AlignedBatch` builder validates that all required (non-optional) inputs are set before publishing.
Optional inputs default to absent; use `unset_{name}()` to explicitly mark them absent after a prior `set_{name}()`.

### Combo Testing (Aligner + Consumer)

Combo testing allows testing the full pipeline from raw messages through alignment to consumer output in a single unit test.

#### Setup

Both the aligner and consumer modules must have `cpp_test_cog` in their `generate` lists.
The consumer module additionally needs `cpp_combo_test`:

```clk
// aligner.clk
#![generate(cpp, cpp_aligner, cpp_test_cog)]

// consumer.clk
#![generate(cpp, cpp_cog, cpp_test_cog, cpp_combo_test)]
```

In the consumer module's BUILD target, add the aligner's test wrapper as a dependency:

```starlark
clk(
    name = "consumer_clk",
    srcs = ["consumer.clk"],
    generate = ["cpp", "cpp_cog", "cpp_test_cog", "cpp_combo_test"],
    # Following must be provided manually and point to the right aligner test target:
    cpp_combo_test_aligner_deps = [":aligner_clk_cc_test"],
    ...
)
```

#### Usage

The combo wrapper is named `{ConsumerCog}ComboWrapper` and lives in the consumer module's `_cc_test` library.

```cpp
#include "consumer_clk_cc_test.hh"

TEST_CASE("Combo test")
{
  ConsumerCogComboWrapper combo;
  auto now = jewels::time::SyncTime{3000ms};
  combo.initialize(now);

  // Publish to shared upstream channels (visible to both aligner and consumer)
  now += 1ms;
  combo.publish_sensor(
    [](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3100ms)); }, now);
  now += 1ms;
  combo.publish_camera(
    [](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3150ms)); }, now);
  now += 1ms;
  combo.publish_lidar(
    [](auto& msg) { msg.set_observation_time(jewels::time::SyncTime(3110ms)); }, now);

  // Execute both aligner and consumer: returns true only if both execute
  REQUIRE(combo.execute(now));

  // Inspect consumer output
  const auto& echo = combo.get_consumer().get_outputs().get_echo().get_next_message();
  CHECK(echo.get_sensor_obs_time() == jewels::time::SyncTime(3100ms));
}
```

#### Step-by-step execution

Use `execute_aligner()` and `execute_consumer()` to run each cog independently and inspect intermediate state:

```cpp
REQUIRE(combo.execute_aligner(now));

// Inspect the alignment message before running the consumer
const auto& alignment =
  combo.get_aligner().get_outputs().get_alignment().get_next_message();

REQUIRE(combo.execute_consumer(now));
```

This is useful particularly if the consumer has additional execution triggers other than the alignment message, or to test the consumer's behavior when there are multiple pending alignments (run the aligner multiple times before running the consumer; simulates a slow consumer).

#### Consumer regular inputs

The combo wrapper also provides `publish_{name}()` methods for the consumer's regular (non-aligned) inputs, which are NOT shared with the aligner.

#### Underlying wrappers

Access the underlying aligner and consumer test wrappers via `get_aligner()` and `get_consumer()` for full control over inputs, outputs, and state.

## Signals

This example shows a cog that runs periodically with a single signal report group defined.
The unit test wrapper provides access to the messages that are generated by the cog.
Since messages from different report groups can be published at different cadences, the test wrapper does not present the signals from the cog as through a single object.
If a cog only has one report group the messages generated by that report group are accessed by calling the `get_signals` method.
If a cog has more than one report group, then the report group messages are accessed by calling `get_signals().get_<name>()`, because each report group is published in a separate message.

```clk
// example.clk
#![generate(cpp, cpp_cog, cpp_test_cog)
#![cpp_namespace=clockwork::example]
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupPolicyConfig, ReportGroupLogType};

// Signals test cog
cog SignalsCog
{
  signals group
  {
    // Doc
    value1 signal Int64
    {
      post_aggregation: ["min", "max"];
      multi_instance: true;
    }
  }
  execution
  {
    condition periodic_100ms: time_since_last_exec(100ms);
    execute when: periodic_100ms;
  }
}

policy ReportGroupPolicy for SignalsCog.group
{
  reporting_strategy = ReportingStrategy::post_aggregated;
  log_type = ReportGroupLogType::none;
  max_observations = 2;
}
```

A unit test for this example might look like this.

```cpp
#include example/example_clk_cc_test.hh”

TestCase("SignalsCogTest")
{
  auto now = jewels::time::SyncTime{};
  SignalsCogTestWrapper test_cog;
  test_cog.initialize(now);

  now += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));

  // Report group published every other execution
  REQUIRE_FALSE(test_cog.get_signals()..try_get_next_message());

  now += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(now));

  auto signals_msg = test_cog.get_signals().get_next_message();
  REQUIRE(signals_msg.get_execution_count() == 2U);
  REQUIRE(signals_msg.get_value1_value_min() == 1);
  REQUIRE(signals_msg.get_value1_value_max() == 2);
}
```

