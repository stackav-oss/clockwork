# Helloworld

This example demonstrates a simple single producer - single consumer relationship.

## Running

In one terminal run the following to start the producer Cog:

```bash
bazel run //clockwork/examples/helloworld:cogs_exe -- clockwork/examples/helloworld/clockwork.clockwork.examples.helloworld.cogs.producer_consumer.producer_process.tachyon
```

In another terminal, run the following to start the consumer Cog:

```bash
bazel run //clockwork/examples/helloworld:cogs_exe -- clockwork/examples/helloworld/clockwork.clockwork.examples.helloworld.cogs.producer_consumer.consumer_process.tachyon
```

You should see the following input in the consumer terminal:

```bash
Received a new message on InputA: 42
```
