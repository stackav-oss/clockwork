# Clockwork troubleshooting

This document includes some guidance on how to debug and resolve common issues when building or deploying a clockwork system.

## Deploying: TCP bridge not delivering messages

If you aren't seeing messages being delivered between Cogs on different nodes in a multi-node system, the TCP bridge may be to blame.
If you suspect this is the case, grab at least one frame of debug information with `channel_spy echo /tcp_bridge_status` and include it in a bug report.
If the bridge status channel is silent, continue reading for other things to check.

Sometimes the TCP bridge will get stuck during initialization, which prevents it from doing meaningful work.
This manifests as user Cogs with remote publishers not seeing any messages.

If the tail of the log is full of messages like the following, then the bridge daemon is waiting for the publisher of a local channel to start up.

```text
INFO platforms/clockwork/pinion/tcp_bridge_main.cc:113 [1736968342.171369826] Waiting for publisher creation (10 subscribers remaining, Next up: ...)
```

This is most likely happening because the process that executes the publisher has exited prematurely.
You can determine which process(es) are affected by `simplelaunch_client list` on the relevant node.
If any processes are reported as `PROCESS_STATE_EXITED`, they are probably the culplrit.
Check the console logs of the unhappy process with `simplelaunch_client logs <procress_name>` and address its complaints.
If the process failed to launch because the process binary couldn't be found, the relevant cpp_executable target was likely left out of the data dependencies for the node's `merge_simplelaunch_config` target.
If its not obvious how to address the processes's complaints, include the console logs in a bug report.

If the tail of log is full of message indicating failures to connect to another bridge, then the peer has likely crashed.

```text
ERROR platforms/clockwork/pinion/tcp_bridge_client.cc:138 [1736968352.385427920] failed to open socket to server at 10.0.0.22:16000: Connection refused. Trying again in 5s
```

You can confirm this by running `simplelaunch_client list` on the node with the IP address indicated in the log.
If the bridge has crashed, grab the console logs with `simplelaunch_client logs tcp_bridge_main` and include them in a bug report.
