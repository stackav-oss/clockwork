# Clockwork system composition

Clockwork executes Cogs always within the context of a _system_.
A system includes one or more Cogs in one or more processes spread over one or more CPUs.
The endpoints of the Cogs are connected to channels, state, and config files, forming a data flow graph.
Channels can also be connected to UDP ports (for sensor drivers) and to the event or telemetry loggers for data recording.
Assembling that entire definition of the system is called _system composition._

Systems are defined in the [Clockwork domain-specific language (DSL)](../../dsl/README.md).
The Clockwork compiler compiles the system definition into a set of system description files.
To execute the Clockwork system, you must have both the compiled binary executables, and those system description files.
Note that a single compiled binary can be used in more than one system, using different system description files.
Many changes in system composition can be made without forcing C++ code to be recompiled, but in all cases the DSL compiler will need to re-execute to generate a new set of system description files.

## Note about variable name scope

All definitions within a Cog or Box can be thought of being in the same "scope".
This means that all names in a Cog or Box, including those defined in `state`, `inputs`, and `outputs` must be unique.

## Classes, instances, and Boxes

Many constructs in the DSL, including Cogs, have both _classes_ and _instances_. For example, when you define a Cog in the DSL:

```clockwork
// Hello, world!
cog HelloWorld
{
    // ...
}
```

You have defined a Cog _class_, not a Cog instance. Cog classes can be compiled into executables, but you can't execute a Cog class; you must create a Cog instance to execute:

```clockwork
box HelloBox
{
    new hello_world: HelloWorld;
}
```

Here we've defined a Box called HelloBox which contains an _instance_ of the HelloCog class called `hello_world`.
HelloBox is itself a box _class_ (or, technically, a box _template_, inside the DSL compiler, but as a user think of it as a class), and that means it can be instantiated:

```clockwork
box HelloSystem
{
    new hello_box: HelloBox;
}
```

Boxes can be nested to arbitrary depth in this way, and of course you can instantiate things multiple times. Each instance of a thing is an independent entity, just like instances of a class in any object-oriented language:

```clockwork
box HelloBox
{
    new hello_world_1: HelloWorld;
    new hello_world_2: HelloWorld;
}

box HelloSystem
{
    new hello_box_1: HelloBox;
    new hello_box_2: HelloBox;
}
```

Here there are four distinct Cog instances in each HelloSystem. They each have unique identifying instance names also:

- `hello_box_1.hello_world_1`
- `hello_box_1.hello_world_2`
- `hello_box_2.hello_world_1`
- `hello_box_2.hello_world_2`

In addition to having instance names, they'll also have unique UUIDs to identify them.
There's nothing shared between these four Cog instances other than the C++ code that implements their Cog functions.
They can each have separate input/output connections, separate state, and separate configuration.
They can run in the same process or in different processes, same CPU or different CPUs.

Note that `HelloSystem` is not yet instantiated.
We'll get to that soon. For now, remember one important rule:

> Things (Cogs, state, config, UDP ports) must be instantiated within a Box to be used as part of a system.

### How to use Boxes

_Boxes_ are the main way we organize complex systems in Clockwork.
Each box can be thought of as a reusable system module, and it can be stamped out multiple times in the same system or in different systems.
For example, a camera processing pipeline can be assembled into a single Box, and that Box can be stamped out once for each camera.
Systems are composed hierarchically from Boxes in this way, without any limit on nesting depth.
The same Box definition can be reused not only multiple times within one system, but can also be reused in different systems.
And of course, Cog classes can be reused in multiple Boxes, and multiple systems.

## Channels and connections

Channels are a little different than Cogs and Boxes.
There are not separate channel classes and instances; there are just channels.
Channels have a sort of global unique identity that remains the same even across different Boxes and systems.
However, the way each channel connects to different things is very much dependent on Boxes and systems.
Channels cannot be connected to Cog _classes_; they can only be connected to cog _instances_, and that happens inside Boxes.

```clockwork
cog HelloWorld
{
    inputs
    {
        hello: Tappy<HelloMsg>;
    }
}

channel HelloChan1
{
    message_type: Tachyon<HelloMsg>;
    max_num_messages: 10;
}

channel HelloChan2
{
    message_type: Tachyon<HelloMsg>;
    max_num_messages: 10;
}

box HelloBox
{
    // Instantiate our cogs
    new hello_world_1: HelloWorld;
    new hello_world_2: HelloWorld;

    // Connect their inputs
    connect HelloChan1 to hello_world_1.hello;
    connect HelloChan2 to hello_world_2.hello;
}
```

Here we've connected a different channel to each Cog instance, but they could also be connected to the same channel.
Note that we connect to a specific _endpoint_ within the Cog instance, not just to the Cog.
A Cog could have multiple inputs of the same message type, so you always need to specify which endpoint you mean.

Note that we don't instantiate the channels.
They are defined outside of Boxes and are ready to connect.
The connections always happen inside of Boxes, but the connections don't need to happen in the same Box that instantiates the Cogs.
This is entirely valid as well:

```clockwork
box HelloBox
{
    new hello_world_1: HelloWorld;
    new hello_world_2: HelloWorld;
}

box OuterBox
{
    new inner: HelloBox;

    connect HelloChan1 to inner.hello_world_1.hello;
    connect HelloChan2 to inner.hello_world_2.hello;
}
```

The ability to defer making connections to a higher-level Box makes certain system composition reuse patterns easier.
The DSL allows you to reach through multiple layers of Boxes to make connections, so you can put the connection decisions at the right level of the hierarchy.

What's the "right" level?
There's no single answer to that question.
You make the connections when you're at the level of composition where you know what the right connections are.
Don't fuss too much about this decision; it's easy to change this stuff later.
The worst consequence of doing it wrong is that you might have to copy/paste your Box definitions to adapt them to a different system configuration, if you've made the connections prematurely.

### Note about definition location

Channels must be defined in a `.clk` file other than the one which contains the schema definition of the associated message type.

### Connections are directional

The connection syntax `connect X to Y` is intentionally directional.
Above we show a connection _from_ a channel _to_ an input endpoint.
Cogs can also have output endpoints, and the direction should be reversed in that case; if `HelloWorld` had an output endpoint called `output`, that would be connected like this:

```clockwork
connect hello_world_1.output to HelloChan1;
```

If you get this wrong, the compiler will tell you; it'll never connect an output as if it were an input.

## State, config, and memory resources

Cogs can also have state, config, and memory resource endpoints.
These must be instantiated and connected inside of Boxes:

```clockwork
box HelloBox
{
    new hello_world_1: HelloWorld;
    new hello_world_2: HelloWorld;

    new mem_res: HeapMemory(max_size=1'000'000);
    new hello_state: State(representation=Tachyon<HelloState>);
    new hello_config_1: SerializedDataFile(representation=Protobuf<HelloConfig>,  path="path/to/my/config1.txtpb");
    new hello_config_2: SerializedDataFile(representation=Protobuf<HelloConfig>,  path="path/to/my/config2.txtpb");

    // Here we use the same memory resource for both cogs:
    connect mem_res to hello_world_1.memory;
    connect mem_res to hello_world_2.memory;

    // And they also share state:
    connect hello_state to hello_world_1.state;
    connect hello_state to hello_world_2.state;

    // But they have different config data:
    connect hello_config_1 to hello_world_1.config;
    connect hello_config_2 to hello_world_2.config;
}
```

Note that these connections could also go between different sub-Boxes; you can connect config instantiated in one Box to Cogs instantiated in another.
There just needs to be a single Box that instantiates both sub-Boxes, and the connection can be made there.

The directionality for all of these endpoint types is `connect <resource> to <cog endpoint>`.

## Casings and executables

Cog and schema _classes_ must be compiled into C++ libraries.
Those are created through `cpp_target` blocks in the DSL.
A `cpp_target` block corresponds to a `cc_library` in Bazel.

In order to make use of these class libraries, they need to be compiled into an executable:

```clockwork
cpp_executable helloworld_exe
{
    casing
    {
        interface Tap<Tachyon<hellomsg::HelloMsg>>;
        cog hellocog::HelloCog;
    }
}
```

An executable includes a _Casing_, which is a little like a Box, except instead of holding _instances_ of Cogs, state, etc., it contains compiled _classes_ of Cog schema representations and schema interfaces.
Those classes must also be part of some `cpp_target` somewhere (probably multiple `cpp_target`s).
The casing links all of these different compiled classes into a form that can be executed as part of a system.

As of right now, each executable can have only a single casing, and each casing must be inside a single executable.
In the future this may change; in particular we may support multiple casings in a single executable.
This would reduce some boilerplate.

Note that neither casings nor executables have anything to do with Boxes or instances of anything.
They are only compiled classes, waiting to be instantiated.
An executable cannot execute on its own; it needs to be part of a system.
Specifically, if you try to just execute it, it'll ask for a path to a _process description file_ as a command line argument.
To get that process description file, you need a system.
We're almost ready to create one.

## Executables and processes

Executables contain _classes_.
Processes contain _instances_.
We saw above how to define an executable; now we need a process:

```clockwork
box HelloBox
{
    new hello_cog: HelloWorld;
    new hello_process: Process(executable=helloworld_exe)

    apply HostProcess(process=hello_process) to hello_cog;
}
```

So this shows first how to instantiate a process.
A process must be instantiated in a Box, and it needs to specify the executable that runs that process.

### Matching executables to processes

The executable for a process must contain compiled Cog and schema _classes_ that correspond to all of the _instances_ that run in that process.
It can contain more than the process needs; if it contains unused code, that's no problem.
That allows executables to be shared by multiple processes, each process with a different set of instantiated things.
In principle, an entire system of thousands of Cog and schema classes could be compiled into a single executable, and every process in the system could use the same executable.
None of them would likely use _all_ of the executable.
That's just fine.
In general, though, executables are likely to contain only what the processes that use them need, but Clockwork does not enforce this.
For sim in particular, it's very useful to have general-purpose executable that can run many different system configurations.

### Hosting instances in processes via policies

Now, what is a process, exactly?
It's a way to host instances of things.
Instances can include Cogs, state, config, and a few other things (like UDP sockets).
Every instantiated thing in a system needs to have a process to host that.

We specify this using _policies_ in the DSL.
Policies are a general-purpose mechanism in the Clockwork DSL, also used for things like logging configuration.
The syntax shown here isn't specific to processes; this is general policy syntax.
What's specific to processes is the exact policy we're specifying: the `HostProcess` policy, which is a policy that can be applied to instances of things to specify what process hosts them.

Separating the process instantiation (the `new hello_process` line above) from the specification of what runs in that process (`apply HostProcess...`) allows us to be very flexible in reconfiguring the system in different ways with less copy/paste boilerplate.
But if we had to individually specify the policy for every instance of a Cog, state, or config, that would be a lot of boilerplate.
The policy mechanism builds in a compositional shorthand for this though:

```clockwork
box SuperBox
{
    new sub_box1: SubBox1;
    new sub_box2: SubBox2;
    new process: Process(executable=...);
    apply HostProcess(process=process) in sub_box1;
    apply HostProcess(process=process) in sub_box2;
}
```

Here the syntax is slightly different: `apply ... in ...` instead of `apply ... to ...`.
You use `to` to apply a policy to a _single_ instance, and `in` to apply a policy recursively to everything inside a Box instance.
So if you already have a Box that has a bunch of related Cogs, state, config, and memory resources and you want to run all of that in one process, you can easily do that all at once with an `apply ... in ...` statement.
This works even if `SubBox` has many layers of Boxes within it; the policy applies recursively.

Note that above we're actually creating two different Boxes that perhaps weren't "meant" to be executed together, but we're executing them in the same process, without having to change any of the Box definitions or Cog code.
Cogs are defined and implemented without knowing anything about their host execution process, and they can be freely combined.

## Processes and CPUs

We're close to having a system, but not quite there yet.
Clockwork is built to make distributed, multi-CPU systems, and it needs to know where everything runs.
So we need to tell it about our hardware layout also.

```clockwork
cpu_domain Cpu1;
cpu_domain Cpu2;

box SuperBox
{
    new sub_box1: SubBox1;
    new sub_box2: SubBox2;
    new process1: Process(executable=...);
    new process2: Process(executable=...);
    apply HostProcess(process=process1) in sub_box1;
    apply HostProcess(process=process2) in sub_box2;

    apply HostCpuDomain(cpu_domain=Cpu1) to process1;
    apply HostCpuDomain(cpu_domain=Cpu2) to process2;
}
```

CPUs are defined as "CPU Domains", which could be physical CPUs but could also be virtual machines or partitioned CPU cores.
CPU domains are like channels: they're sort of "global" and are not instantiated inside of Boxes.
But you reference them inside of Boxes when specifying the HostCpuDomain policy for a process, as shown above.

Note that HostCpuDomain is just a normal policy; it will bind to processes (but nothing else).
So you can use either `apply ... to ...` to apply it one process at a time, as shown above, or `apply ... in ...` to apply it recursively to a Box containing many processes.

The example above defines two processes, one running on each CPU.

## CPU networking

The system shown above can't actually communicate anything between `process1` and `process2`, and if there were any data flow between them the compiler would complain.
That's because Clockwork doesn't know how Cpu1 connects to Cpu2.
We need to tell it about networking:

```clockwork
cpu_domain Cpu1;
cpu_domain Cpu2;

ethernet Lan1
{
    ipv4_address 1.2.3.4: Cpu1
    {
        bridge_ports: 16000 - 18000;
    }
    ipv4_address 1.2.3.5: Cpu2
    {
        bridge_ports: 16000 - 18000;
    }
}
```

Here we define an Ethernet network and tell it what address each CPU domain has on that network.
We also give it a range of TCP ports which it can allocate for connections between those CPUs.
It will auto-assign ports within this range as it needs to in order to create connections between processes on different CPUs.

## Defining systems

We are, finally, ready to instantiate a system in Clockwork:

```clockwork
system_target hello_system
{
    box: HelloSystem;
}
```

(Not shown: the definition of HelloSystem box, cpu_domains, ethernet, etc.)

Once you've laid the groundwork, meaning you've defined all your Cog and schema classes, included them in `cpp_target`s and `cpp_executables`, instantiated them in Boxes, defined your CPU domains and the LAN connections between them, and applied `HostProcess` and `HostCpuDomain` policies to specify where to run everything, now, you just create a `system_target` that names that one Box containing the whole system.

From that `system_target`, the Clockwork compiler will generate a whole lot of data files with extension `.tachyon`.
This does _not_ generate any C++ code, or Python code, or anything like that.
It only generates Tachyon files that describe the "compiled" system.
This includes:

- Process description files (one per process) -- `*.{process_name}.tachyon` where `{process_name}` is the name of your process.
- Logger configuration files (two per CPU domain: one event, one telemetry)
- Bridge configuration files (one per CPU domain)
- Diagnostics configuration files (one per Diagnostics aggregator)
- Event manager configuration files (one per event logging manager)

All of those files are generated in the same directory as the Clockwork module containing the `system_target`, using a rather verbose naming system that ensures that they are all unique.
The Clockwork tooling will keep the associated Bazel targets up to date.
For a large system, there will be quite a few `outs` added to the Bazel target for that Clockwork module.

## Running single-process systems

When an executable consists of a single process executing on a single CPU domain, you can run them manually.
To run an executable you must pass a process description file (`*.{process_name}.tachyon`).
You can find the process description filename by looking at the `outs` of the `clk` Bazel target that contains your `system_target`
When you pass it to the executable you need to reference it's location in the Bazel outputs directory at the same path as your `clk` Bazel target that contains the `system_target` (e.g. `bazel-bin/autonomy/...`)

> [!NOTE]
> You must run `rm -rf /dev/shm/clockwork/pinion/pub` in between invocations of executables.
> If you forget to do this you will see an error similar to `ERROR platforms/clockwork/scaffolding/channels.cc:76 [1729647772.755608156] Could not create channel '0b3b5de1-eda3-5ed2-a43a-9ebf7c2d698d': dirty`

## Running multi-process systems

Systems that comprise multiple processes can be cumbersome to run by hand.
One needs to track down all of the relevant executables and process description files and wrap them up in a shell script or similar.
You can avoid this complexity by using simplelaunch: a lightweight process supervisor included with Clockwork.

When you have a `clk` build target that includes a `system_target`, `clk-deps` will generate several useful build targets for your system.
Among them will be targets named `:<name>.textproto` for each CPU domain involved in your system.
You can use these targets in conjunction with simplelaunch to start and monitor all of the processes attached to a particular CPU domain with minimal fuss.

For example, consider the following system with two processes declared to run on `HostA`.
After running `clk-deps` the build file will have a target called `hosta.textproto`.
This file contains entries for `process1` and `process2`.
These entries are not unlike systemd unit files with `Type=simple`.
They indicate which executable to run and include a list of command line arguments.

```clockwork
cpu_domain HostA;

box SuperBox
{
    new sub_box1: SubBox1;
    new sub_box2: SubBox2;
    new process1: Process(executable=...);
    new process2: Process(executable=...);
    apply HostProcess(process=process1) in sub_box1;
    apply HostProcess(process=process2) in sub_box2;

    apply HostCpuDomain(cpu_domain=HostA) to process1;
    apply HostCpuDomain(cpu_domain=HostA) to process2;
}

system_target super_system
{
    box: SuperBox;
}
```

You can generate an executable target from `hosta.textproto` using the `simplelaunch_runner` Bazel rule.
You can then launch everything with `bazel run //path/to/your/module:hosta_runner`.
Alternatively, you can pass `:hosta_runner` as an entrypoint to your preferred rules for distributable binaries.

```bazel
load("//jewels/simplelaunch:rules.bzl", "simplelaunch_runner")

simplelaunch_runner(
    name = "hosta_runner",
    config = ":hosta.textproto",
)
```

## Running multi-node systems

Systems can also span multiple computers.
Consider the simple system below with a producer and consumer Cog that are placed in separate CPU domains.
You can specify that the domains are mutually reachable by including them in the same `ethernet` block (see _CPU Networking_ above for details).

```clockwork
// Channel for HelloMsg messages
channel HelloChan
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

cog ProducerCog
{
    outputs
    {
        hello: Tappy<hellomsg::HelloMsg>;
    }
    ...
}

cog ConsumerCog
{
    inputs
    {
        hello: Tappy<hellomsg::HelloMsg>;
    }
    ...
}

cpu_domain HostA;
cpu_domain HostB;

ethernet LocalNetwork
{
    ipv4_address 10.0.0.10: HostA
    {
        bridge_ports: 16000 - 18000;
    }
    ipv4_address 10.0.0.20: HostB
    {
        bridge_ports: 16000 - 18000;
    }
}

box ProducerBox
{
    new producer_cog: ProducerCog;
    connect producer_cog.hello to HelloChan;
}

box ConsumerBox
{
    new consumer_cog: ConsumerCog;
    connect HelloChan to consumer_cog.hello;
}

box SystemBox
{
    new producer_box: ProducerBox;
    new consumer_box: ConsumerBox;
    new process1: Process(executable=...);
    new process2: Process(executable=...);
    apply HostProcess(process=process1) in producer_box;
    apply HostProcess(process=process2) in consumer_box;

    apply HostCpuDomain(cpu_domain=HostA) to process1;
    apply HostCpuDomain(cpu_domain=HostB) to process2;
}

system_target the_system
{
    box: SystemBox;
}
```

Channels between Cogs that are placed in the same CPU domain can communicate using shared memory.
This is, of course, not possible when the producer and consumer(s) of a channel exist on CPU domains that are only connected to each other by an IP network.
To facilitate this communication pattern, Clockwork will set up shared memory channels on each incident CPU domain for such channels.
These shared memory channels are then glued together by a process called the _TCP bridge_ that runs on each CPU domain.
For each CPU domain involved in inter-node communications the compiler will generate a configuration file that includes settings for the TCP bridge.
Each channel will be allocated a port number in the ranges provided in the `ethernet` block.
This configuration and the TCP bridge binary (with a matching simplelaunch process configuration file) will be included as data dependencies in the `merge_simplelaunch_config` rules generated for each CPU domain by `clk-deps`.
When you launch the simplelaunch runner on each node (see _Running Multi-Process Systems_ above), the TCP bridges will automatically negotiate connectivity and messages should be delivered from the producer to the consumer as if they were using a shared memory buffer.
For more details about the TCP bridge see [this reference](../reference/tcp_bridge.md).
For notes on debugging inter-node message delivery, see the TCP bridge section of the [troubleshooting guide](../troubleshooting.md).
