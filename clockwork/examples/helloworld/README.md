# Helloworld

This example demonstrates a simple single producer - single consumer relationship.

## Running

Run the system

```bash
bazel run //clockwork/examples/helloworld
```

You should see the following input in the output file ("/tmp/simplelaunch_logs/consumer_process_0.log"):

```bash
Received a new message on InputA: 42
```
