# Hello, World! (but in Clockwork)

> [!NOTE]
> This tutorial uses Clockwork file format that is recommended for new development.
> The original Clockwork language format is somewhat more verbose but it is also still fully supported.
> See the [original tutorial](legacy_helloworld.md) to learn about the original language format.

This tutorial walks you through creating a simple Clockwork system, step by step.
It is _not_ a complete reference guide and does not show all Clockwork features.

**Prerequisites:** Read [Clockwork 101](../clockwork101.md) for a high-level overview of the system.

**In this tutorial, you will learn how to:**

- Clockwork schema annotations
- Define data models (schemas).
- Create components (Cogs) that can produce and/or consume data.
- Assemble the Cogs into a system.
- Create an executable and run the system.

## Clockwork Schema Attributes

Clockwork uses attributes in the source to tell the compiler how to generate code from the source.
Annotations are lines that start with either `#![` for inner attributes or `#[` for outer attributes.
Inner attributes define settings that apply to the entire source file.
Outer attributes define settings that only apply to the following statement.

The first attribute defined in a Clockwork file must be a generate attribute, which tells the compiler what code to generate from the Clockwork source.
The value of the generate attribute is a list of target names.
Clockwork supports generating schema defininitions in C++ ("cpp"), python ("py"), protobuf ("proto"), and nanobind ("nanobind").
For example, this next line tells the compiler to generate C++.

```clockwork
#![generate(cpp)]
```

Additional language specific attributes provide additional information needed to generate the code for each language.
To generate C++ you need to provide the namespace to use for the generated code.

```clockwork
#![cpp(namespace=stack::demo)]
```

## A simple schema

You can't do much of anything in Clockwork without data, like messages, and that requires schemas.
Normally there would be lots of schemas, but we're going to cheat in this tutorial and create just a single schema that we'll reuse for everything.

Put this in a file called `av/demo/schema.clk`:

```clockwork
// Demo schemas
#![generate(cpp)]
#![cpp(namesapce=stack::demo)]


// A simple multi-purpose schema
//
// This is just for the Hello, World! tutorial.
schema SimpleSchema
{
    uuid: 26276f88-ab47-46c8-8528-d69aaaa3e17b;
    fields
    {
        // A scalar field.
        #0 a_field: Int64;
    }
}
```

Let's walk through this.
We start with what looks like a comment, but it's actually [documentation](../reference/common_syntax.md#documentation).
A Clockwork source file (aka "module") may optionally have a documentation block at the beginning, but it is not required.
If it appears, it must be at the top of the file.

Then we get some inner attributes that tell the compiler to generate C++ using the stack::demo namespace.
The compiler will put the code into files named "schema_clk_cc.cc", "schema_clk_cc.hh", and "schema_clk_cc.inl".

Then we get another documentation block; this is for the schema.
This one is required; if you don't have documentation for a schema the compilation will fail.

Then we introduce and name the schema `SimpleSchema`.

### Schema UUIDs

Most schemas need a unique UUID.
Don't copy/paste these from any other schema; create a new random UUID from scratch for each new schema.
But then, _never change the schema UUID_, because the entire purpose is to provide an identifier that never changes for each schema, even if the schema name changes or moves to a different source location.

It's recommended to use `uuidgen` at the command line to generate random UUIDs.
More information is in [the schema reference manual](../reference/schemas.md#schema-uuids).

Note that the compiler won't _immediately_ complain if you leave the UUID out.
It will complain if you ever try to _log_ a channel with a schema that doesn't have a UUID.
UUIDs are needed mostly for backward compatibility; but if you don't log it, you don't need backward compatibility.
So UUIDs are optional unless the schema is written to a log.
Many schemas won't be logged, such as those used for configuration data, and so don't require UUIDs.

### Fields

Every field has:

- Documentation
- A field number (#0 above)
- A name (a_field)
- A data type (Int64)

You can have multiple fields, and fields with non-scalar data types.
We're just keeping it simple for this example.

### Bazel rules

You need a Bazel target, in `BUILD.bazel`:

```py
load("@clockwork//clockwork:rules.bzl", "clk")

clk(
    name = "schema_clk",
    srcs = ["schema.clk"],
    outs = [
        "schema_clk_cc.cc",
        "schema_clk_cc.hh",
        "schema_clk_cc.inl",
    ],
    generate = ["cpp"],
)
```

That's the complete rule.
Clockwork also comes with a gazelle plugin under `tools/gazelle/clk_plugin` that you can use to automate generating build files for you.

### Generating other languages

To generate other languages you just add the languages you want to the `generate` attribute and provide language specific attributes to tell the compiler what to do.
For example, to generate python and protobuf from the above example, the attributes at the top of the file would change to this.

```clockwork
#![generate(cpp, py, proto)]
#![cpp(namesapce=stack::demo)]
#![proto(package=stack.demo.proto)]
```

The Bazel rule to generate C++, python, and protobuf would look like this.
If you use gazelle this is all done for you so you don't have to think about it.

```py
load("@clockwork//clockwork:rules.bzl", "clk")

clk(
    name = "schema_clk",
    srcs = ["schema.clk"],
    outs = [
        "schema_clk_cc.cc",
        "schema_clk_cc.hh",
        "schema_clk_cc.inl",
        "schema_clk_proto.proto",
        "schema_clk_py.py",
    ],
    generate = ["cpp", "proto", "py"],
)
```

### More information

You can see the [reference documentation for the schema language](../reference/schemas.md) for more complete documentation.

## A producer Cog

The first Cog we'll make does nothing but produce a message at a fixed frequency; this sort of simulates a sensor driver.
Most real-world Cogs won't work like this; they'll consume data and will probably be triggered by receipt of that data rather than periodically, but we need some source of data in our demo system.

Let's make `cogs.clk`:

```clockwork
// Demo cogs
#![generate(cpp, cpp_cog)]
#![cpp(namespace=stack::demo)]

use demo::schema;

// Producer Cog
cog ProducerCog
{
    resources
    {
        my_memory: persistent;
    }

    outputs
    {
        my_output: Tappy<schema::SimpleSchema>;
    }

    execution
    {
        condition my_periodic: time_since_last_exec(500ms);
        execute when: my_periodic;
    }
}
```

First off, you don't actually _have_ to make this a separate file.
You could put this in the same source file as the schema.
We're separating them because that's probably how you'll usually do this in practice.
This then requires us to import the schema module from the Cogs module; that's what the `use` statement does.
This makes anything defined in that module available in the `schema::` namespace.
You can see the [documentation on attributes and code generation](../reference/attributes.md) for details.

Then we define the `ProducerCog` Cog.
There are _many_ things that can go in a Cog definition.
Here we define a PMR memory resource `my_memory` which provides persistent heap memory.
Then we define one output called `my_output` on which we can publish messages of type `Tappy<schema::SimpleSchema>`.
You will always have a `Tappy` type here in the current version of Clockwork; future Clockwork versions might have other options.

Lastly, we specify when this Cog should execute.
You can create multiple execution `condition`s, but here we create just one: a condition called `my_periodic` which will become active whenever it has been at least 500 milliseconds since the last execution.
The `execute when` statement would allow us to combine multiple conditions in a condition expression, but we only have one condition, so we just name it here.
The net effect is that this Cog will run at 2Hz (every 500ms).

Like schemas, we need to generate C++ code for this Cog, so there's a `cpp_cog` target in the generate attribute.
The Cog itself is a function that you write, which we'll show in a moment, but Clockwork also needs to generate a bunch of code that calls your function, and also a Dial structure, which is how your function gets access to its resources and endpoints, and that's what this `generate(cpp_cog)` makes happen.

So, how do you write your code? Make a file named `cogs_clk_cc_impl.cc`.
The file name is the name of your source ("cogs.clk") with a `_clk_cc_impl.cc` added to the name.
and make it look a bit like this:

```cpp
#include "demo/cogs_clk_cc_dial.hh"

#include <iostream>

namespace stack::demo
{
void execute_cog(ProducerCogDial& dial)
{
    // We can access time:
    const auto now = dial.get_start_time();

    std::cout << "ProducerCog executing at " << now.time_since_epoch().count() << std::endl;

    // We can access our dynamic memory resource:
    auto my_data = std::pmr::vector<uint32_t>(dial.get_resources().get_my_memory());

    // And we can access our output message:
    auto& output = dial.get_outputs().get_my_output().message();
    // And set fields:
    output.set_a_field(42);
    // And finally, mark the message as ready to publish:
    dial.get_outputs().get_my_output().mark_for_publish();
}
} // namespace stack::demo
```

The header file you include there is generated by the Clockwork compiler when you run a build.
It defines a struct called `ProducerCogDial` that contains all of the resources and endpoints you've declared in your Cog specification, plus some standard member functions like `get_start_time`.
You can see the basics for our simple Cog above.

Gazelle generates build targets for code generated by the Clockwork compiler, but it does not generate the build target for the Cog implementation.
It's up to you to provide a `cc_library` target named `[stem]_clk_impl` (where `stem` is the stem of the Clockwork source file) that contains your Cog code.
For this example, it should look something like this:

```py
cc_library(
    name = "cogs_clk_cc_impl",
    srcs = ["cogs_clk_cc_impl.cc"],
    deps = [
        ":cogs_clk_cc_dial",
    ],
)
```

That's a completed Cog!
But we can't actually execute it yet, because we haven't instantiated it in a system, nor created an executable.
(We have only a C++ library right now, not an executable.)

## Creating an executable

Normally, you'd do system composition in a separate Clockwork module, but since we're making a single-Cog system here, we'll just add this to our existing `cogs.clk`:
All you have to do is change the generate attribute to tell Clockwork to generate the C++ executable.
The rest of the source file is the same.

```clockwork
#![generate(cpp, cpp_cog, cpp_exe)]
#![cpp(namespace=stack::demo)]
```

This is going to cause a `main` function to be generated in `cogs_clk_exe` (after you do the Bazel magic, it will create a `cc_binary` target for you).
The generated executable will be a binary that knows how to run all of the cogs defined in the file.
Clockwork handles the magic of getting this all linked into an executable binary.
However, we still haven't made a `system`, so this executable won't actually work.
The executable takes a command line argument which is the path to a _process description file_.
This is a binary file that is also generated by Clockwork to define a process within a system.
This two-step process allows the same executable to be used in multiple processes and multiple systems.

## Creating a system

Again in the same file, we'll now add some system composition:

```clockwork
// A module containing what ProducerCog needs to run
box ProducerBox
{
    new my_memory: HeapMemory(max_size=1'000'000);
    new producer: ProducerCog;

    connect my_memory to producer.my_memory;
}

channel ProducerA
{
    message_type: Tachyon<schema::SimpleSchema>;
    max_num_messages: 10;
}

cpu_domain DemoCpu;

box ProducerOnlySystem
{
    new producer_a: ProducerBox;
    // The box left the output unconnected; we have to connect it here:
    connect producer_a.producer.my_output to ProducerA;

    new demo_process: Process(executable=cogs_exe);
    apply HostProcess(process=demo_process) in producer_a;
    apply HostCpuDomain(cpu_domain=DemoCpu) to demo_process;
}
```

Let's walk through that.
First we create a Box called `ProducerBox`, which contains a single _instance_ of `ProducerCog` called `producer`.
Note that when we define a Cog in the domain-specific language (DSL), we're really defining a _cog class_, which needs to be instantiated to be used.
And it can be instantiated multiple times; we'll see that later.
We also instantiate a memory resource, because we specified that our Cog needs one.
When we create a memory resource, we specify a maximum size (in bytes).
Then we have to _connect_ that memory resource to the memory endpoint that we defined in the Cog.

Because we've done all of that in a Box, we have a reusable module for this Cog.
That Box itself can be instantiated multiple times, but we've reduced the boilerplate because now instead of instantiating both the Cog and it's memory resource and connecting those, we just instantiate the Box, and the memory resource and Cog come together as a unit.

But, we did not connect the output of the Cog to a channel yet.
First, we need to define a channel; here we define one called `ProducerA`.
Normally schemas and channels are defined in a separate file so they can be imported in other Cogs without bringing in the rest of the definitions for the Cog, but since we're making a single-Cog system here, we'll just define schemas and channels in our existing `cogs.clk`.
Channels need a message type, which in the current version of Clockwork should always be a `Tachyon` type (**not** a `Tap` or `Tappy` type; we want a representation here, not an interface).
They also need a `max_num_messages` parameter, which controls how big the sliding window of messages held in memory will be.
This generally should be set to at least one second's worth of messages; so for a 10Hz message, 10 is a reasonable value.

A system next needs a CPU to run on.
Even if you're just going to run this on your workstation, you need to define a CPU domain.
We define one here called `DemoCpu`.


Then, we create another box; we could have put everything in one Box but this is more representative of how system composition really works in practice with good modularity.
First we instantiate our `ProducerBox`.
This gives us, inside that Box, an already-connected Cog and memory resource.
We connect the output of that Cog to our channel.

At this point, our system is fully connected, but we haven't specified where to execute that Cog.
We need a process, so we create one called `demo_process`.
We specify which process the Cog runs in by _applying_ the `HostProcess` policy; we apply this recursively inside the Box we defined earlier, which just means that everything inside that Box runs in that process.
Then, we apply `HostCpuDomain` to that process to specify which CPU it runs on.

Lastly, we just need to name this Box in a `system_target`, and we have defined a system.
The `system_target` needs to go into a different source file because Gazelle doesn't know how to generate build files for systems.
The build rule for this system target file is managed by running `stack clk-deps //demo/...`.
That `system_target` will generate a lot of data files, but no C++ code.
It will generate a process description file, which the executable needs, plus logger configurations, network bridge configurations, and a set of SimpleLaunch configuration that will launch the whole system.

Let's make `system.clk`:

```clockwork
use demo::cogs;

system_target single_producer
{
  box: cogs::ProducerOnlySystem;
}
```

You don't actually need the SimpleLaunch script or logger configuration to execute just your one process; you can run that executable passing the path to the generated process description file, and it will start working.
(When you have multi-process systems, that's when you probably want to be using those SimpleLaunch configs.)

## Running single-process systems

When an executable consists of a single process executing on a single CPU domain, you can run them manually.
To run an executable, you must pass a process description file (`*.{process_name}.tachyon`).
You can find the process description filename by looking at the `outs` of the `clk` Bazel target that contains your `system_target`
When you pass it to the executable, you need to reference it's location in the Bazel outputs directory at the same path as your `clk` Bazel target that contains the `system_target` (e.g. `bazel-bin/autonomy/...`)

> [!IMPORTANT]
> You must run `rm -rf /dev/shm/clockwork/pinion/pub` in between invocations of executables.
> If you forget to do this you will see an error similar to `ERROR clockwork/scaffolding/channels.cc:76 [1729647772.755608156] Could not create channel '0b3b5de1-eda3-5ed2-a43a-9ebf7c2d698d': dirty`

## Adding a second producer

Let's make our Cog run twice, producing on two different channels, sort of the way a camera driver might run once for each camera, publishing on different channels.
To do this, we don't have to write any C++ code; this is just a DSL change.

Add this to `cogs.clk`:

```clockwork
// Add a second channel:
channel ProducerB
{
    message_type: Tachyon<schema::SimpleSchema>;
    max_num_messages: 10;
}

// Rename and modify the system box:
box TwoProducerSystem
{
    new producer_a: ProducerBox;
    connect producer_a.producer.my_output to ProducerA;

    // Add this stuff:
    new producer_b: ProducerBox;
    connect producer_b.producer.my_output to ProducerB;

    new demo_process: Process(executable=cogs_exe);
    apply HostProcess(process=demo_process) in producer_a;
    // And add this:
    apply HostProcess(process=demo_process) in producer_b;
    apply HostCpuDomain(cpu_domain=DemoCpu) to demo_process;
}
```

And then change the system.clk file to this:

```clockwork
use demo::cogs;

system_target single_producer
{
    // Change the box name here too
    box: TwoProducerSystem;
}
```

Here, we have two instance of the same Cog, both running in the same process.
If you execute that, you'll now see the `std::cout` output at twice the previous rate, because now there are two Cog instances running.

Note that making that change did not result in recompiling any C++ code.
It just regenerates those system files, which tell the existing compiled code what to instantiate at runtime.

## Adding a consumer

> [!WARNING]
> Inputs in Clockwork are by default "latest message" inputs, not "every message" inputs!
> This means that skipping messages to keep up to date with the latest data is the default behavior.
> If you need every message, you must request that specifically.
> See the [input configuration documentation](../concepts/input_views_triggers.md) for more details.

If we want to have something consume these messages, we have to define a new Cog class, because our existing one has no inputs.
Let's just add this in the same `cogs.clk` file:

```clockwork
// A consumer
cog ConsumerCog
{
    inputs
    {
        input_a: Tappy<schema::SimpleSchema>
        {
            max_msgs: 4;
        }
        input_b: Tappy<schema::SimpleSchema>
        {
            max_msgs: 4;
        }
    }

    execution
    {
        // This input can skip messages in between calls:
        condition new_a: new_message(input_a);
        // This input will get every message:
        condition new_b: new_message(input_b, max=1);
        condition any_a: any_message(input_a, min=2);
        condition any_b: any_message(input_b, min=2);
        condition timeout: time_since_last_exec(600ms);
        execute when: timeout or (any_a and any_b and (new_a or new_b));
    }
}
```

The new `ConsumerCog` has an `inputs` block.
Here, we define two separate inputs, though both with the same message type, `input_a` and `input_b`.
Each of them has a view size of 4, meaning we get a sliding window over the last four messages.
These views will have _up to_ four messages in them, but at startup there will be fewer.

In the execution section, we're doing some fancy stuff.
On each input,we have both a _new_ condition and an _any_ condition.
These do what you'd think from the names; a `new_message` condition is active (true) when there is a new message on the input.
A new message is one that the Cog has not "seen" before; i.e., it's never been present in the input view during any previous execution of the Cog.

> [!NOTE]
> If you want _every_ message rather than _latest_ you need that `max` parameter and it needs to be configured correctly.
> See the [input configuration documentation](../concepts/input_views_triggers.md) for more details.

The `any_message` condition is active (true) when there is _any_ message on the input, including messages the Cog has seen before.
Here, we do a little bit more; we specify `min=2`, meaning we want not just one message but at least two messages.
In effect, this means that our consumer waits at startup until both of its inputs have at least two messages.

Or it would, except we also have that `timeout` condition.
This is active if it's been at least 600ms since the last execution.
This means that even if we're not satisfying our input conditions, the Cog can execute.

Those all get combined in the `execute when` expression, which uses boolean and/or logic to combine multiple conditions. Here's the plain English translation of that:

Execute ConsumerCog when:

- It's been at least 600ms since the last execution (the timeout condition)
- OR we have inputs:
  - At least two messages on input_a (including messages we've seen before)
  - AND at least two messages on input_b (including messages we've seen before)
  - AND there's at least one new message on either input_a or input_b (or both)

In effect, what's (most likely) going to happen when we run this Cog along with our two producers is that on startup (at roughly t=0) each producer will produce one message.
The consumer still can't run because it wants _two_ messages on each input.
At roughly t=500, each producer will run again and produce a second message.
So long as those complete within 100ms, then _before_ the 600ms timeout becomes active, our input conditions will become active, and the consumer Cog will run.
Thereafter, the consumer Cog will run once or twice every 500ms, depending on some non-deterministic timing jitter between the three Cogs.
If both producers run first, then a single execution of the consumer will pick up both new messages, but jitter makes it possible that one producer will run, then the consumer, then the other producer, and then the consumer again, getting only one message on each execution.

> [!NOTE]
> When running in the deterministic runner, which is how simulation works, there will not be non-determinism, but it is still undefined in this system whether the consumer runs once every 500ms or twice.
> (This is a somewhat contrived example; in real-world conditions you'd likely be using an aligner here to coordinate messages across these two inputs, which would give better-defined behavior.)

We need to implement the user code now, in the same `[cogname]_impl` library as the producer code:

```cpp
void execute_cog(ConsumerCogDial& dial)
{
    // We can access time:
    const auto now = dial.get_start_time();

    std::cout << "ConsumerCog executing at " << now.time_since_epoch().count() << std::endl;

    if (dial.get_conditions().get_timeout())
    {
        std::cout << "Timeout!\n";
    }

    for (const auto& msg : dial.get_inputs().get_input_a().get_new_msgs_view())
    {
        std::cout << "New message on A: " << msg.get_a_field() << std::endl;
    }

    for (const auto& msg : dial.get_inputs().get_input_b().get_new_msgs_view())
    {
        std::cout << "New message on B: " << msg.get_a_field() << std::endl;
    }
}
```

This shows examples of changing which execution conditions were active, and how to retrieve messages.
Here we're retrieving only new messages, but there are other API options; see `platforms/clockwork/dial/msg_input.hh` for the full API.

## Connecting producers to consumers

Now we need to modify our `cogs.clk` to include the consumer:

```clockwork
// Rename and modify the system box:
box FullSystem
{
    new producer_a: ProducerBox;
    connect producer_a.producer.my_output to ProducerA;

    new producer_b: ProducerBox;
    connect producer_b.producer.my_output to ProducerB;

    // Instantiate and connect our new cog:
    new consumer: ConsumerCog;
    connect ProducerA to consumer.input_a;
    connect ProducerB to consumer.input_b;

    new demo_process: Process(executable=cogs_exe);
    apply HostProcess(process=demo_process) in producer_a;
    apply HostProcess(process=demo_process) in producer_b;
    // And specify where it runs:
    apply HostProcess(process=demo_process) to consumer;
    apply HostCpuDomain(cpu_domain=DemoCpu) to demo_process;
}
```

And then change our `system.clk` file to run the full system:

```clockwork
use demo::cogs;

system_target single_producer
{
    // Change the box name here too
    box: cogs::FullSystem;
}
```

And that's it!
You now have a complete, single-process two-producer, one-consumer system.

### Adding diagnostics


To add support for diagnostics to a Cog, you must `use @clockwork::clockwork::diagnostics::report` and add a `diagnostics` block to the Cog:

```clockwork
use @clockwork::clockwork::diagnostics::report;

// Cog that has diagnostics
cog MyDiagnosticsCog
{
    execution
    {
        condition my_periodic: time_since_last_exec(500ms);
        execute when: my_periodic;
    }

    diagnostics
    {
        group_id: "fault_injector_b";
        instance_id: "a";
    }
}
```


Then, at the system level you must connect the diagnostics output to the diagnostics bus:

```clockwork
use @clockwork::clockwork::diagnostics::channels::DiagnosticsChannel;

box MyDiagnosticsExampleBox
{
    new cog1: MyDiagnosticsCog;
    connect cog1.diagnostics to DiagnosticsChannel;
}
```

- The Cog diagnostics output is the fixed name `.diagnostics`
- The channel `DiagnosticsChannel` is a current convention

## Next steps

For more detail on the concepts introduced in this tutorial, read:

- [Clockwork schema language reference manual](../reference/schemas.md)
- [Clockwork system composition](../concepts/composition.md)
- [Clockwork execution conditions](../reference/exec_conditions.md)

Don't forget to checkout the [developer tools](../../tools/README.md) to learn more about our collection of Clockwork tools.
