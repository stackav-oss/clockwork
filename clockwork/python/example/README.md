# Example Python Cogs

This directory defines a system that runs three cogs: input1, input2 and output.
The input1 and input2 cogs publish messages periodically on the /example/input1 and /example/input2 channels respectively.
The output cog subscribes to the input1 and input2 messages and publishes what it receives on the /example/output channel.
Additionally a logger and channel spy are configured so that the messages can be examined live on the system and also after a run by reading the log.

The command to run the demo cogs from within the Clockwork repo is:

```bash
bazel run //clockwork/python/example
```

To see the output from the output cog use this command:

```base
bazel run //clockwork/tools/channel_spy:channel_spy_cli echo /example/output
```

The console logs from the run will be written in /tmp/simplelaunch_logs.
