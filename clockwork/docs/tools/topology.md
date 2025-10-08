# Clockwork system topology tool

Clockwork systems define topology through connections made within each Box in the system.
Users can visually inspect the files with these boxes to see these connections, but as systems grow larger this quickly becomes unfeasible.
The topology tool provides an easy way to extract basic information about the system.
Examples include describing the publishers and subscribers of a channel or the inputs and outputs of an entity (e.g., Cog or Socket).
Please see `--help` both on the tool and each sub-command for the latest information on how to use the tool.

## Example usage

Use of the tool first requires a defined `topology_summary` target using a Clockwork system target as an input.
Here is an example of defining such a target.

```bazel
load("//clockwork/tools/topology:rules.bzl", "topology_summary")

clk(
    name = "test_system_clk",
    srcs = ["test_system.clk"],
    outs [...],
)

topology_summary(
    name = "test_system_clk_topology",
    system_target = ":test_system_clk",
)
```

In this example, the target `:test_system_clk` is assumed to be a Clockwork target that defines a `system_target` within the `.clk` source file.
Assuming the example above is in package `//a/b/c`, below is an example invocation of the tool.

```bash
bazel run @clockwork//clockwork/tools/topology:query -- //a/b/c:test_system_clk_topology list-entities
```

Internally, the tool calls `bazel` to build that topology target ensuring it is up to date.
This specific invocation will list the names of every entity in the system.

For more details, there are some unit tests that exercise this tool with a sample system and a topology target.
Those tests can be found within `//clockwork/tools/topology/tests/...`.

### FAQ

#### Bridge impact

A common question often asked is whether adding an input to a cog will cause a change in network utilization.
By running the `channel` subcommand, users can sees which CPU the publisher and subscribers run on.

```console
$ bazel run @clockwork//clockwork/tools/topology:query -- @clockwork//clockwork/tools/topology/tests:test_system_multi_node_topology_summary channel MultiNodeChan
...
Channel: MultiNodeChan
  Type: @clockwork::clockwork::tests::support::test_messages_multi_node.MultiNodeMessage
  Size: 8 bytes
  Publishers:
    - CPU: TestSystemCpu1
        - @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_cog
  Subscribers
    - CPU: TestSystemCpu2
        - @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_cog
```

Currently, the only supported bridge is via TCP so it can be assumed any channel crossing two nodes will impact the network.

If you are unsure of the exact channel name to query for, simply list them out first.

```console
$ bazel run @clockwork//clockwork/tools/topology:query -- @clockwork//clockwork/tools/topology/tests:test_system_multi_node_topology_summary list-channels
...
MultiNodeChan
SinkChan
SourceChan
```
