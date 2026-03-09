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
- Add `cpp` to the `generate` attribute for the Clockwork file and set the `namespace`.

```clockwork
#![generate(cpp)]
#![cpp(namespace=user::defined::namespace)]

// User schema
schema SomeSchema
{
  ...
}
```

## Cog Definition

- Create a new `cog` definition in a `.clk` file.
  - Add cpp_cog to the generate attribute to generate a target for all of the cogs implemented in the clk file
  - Add `use` statements for the entities needed in the cog definitions.
  - Create `state`s to capture persistent state as needed.
  - Create `input`s to accept messages from other cogs.
  - Create `output`s to publish output from your cog.
  - Create `execution` [conditions](exec_conditions.md) to specify when your cog should be executed.
  - If your cog needs one-time setup at startup, create an `init` cog to perform that setup.

```clockwork
#![generate(cpp, cpp_cog)]
#![cpp(namespace=user::defined::namespace)]

use path::to::example_schema::{ExampleSchema, ExampleState};

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

## Build file updates

- Run Gazelle to automatically generate Bazel rules for each stub.

## Cog Implementation

The `generate` attribute with `cpp_cog` requires that a used provided cog_name_clk_cc_impl cc_library, where cog_name is the name of the clk file containing the cog definition.
The cog_name_clk_cc_impl library must contain the execute_cog functions for all of the cogs in the clk file.

- Create a `cog_name_clk_cc_impl` `cc_library` target for user cog code.
- Create a `cog_name_clk_cc_impl.cpp` for each new `cog`, and add it to the target.
- Create an `execute_cog` function in the `impl` file for Clockwork to execute.

```cpp
#include "clockwork/dial/msg_input.hh"
#include "clockwork/pinion/publisher_handle.hh"
// Update the following includes to the paths in the repo containing the required elements.
#include "path/to/cog/cog_name_clk_cc_dial.hh"
#include "path/to/schema/schema_name_clk_cc.hh"

// Using the namespace from the `cpp` attribute in the clockwork file simplifies implementation.
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
#![generate()]
use path::to::example_schema::{ExampleSchema};

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
#![generate()]

use clockwork::logging::channel_policy::{ChannelLoggingPolicy, LogType};
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

## Cog instances and connections and creating a C++ executable

Instances of a `cog` are defined inside of a `box` element.
An executable must include one or more `box` elements.
`box`es contain connections between cogs and channels, and a `box` is required to generate a C++ executable.

- Instantiate the cog in one or more `box`es in the system.
- Make necessary connections between `cog` inputs/outputs and channels in the box.
- Make necessary connections between `cog` resources (i.e. memory resources) and resource instances in the box.
- Make necessary connections between `cog` states and state instances in the box.

```clockwork
#![generate(cpp_exe)]
#![cpp(namespace=user::defined::namespace)]

use path::to::example_schema::{ExampleSchema, ExampleState};
use path::to::example_cog::{ExampleSchema, ExampleCog};

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

- Create or use an existing `cpu_domain` to specify where processes are executed in a multi-compute node architecture.

```clockwork
cpu_domain ExampleCpu;
```

- Create a box to map processes and executables to CPU domains inside a Clockwork file that generates an executable.
- The cog and executable can all be defined in the same box with `generate(cpp, cpp_cog, cpp_exe)`.

```clockwork
#![generate(cpp_exe)]
use path::to::example_box::{ExampleBox};
use path::to::cpu_domain::{ExampleCpu};

box ExampleExe
{
    // Instantiate the ExampleBox defined above.
    new example_box: ExampleBox;

    // Create a process with the defined in this file (source file name plus _clk_exe suffix).
    new example_process: Process(executable=example_exe_clk_exe)

    // Run the box on the process
    apply HostProcess(process=example_process) in example_box;

    // Apply a CPU domain to each Process
    apply HostCpuDomain(cpu_domain=ExampleCpu) to example_process;
}
```

## System Integration

- If needed create a system target for your system.
- The system_target cannot have a `generate` attribute because it uses clk-deps to generate the build rules.

```clockwork
use path:to:example_exe::{ExampleExe};

system_target example_system
{
    box: ExampleExe;
}
```

- If needed, include policies defined in separate `.clk` files with `use` statements.

## Tips

- Each time you add an entity or `use` statement to a `.clk` file, run Gazelle to add the new targets and dependencies to the `BUILD` file.
- Examples system definitions in `clockwork/examples/helloworld` and `clockwork/examples/demo_system` are available for reference.
