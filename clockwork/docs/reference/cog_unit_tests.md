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
#include example_clk_cc_test.hh”

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

