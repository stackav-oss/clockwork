# Clockwork Inter-Cog Communication Architecture

This document describes the overall architecture for communication between Cogs within the Clockwork system.
It does not cover communication between Cogs and components or devices outside of Clockwork, such as hardware and sensors.

There are two inter-Cog communication mechanisms in Clockwork: message passing and shared state.
Of these, message passing is the primary and generally preferred mechanisms; shared state should only be used when message passing is not suitable.

Note that this is a high-level architectural overview only and does not cover the details of the implementation or APIs.

## Message Passing

Message passing is the primary way Cogs communicate with other Cogs in Clockwork, whether they are in the same process or different processes and whether on the same machine or different machines.
In fact the Cog user code does not need to know whether the other Cog is in the same process or machine or not; this is handled transparently by Clockwork.

### Message passing is asynchronous

Message passing is always **asynchronous** and **non-blocking** in Clockwork.
This is a critical and fundamental architectural aspect of Clockwork, and while it constrains the possible ways of implementing user code, it provides significant benefits.
Most important of these is that it enables reliable and reproducible offline execution in simulated-time environments.
It also enables more efficient resource scheduling and ahead-of-time scheduling (aka static scheduling).

By asynchronous and non-blocking we mean this: When a Cog function executes, it receives some set of input messages, processes them, and then may produce some set of output messages.
For the entire duration of that execution, it only has access to the input messages it received at the start of the execution.
It cannot block waiting for new messages to arrive or even query whether new messages have arrived.
The state of the world after the start of its execution is effectively invisible and frozen (from its perspective) for the duration of that execution.
Similarly, while it may populate its output messages at any time during its execution, those messages are invisible to other Cogs until the execution completes.

In this way, each Cog execution can be fully deterministic and reproducible[^note-determinism], as it only depends on the input messages it received at the start of its execution.
In effect, the Cog execution is hermetically sealed from the outside world and cannot be influenced by any other Cogs nor can it influence them until it completes.
All interactions between Cogs happen _between_ executions, not during them.

[^note-determinism]:
    This design enables fully deterministic execution, but the Cog function must be written in such a way that it does not depend on anything other than the input messages and state in order to actually be deterministic.
    If you write a Cog function that uses a random number generator, for example, it will not be deterministic.

### Delivery is not guaranteed

Clockwork does not make any guarantees about the delivery of messages.
The exact details of what might cause a failed delivery depend on the system configuration and where the producer and consumer are executing.
But these details should never be relied upon by the Cog code, which must always assume that any message delivery may fail.

Ordering is, however, guaranteed for all Cog-to-Cog messages, in that the receiving Cog will never receive messages from a single producer in a different order than they were produced.
Messages may be dropped from the sequence but the sequence will not be reordered.

Almost all channels in Clockwork are single-producer, but for multi-producer channels, no guarantees about inter-producer message ordering are provided, only that the messages from each producer will be delivered in the order they were produced by that producer.

### Strong typing with schemas and wire format independence

Cogs declare the format of their input and output messages using Clockwork schemas, which are strongly typed and versioned.
Cogs do not have control over the wire format (byte layout) of the messages, and all serialization and deserialization is handled by Clockwork.

In fact, the primary wire format in Clockwork, Tachyon, is designed for fast in-place access without separate serialization and deserialization steps.
However, because this is abstracted from the Cogs, Clockwork can convert between Tachyon and other wire formats without needing to change the Cog code.

### Transport independence

Clockwork has multiple transport mechanisms available and automatically selects the best one for each producer-consumer link.
Cog code does not depend on the transport mechanism and doesn't need to change when the mechanism changes.

The primary transport mechanism in Clockwork is called Pinion, and is zero-copy, zero-allocation, and zero-serialization.
It uses host shared memory to transfer messages, with all allocation and sizing handled at system startup.

Shared memory, however, only works within a single CPU domain.
When communicating between machines or memory spaces, Clockwork uses an appropriate transport mechanism automatically based on the system configuration.

## Shared (and unshared) state

The other communication mechanism in Clockwork is shared state.
Shared state is much more limited than message passing, but it follows similar determinism-oriented principles.

Not all state in Clockwork is shared, but the Cog does not inherently know whether its state is shared or not.
Cog state is owned by Clockwork and provided to the Cog on each execution as a class instance.

State can only be shared between Cogs which execute in the same host process.
(In the future, we may add the ability to share state between different processes on the same CPU domain via shared memory, but this is not currently supported.)

### State schemas

Clockwork's primary state mechanism uses the same Clockwork schema system as message passing.
This should be used whenever possible, but some types of data cannot be represented in schemas, and for this arbitrary C++ state is also supported.
Even in the case of C++ state classes, it is the Clockwork infrastructure that allocates and owns the state instances, not the Cog.

### Shared state mutability and safe concurrency

When a Cog declares that it uses a piece of state, whether C++ state or a schema, it also declares whether it needs to mutate that state or not.
By default, the state is read-only (passed as `const`), but the Cog can specifically declare that it mutates the state.

This is the basis of safe and reproducible state sharing with concurrency in Clockwork: the Clockwork system will ensure that only a single Cog that mutates a piece of state is executing at any given time.
This means that the Cog can safely mutate the state without needing to worry about other Cogs concurrently accessing it.
If multiple Cogs need to read the state, they can do so concurrently, but if any Cog needs to mutate it, it will block all other Cogs that read or mutate that state until it completes.

This ensures that, as with message passing, Cog execution is hermetic and cannot be influenced by other Cogs during its execution.
It also provides a safe shared state concurrency mechanism that does not require locks or other synchronization primitives and eliminates entire classes of race conditions, deadlocks, and other concurrency issues.
