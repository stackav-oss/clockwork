# Clockwork 101

Clockwork is a software framework and infrastructure designed for safety-critical, real-time[^1], constrained hardware systems.
As a software infrastructure, it manages resources–CPU time, GPU time[^2], memory, network bandwidth–and aims to provide reliable, bounded, predictable, and consistent resource utilization.
As a software framework, it provides APIs to user application code but it also constrains the possible ways of writing that user code.
It does this so that it can, as an infrastructure, fulfill those primary goals of safety, reliability, consistency, and bounded resource utilization.
These constraints also enable fully deterministic offline (simulated-time) execution of the user code, which is a critical feature for reproducible simulation and testing.

[^1]: Note that when we use the term "real time" in Clockwork documentation, we most likely mean soft real time, not hard real time, though this is determined primarily by the underlying operating system.

[^2]: The current version of Clockwork does not yet directly manage GPU time, but this is planned for a future release, along with support for other accelerators.

Below we make some comparisons to ROS, another robotics software framework.
This is not because Clockwork inherits anything from ROS directly, but because many software engineers are familiar with ROS and it serves as a useful point of comparison.

This document is intended to provide a very high-level overview of Clockwork.
It is not a complete reference manual, tutorial, or how-to guide, but it does provide a good overview of the system and is a very good first stop for becoming familiar with Clockwork prior to digging into the more detailed documentation.

## Essential concepts

Conceptually, Clockwork can be understood in terms of four related domains:

1. User software components (Cogs)
2. Data (messages, persistent state, configuration, logging)
3. System composition (connecting components together into a data flow graph and mapping that to hardware resources)
4. Scheduling, resource allocation and management

These are not independent à la carte swappable subsystems in Clockwork but are very tightly integrated.
They're unified by sharing a single domain-specific language (DSL) that's used to specify the system within all four of the domains above.
The DSL is also called just "Clockwork" or "the Clockwork DSL", and source files in this language have extension `.clk`.

This document is intended as a high-level overview, so it doesn't show the DSL directly.
Rather, describes the basic properties of the system that are specified in the DSL.
Wherever the text here says that something is "specified", that means it's specified in the DSL.

### Cogs

All user code in Clockwork executes as a _Cog_, which is the fundamental software component.
In many frameworks, the fundamental component is implemented as a class: in ROS it's called a "node" and it's implemented as a C++ or Python class.
A ROS node can have multiple callbacks that execute under different conditions, but all can communicate via state stored within the class instance.

#### Stateless, but with state

In Clockwork, a Cog is not a class but a single callable function.
Fundamentally, Cogs are stateless: they take inputs and produce outputs, and the outputs are pure functions (conceptually) of the inputs.
If the user code is written to be deterministic (which is up to the application developers, not up to Clockwork), then the Cog will always produce the same outputs for a given set of inputs.
This is the basis of Clockwork's ability to execute deterministically and reproducibly in offline simulated-time environments; even the "current time" is an input (and does not change during execution) and so every execution can be made repeatable by replacing real time with simulated time.

That said, most real-world applications need access to some kind of persistent state or memory.
In Clockwork, this is achieved by making persistent state an input (and, optionally, an output) from the stateless Cog function.
This also provides a mechanism for sharing state between Cogs.
In ROS, a node class can share state between its callbacks via member variables of the class.
In Clockwork, that would instead be an instance of state which is connected to multiple Cogs.
Cogs can also have more than one piece of state, allowing developers to carefully segregate which state is needed by which Cog.
State can be provided to each Cog as read-only (immutable) or read-write (mutable).

#### Pub/sub message passing

But state is not the primary mechanism for communicating between Cogs.
That's message passing, which is superior as a communication mechanism to state in most circumstances: it allows for better concurrency, easier independent unit testing and simulation, and better isolation and abstraction between components.
Message passing in Clockwork provides messages with well-defined schemas to a Cog as inputs, and the Cog produces messages as outputs.

> [!WARNING]
> Inputs in Clockwork are by default "latest message" inputs, not "every message" inputs!
> This means that skipping messages to keep up to date with the latest data is the default behavior.
> If you need every message, you must request that specifically.
> See the [input configuration documentation](./concepts/input_views_triggers.md) for more details.

Inputs and outputs are connected to _channels_, which are a logical nexus in the data flow graph.
Channels work in a one-to-many (or, in rare cases, many-to-many) pub/sub (publish/subscribe) style.
A message published to a channel is received by all subscribers on the channel.
Subscribers might be on the same compute instance (CPU or GPU) as the publisher, or they might be on different instances.
Clockwork handles making sure the data gets from the publisher to the subscriber(s) in the most efficient way possible.

A Cog can have as many inputs and outputs as it needs, each connected to different channels.
Each of its inputs is conceptually a _view_ over the messages on that channel; the channel is conceptually an ordered sequence of messages.
The Cog can choose the size of its view onto the channel.
In many cases, it will only want a single message at a time, in which case it can choose a view of size one (the default).
Many use cases benefit from having access to a view over multiple messages; this can be specified for each input.
This view will be a sliding window over the messages on the channel, always showing the last N messages on the channel.
This is implemented efficiently in a zero-copy way.

#### Cogs vs ROS nodes/callbacks

The above paragraph illustrates a massive difference between Cogs in Clockwork and node callbacks in ROS: A ROS callback can only process a single message from a single channel.
If your component needs multiple inputs, as most real-world robotics component do, then it needs multiple callbacks, and those callbacks typically just store a _copy_ of the message (inefficiently) and then later a periodic callback processes all of the stored messages from multiple channels.

**This is entirely unnecessary in Clockwork and is an inefficient anti-pattern**.
You should never need to store messages yourself inside Cog state.
Instead define multiple input views for your Cog, using a view size greater than one if you need a sliding window over the history of messages.
In this way, what was multiple ROS callbacks can become a single Cog in Clockwork, and there is less total code written and less copying, leading to better runtime efficiency.

That said, there may be reasons still to make a ROS node into multiple Cogs.
One reason would be separation of concerns: if a ROS node is doing multiple things together that could be done separately, it would probably provide better modularity, easier testing, and easier comprehension of the system to make it multiple Cogs, sharing whatever state is required or (preferably) passing messages between Cogs.
**Message passing is extremely efficient** in Clockwork, without any separate serialization/deserialization step and it's entirely zero-copy whenever the publisher and subscriber execute on the same compute instance (CPU).

So if you have an existing ROS node that does A, then B, then C, then D in a sequence, you might gain better isolation (no unintentional sharing), better modularity, easier simulation/testing, and easier overall code maintenance if you make that four different Cogs that communicate via message passing.
It might even run faster rather than slower, if Clockwork can execute those steps concurrently or manage memory more efficiently.

#### Configuration

In addition to persistent state and message passing, Cogs can also take configuration data as a read-only input.
Configuration is loaded from disk, currently using textproto format, and provided to the Cog on every execution as an input.
It's provided in an efficient way, so there's no need to copy from configuration data into persistent state; just use the configuration data itself as the persistent source.

#### Initialization

If you're coming from ROS or other robotics infrastructure, you're probably wondering how Cogs are initialized.
But this question makes no sense; Cogs are stateless, so there's nothing to initialize.
State is declared, allocated, and initialized externally to the Cogs.

But, state absolutely can be initialized with functions that run on startup.
If the state is declared such that it can be default-initialized and that's the correct startup state, then nothing else needs to be done.
But if it's more complicated than that, such as needing access to runtime configuration data in order to initialize the state, or perhaps the state holds something like a GPU context or file handle, then an initialization function can be written for that state.

State initialization functions in Clockwork are also Cogs.
They're Cogs marked specifically as "init Cogs", and they execute only once, at startup.
They take an uninitialized instance of the state as input, and can then modify the state.
They can also take other state (initialized by other init Cogs) as input, and/or configuration data, just like any other Cog.

#### Shutdown

Again, Cogs are stateless; there is no shutdown function.
State will be destroyed during shutdown, so if it has open operating system resources, it can release those resources in its destructor.

#### When do Cogs execute?

The short answer is: whenever you want them to.
Cogs can be periodic, input-triggered (i.e., executing when an input becomes available), or a combination of both.
Execution conditions are specified in a simple expression language for each Cog.
This is complex enough to warrant a [document of its own](reference/exec_conditions.md).

**It is recommended to make Cogs input-triggered whenever possible**.
This will lead to the most efficient scheduling and lowest latency, especially in a future version of Clockwork, once we implement static scheduling.
Very often, developers coming from other robotics infrastructure (like ROS) will tend to prefer periodic execution, using input-triggered callbacks just to store the input messages but processing them all in the periodic method.
This feels "safe" because they know the periodic method will execute even if the input messages are late or missing.
But Clockwork has a better way to do that: the [`time_since_last_exec`](reference/exec_conditions.md) condition can be used to ensure that even if it receives no inputs, the Cog still executes at some minimum frequency.
This allows a Cog to implement "input timeout" behaviors very easily, without resorting to periodic triggering.

This bears repeating: **Instead of periodic execution as a way to be robust to late or missing inputs, use input-triggered execution with a timeout.** Use periodic execution only if it's important to run at a very specific frequency for some reason.

#### Do Cogs execute concurrently?

Yes, Cogs can execute concurrently with other Cogs, so long as they either do not share state, or the state shared between them is read-only (immutable) in both Cogs.
If any Cog mutates an instance of state, it cannot execute concurrently with any other Cog that accesses that state.
Clockwork handles this synchronization automatically for the Cogs; users do not need to do anything to get this behavior.

In the current version of Clockwork, an individual Cog instance cannot execute concurrently with itself (reentrancy), even if it's stateless.
In future versions of Clockwork, we will likely relax this requirement, so that a Cog which is stateless or has only read-only state can execute concurrently with itself, so don't rely on this constraint.

**Never use global or static data in your Cog functions**, because even though Cog _instances_ cannot run concurrently with themselves, a single Cog _function_ can be instantiated multiple times, and those instances can run concurrently.
Use state to store all data in non-global, non-static ways that are friendly to multiple instantiation.
By extension, this means that **singleton classes should not be used in user code**, as these are inherently global and will interfere with concurrency and also determinism and reproducibility.

#### What about unit tests for cogs?

Clockwork optionally generates a test wrapper for each cog that provides an API for manipulating the configuration and state, sending messages to the cog's input channels and receiving messages published on the cog's output channels.
See the [cog unit tests reference](./reference/cog_unit_tests.md) for more details.

### Data

In the section above on Cogs, we already mentioned indirectly most of what you need to know about data.
In Clockwork, data comes primarily in persistent state, pub/sub messages, and configuration data, all mentioned above.

But all of those share a single system for specifying the data schemas (types) and for serializing or representing that data in various forms.
This is based on the Clockwork DSL, which includes a schema language component.
State, messages, and configuration are all specified using this same DSL.

Clockwork schemas support rich data types, including strong types like `Duration` and `SyncTime`, strong unit types for physical quantities, and strong enums.
It also supports composition (schemas containing schemas) and parameterized schemas (aka generic types or template types).

It does _not_ support reference semantics, such as schemas with pointers to schemas.
This means that cyclic or recursive data structures cannot be directly represented in schemas.
(Arbitrary graphs, including cycles, can be represented with adjacency lists in Clockwork.)
It does not directly support runtime polymorphism, though union types (aka "variants" in C++) will be supported in a future release, providing a constrained form of polymorphism.

There is a single common schema language, but there are multiple backends for representing and interfacing to schema data.
Unlike many schema languages, including ROS's schema language, the Clockwork schema language is a _representation-independent_ or multi-representation language.
(The [concepts guide](concepts/schema_repr_iface.md) has the nitty gritty on what this all means.)

More information about the schema language can be found in the [detailed documentation](reference/schemas.md).

#### Tachyon

The primary representation for Clockwork data is called _Tachyon_, which is a format specific to Clockwork.
Tachyon is designed, as the name suggests, for speed, specifically in C++ code.
Accessing Tachyon data is just as fast as accessing data in native C++ data structures (such as `std::vector`), and there is no need for a separate serialize/deserialize step!
C++ code can access Tachyon data, both read and write, directly in serialized form, at native access speed.
This is part of what makes message passing in Clockwork so efficient compared to systems like ROS.

This performance does come at some cost.
Because Clockwork is designed for strictly bounded-resource execution, consistent resource utilization, and reliability, so is Tachyon.
This means that every container in Tachyon must have a maximum size; Tachyon supports variably-sized containers, but they must have a size bound.
And a Tachyon container, even if variable-sized, **will always use worst-case size**, according to its size bound.
This gives us the resource bounding and consistency we set out to achieve.

The philosophy here can be succinctly summarized as: "The worst case is the only case that matters."
This philosophy pervades much of Clockwork's design, but it's most apparent in Tachyon.

##### Portability, Python, and other languages

Despite providing zero-serialization direct access at native speed in C++, Tachyon is also a well-defined wire format with representations specified down to the bit, which means it's portable across compilers, platforms, and languages.
We therefore also have a pure Python implementation of Tachyon (no C++ bindings required), and other languages can be supported in the future.
The Python implementation is not as efficient; it has a separate serialization and deserialization step.

#### Tap and Tappy

Tachyon is the underlying _representation_ or wire format, but it's not a programming or data access API.
The _interface_ to Tachyon data in C++ is called _Tap_.
The Tap API is designed to provide slightly abstracted but native-performance access to Tachyon data; it's more or less a "direct tap" into the data, but still provides a little bit of abstraction to prevent user code from relying on the details of Tachyon.
The full C++ type underlying a Tap interface to a Tachyon representation of a schema called `Foo` would be `Tap<Tachyon<Foo>>`.
This is a lot to type, so we have a shortcut: `Tappy<Foo>` is an alias for `Tap<Tachyon<Foo>>`.
This shorthand works both in C++ and in the Clockwork DSL.

Tap provides a minimal STL-compatible interface to containers within the Tachyon data, including ranges and views, allowing direct and efficient use of STL algorithms.
See the [full Tap API documentation](reference/tap_api.md) for more details on Tap.

#### Protobuf (and TextProto)

There is also a Protobuf backend for the Clockwork schema language.
This generates a Protobuf schema from the Clockwork schema.
Protobuf does not support all of the Clockwork features, such as strong types and parameterization (generics), so this comes with some limitations or loss of fidelity.
A generic Clockwork schema can be represented in Protobuf, but a specific instantiation must be chosen.
Each instantiation becomes a different Protobuf schema.

Once the Protobuf schema is generated, it can do everything Protobuf can do.
This means any Protobuf-supported language can be used for accessing the data, and both binary and text formats are supported.
The text format is used widely in Clockwork for storing configuration data.

Automatic conversion from Protobuf to Clockwork data is also provided.
So for example, when loading configuration data in textproto format, this is automatically converted to Tachyon and user code accesses the Protobuf data via a Tappy interface.

See [the Protobuf representation documentation](reference/repr_iface.md#proto_target-blocks) for details on how to use Protobuf in Clockwork.

#### Backward compatibility

Unlike ROS, the Clockwork schema language is designed for backward compatibility as schemas evolve over time, but this has not yet been fully implemented in the Clockwork MVP.
It will follow as one of the first post-MVP features.

However, even in the MVP, when Clockwork data is logged, it's logged along with schema metadata that allows the pure Python implementation of Tachyon to deserialize and introspect that data even without access to the specific code version that wrote the log.
This makes logs self-describing and always deserializable, given only the PyTachy (Python Tachyon) library.

However, offboard applications should make use of this capability with some caution, because even though you can deserialize and introspect the data, allowing you to be robust to schema changes, there may also be _semantic_ changes in the data that require modification of the offboard processes.
Clockwork cannot encode semantic meaning of schema data; it's up to onboard application developers to coordinate with offboard developers whenever semantics of schema data change.
Offboard code can introspect the version numbers of Clockwork schemas in a log to be able to handle semantic changes correctly.

### System composition

Cogs are functions, with state, config, inputs, and outputs.
Those things are specified in the schema language.
To make a system, you need to connect things to those state, config, and pub/sub endpoints on the Cogs, which connects Cogs together into a system, and you need to specify on what hardware (CPU instance) those things are hosted.
This is referred to in Clockwork as system composition.

It's important to note that when specifying and writing a Cog function, you don't specify or even know, at runtime, what _channel_ your message inputs and outputs connect to, what file your config was loaded from, or even what other Cogs share your persistent state.
The Cog simply specifies the _schema_ (data type) of each of its endpoints.
Connecting those endpoints to channels, for example, happens outside of the Cog itself.
And it's those connections that truly define the system.

In particular, a Cog does not know what process it runs in, what CPU that process runs on, what processes and CPUs its inputs come from, and what processes and CPUs its outputs go to.
It only has the data.
Clockwork manages getting the data from where it's produced to where it's needed in the most efficient way.

To be included in a system, a Cog _class_ (which corresponds to a Cog definition in the DSL and a single C++ function) must be instantiated into a Cog _instance_.
Each Cog class/function can be instantiated multiple times, and each instance can be connected to different input and output channels, configuration, and persistent state.
Or, they can share some channels, configuration, and state.
The Cog functions don't actually "know" this, because it happens well after Cog definition, during system composition.

Cogs can be grouped together into logical modules called Boxes, and Boxes can be grouped hierarchically (Boxes of Boxes) to arbitrary nesting depth.
These logical modules don't affect anything about how the system functions directly.
It's just a way of organizing a complex system, and specifically reusable clusters of Cogs with connections between can be specified as Boxes, and those Boxes can then be instantiated multiple times.
So for example, a camera pipeline could be assembled once and then instantiated once for each camera.

Boxes do not need to be directly related to processes or CPUs, but they can be.
The most common case is to assemble everything you want to run in a single process into a Box, and then specify a process for that Box, but this isn't the only way.
Processes can also be assigned to individual Cog instances within Boxes; this is just more verbose.
But this flexibility is useful for easily handling different system configurations with a minimum of repeated boilerplate.

Processes are then independently allocated to "CPU Domains", which is the Clockwork term for a particular operating system instance running on some piece of hardware.
Again, it's easy to move processes around with small DSL changes; this does not require writing or even recompiling C++ code.

See the [composition concepts guide](concepts/composition.md) for more information.

### Scheduling and resource allocation

#### CPU scheduling

Clockwork is responsible for executing Cog code at runtime.
In the Clockwork MVP, we have taken a fairly simple and traditional, ROS-like approach to this, where each process has a thread pool for executing Cogs on a first-ready-first-executed basis.
Clockwork decides on "readiness" based on the Cog's execution conditions, and it will automatically run things concurrently when they do not share state or share only immutable state.

Fundamentally, the scheduler in the Clockwork MVP is Linux, which underlies that thread pool.
After the MVP release, we intend to explore the possibility of static scheduling, moving scheduling largely into userspace to get more consistency and determinism.
But this has not yet been implemented.

#### GPU scheduling

In the Clockwork MVP, we do not directly manage GPU resources.
GPU code executes as a normal CPU Cog and has its own GPU context.
Clockwork does nothing to coordinate different GPU contexts in different processes running on the same GPU.

Post-MVP, as part of the move toward more userspace scheduling, we will make GPU Cogs a first-class feature, taking over management of the GPU context from user code and coordinating access to the GPU between different Cogs/processes.

#### Memory allocation

All user code that allocates memory should be doing so using PMR resources.
Clockwork provides the memory resources to the Cog.
In the MVP, these memory resources are actually unbounded new/delete resources, though a bound is specified in the DSL.
Post-MVP, we will begin enforcing these bounds and using different types of allocators.
Until then, make sure all allocation is done via a PMR resource obtained from Clockwork rather than created within the Cog.

## Next steps

If you need to know how to actually define schemas and Cogs and assemble a Clockwork system, the [Hello, World! tutorial](tutorial/helloworld.md) is the best place to head next.
