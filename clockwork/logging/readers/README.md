# Log readers

This directory contains code to read logs in any supported log format.
The current log formats we can read are onboard, offboard, and MCAP.

## Log Processor

The log processor provides a callback interface for reading logs.
The LogProcessor constructor takes a configuration instance contains the log URI and optional absolute and relative time intervals.
The following snippet shows how to create a log processor to read a log from disk.

```cpp
// Initialize python for the log schema upgrader
clockwork::python::python_init_once();

clockwork_logging::LogProcessor processor({
  .uri = "/path/to/the/log",
  .interval = {},
  .relative_interval = {},
});
```

Note the call to `python_init_once`.
Python needs to be initialized because the log processor may need to execute python code to process messages.

Callbacks are added to the log processor to read messages from a channel using the `add_callback` method.
The template parameter to `add_callback` provides the class corresponding to the data in the channel.
The log processor handles deserialization and automatically upgrades messages from the schema in the log to the current schema using the metadata stored in the log.

```cpp
processor.template add_callback<clockwork::Tappy<YourClockworkClass>>(
    "channel_name", [](const auto& msg) { /* process the message */ });
```

Once all callbacks have been added, the `process` method is used to read the log and call the user provided callbacks for each message.

```cpp
processor.process();
```

## Integrating with schema upgrade

Some of the code that handles schema version compatibility is written in python, and in order for the python code to work the log processor executable must run in a python environment.
A `cc_binary_with_embedded_python` Bazel macro is provided that sets up the runtime environment and executes the binary so that python can find its dependencies.
Schema upgrade needs a python dependency on `@clockwork//clockwork/serialization/py:create_upgrader`.
The Bazel file to build a log processor binary looks like the following.

```bazel
load("//tools/rules:python.bzl", "cc_binary_with_embedded_py")

cc_binary_with_embedded_py(
    name = "your_binary",
    srcs = ["your_binary.cc"],
    py_deps = ["//clockwork/serialization/py:create_upgrader"],
    deps = [
        "//clockwork/logging/readers:log_processor",
        "//clockwork/python:python_init_once",
        YOUR_DEPENDENCIES_HERE,
    ],
)
```
