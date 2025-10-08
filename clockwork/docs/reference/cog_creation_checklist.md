# Cog Creation Checklist

Directions for creating a cog and integrating it into a system.
Assumes basic familiarity with Clockwork concepts.
Refer to [Clockwork 101](../clockwork101.md) for high-level Clockwork concepts.
Refer to the [Hello World tutorial](../tutorial/helloworld.md) for a guided Clockwork system generation tutorial.

Throughout the instructions entity names will contain placeholders like `example`, `cog_name`, `CogName`, and `path::to::resource`.
These should be substituted with the name of your cogs, `.clk` file paths, etc.

## Schema Definition

A `.clk` file with schema definitions may contain more than one schema definition.
It may also contain one or more `cpp_target` elements containing one or more schema definitions each.

- Define a [schema](schemas.md) if needed.
  - Schemas are required to describe `cog` inputs, outputs, and state.
- Add the `schema` to a `cpp_target` to generate the C++ files with the schema definition.
  - Include the `schema` element to capture the schema definition, the `representation` element, and the `interface` element.
    See the [representation and interface](repr_iface.md) for more details.
  - Optionally include a namespace to prefix the generated C++ code with.

```clockwork
// Example for how to instantiate schemas for use in a cog.
// Assumes schema definitions exist for "ExampleSchema" and "ExampleState"
cpp_target example_schema
{
    options
    {
        namespace user::defined::namespace;
    }

    schema ExampleSchema;
    representation Tachyon<ExampleSchema>;
    interface Tappy<ExampleSchema>;

    schema ExampleState;
    representation Tachyon<ExampleState>;
    interface Tappy<ExampleState>;
}
```

## Cog Definition

- Create a new `cog` definition in a `.clk` file.
  - Create `state`s to capture persistent state as needed.
  - Create `input`s to accept messages from other cogs.
  - Create `output`s to publish output from your cog.
  - Create `execution` [conditions](exec_conditions.md) to specify when your cog should be executed.
  - If your cog needs one-time setup at startup, create an `init` cog to perform that setup.

```clockwork
// Example of a cog with resources, state, inputs, and outputs.
// Uses the schema examples from the Schema Definition section.
// Not all of these entry types may be needed for all cogs.
cog ExampleCog
{
    resources
    {
        example_cog_memory: persistent;
    }

    state
    {
        state: example_state
        {
            mutable: true;
        }
    }

    inputs
    {
        example_cog_input: Tappy<ExampleSchema>
        {
            max_msgs: 10;
        }
    }

    outputs
    {
        example_cog_output: Tappy<ExampleSchema>;
    }

    execution
    {
        condition new_input: new_message(example_cog_input);
        execute when: new_input;
    }
}
```

- Import entities from other `.clk` files using [`use` statements](use.md) at the top of the `.clk` file.

```clockwork
use clockwork::examples::demo_system::demo_system
use path::to::example_schema::{ExampleSchema, ExampleState}
```

- Add the `cog` to a `cpp_target` to generate C++ interface files.

```clockwork
cpp_target ExampleTarget
{
    cog ExampleCog;
}
```

- Add the `cpp_target` to a `casing` within a `cpp_executable` to generate an executable containing the cog.

```clockwork
cpp_executable ExampleExecutable
{
    casing {
        cog ExampleCog;
    }
}
```

## Build file updates

- For each new `.clk` file, add a `clk` stub to the `BUILD.bazel` file in the same directory.
  By convention, the target's name should be the same as the `.clk` file's filename, with an underscore replacing the dot before the clk file extension.

```clockwork
load("//clockwork:rules.bzl", "clk")
clk(
    name = "new_file_clk",
    srcs = ["new_file.clk],
)
```

- Run `clk-deps` to automatically generate Bazel rules for each stub.

## Cog Implementation

The `BUILD.bazel` update added a `cog_name_impl` target to the cog target's dependency list.
This target is intended for user-generated code necessary to implement your cog's functionality.
The `cog_name_impl.cpp` target is not created automatically.

- Create a `cog_name_impl` `cc_library` target for user cog code.
- Create a `cog_name_impl.cpp` for each new `cog`, and add it to the target.
- Create an `execute_cog` function in the `impl` file for Clockwork to execute.

```cpp
#include "clockwork/dial/msg_input.hh"
#include "clockwork/pinion/publisher_handle.hh"
// Update the following includes to the paths in the repo containing the required elements.
#include "path/to/cog/cog_name_dial.hh"
#include "path/to/schema/schema_name.hh"

// If the `cpp_target` a cog was included in specified a namespace, adding the execute_cog function to that
// namespace simplifies implementation.
namespace cpp::target::namespace
{

void execute_cog(CogNameDial& dial)
{
  // Interact with Cog dial here.
}
}  // namespace cpp::target::namespace

```

## Channel Definition

`cog` inputs and outputs are connected via a `channel`.
Each `channel` may be defined in the same `.clk` file as the `cog`, or in a separate `.clk` file to make tracking them easier.
Channels are automatically instantiated in the Clockwork compiler, they do not need to be added to a `cpp_target` like a `cog` or `schema`.

- Create or use existing `channel`s with the same schema used in a `cog`'s input and outputs.

```clockwork
channel ExampleInputChannel
{
    message_type: Tachyon<ExampleSchema>;
    max_num_messages: 10;
}

channel ExampleOutputChannel
{
    message_type: Tachyon<ExampleSchema>;
    max_num_message: 20;
}
```

## Channel Logging

`policy` entries control how channels are logged by the logger.
Each channel that should be logged needs a `ChannelLoggingPolicy` to specify how it should be logged.
The channel logging policies must be visible during system compilation.
They should be defined in a `.clk` file with their channels, or imported via a `use` statement.

- Create a new `ChannelLoggingPolicy` for each channel that needs to be logged.

```clockwork
use clockwork::logging::channel_policy::{ChannelLoggingPolicy, LogType}
use clockwork::logging::channel_type::ChannelType;
use path::to::example_channels;

// Defining this policy's log type as "telemetry" adds the channel to both the telemetry
// log writer configuration and the event log writer configuration. Setting the log type
// to "event" would only add the channel to the event log writer configuration.
policy ChannelLoggingPolicy for example_channels::ExampleInputChannel
{
  log_type = LogType::telemetry;
}

policy ChannelLoggingPolicy for example_channels::ExampleOutputChannel
{
    log_type = LogType::event;
}
```

## Cog instances and connections

Instances of a `cog` are defined inside of a `box` element.
A system must include one or more `box` elements.
`box`es contain connections between cogs and channels, and a `box` is required to generate a `system_target`.

- Instantiate the cog in one or more `box`es in the system.
- Make necessary connections between `cog` inputs/outputs and channels in the box.
- Make necessary connections between `cog` resources (i.e. memory resources) and resource instances in the box.
- Make necessary connections between `cog` states and state instances in the box.

```clockwork
box ExampleBox
{
    // Instantiate an ExampleCog named example_cog
    new example_cog: ExampleCog;
    new state_memory: HeapMemory(max_size=4'096);
    new cog_memory = HeapMemory(max_size = 1'024);
    new cog_state: State(representation=ExampleState, memory_resource=state_memory);

    // Connect state and memory resources to the cog.
    connect cog_memory to example_cog.example_cog_memory;
    connect cog_state to example_cog.example_state;

    // Connect a channel to each of the cog's inputs and outputs.
    connect ExampleInputChannel to example_cog.example_cog_input;
    connect example_cog.example_cog_output to ExampleOutputChannel;
}
```

## System Integration

- Create or use an existing `cpu_domain` to specify where processes are executed in a multi-compute node architecture.

```clockwork
cpu_domain ExampleCpu;
```

- Create or use an existing `box` to map processes and executables to CPU domains.

```clockwork
box ExampleSystemBox
{
    // Instantiate the ExampleBox defined above.
    new example_box: ExampleBox;

    // Create a process with the cpp_executable defined above.
    new example_process: Process(executable=ExampleExecutable)

    // Run the box on the process
    apply HostProcess(process=example_process) in example_box;

    // Apply a CPU domain to each Process
    apply HostCpuDomain(cpu_domain=ExampleCpu) to example_process;
}
```

- If needed, create a system target to generate the whole system from a top-level box.

```clockwork
system_target example_system
{
    box: ExampleSystemBox;
}
```

- If needed, include policies defined in separate `.clk` files with `use` statements.

## Tips

- Each time you add an entity or `use` statement to a `.clk` file, run `clk-deps` to add the new targets and dependencies to the `BUILD` file.
- Examples system definitions in `clockwork/examples/helloworld` and `clockwork/examples/demo_system` are available for reference.
