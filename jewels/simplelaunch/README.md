# `simplelaunch`

`simplelaunch` is a tool to launch processes and manage their lifecycle.

## Running `simplelaunch`

To run the server and the configured subprocesses:

```bash
bazel run -- //jewels/simplelaunch -c $PWD/path/to/config.textproto
```

You can also generate an executable target using the `simplelaunch_runner` rule.
You can run the target from the snippet below directly with `bazel run //path/to/module:a_runner` or you can use it as an entry point to a distributable artifact.

```bazel
load("//jewels/simplelaunch:rules.bzl", "simplelaunch_runner")

simplelaunch_runner(
    name = "a_runner",
    config = ":config.textproto",
)
```

## Writing a `simplelaunch` config

The config format is a textproto with a list of `app` and `pre_launch` entries.

### Schema

```protobuf
syntax = "proto3";

package jewels.simplelaunch.v1;

// The configuration for the task_manager.
message Config {
  // A list of application-specific configurations.
  repeated AppConfig app = 1;
  // A list of binaries to run synchronously in order before launching any applications.
  // Note: Right now these are launched _before_ the HTTP status server comes up.
  // Make sure they do not block for a large amount of time.
  repeated AppConfig pre_launch = 2;
}

// The configuration for an individual application.
message AppConfig {
  // Descriptive name of application.
  string name = 1;
  // Path to the executable.
  string executable = 2;
  // Arguments to run the executable with.
  repeated string args = 3;
  // The environment variables to set.
  map<string, string> env = 4;
  // Run the application as root.
  // Note: This can only be used for pre_launch apps.
  bool as_root = 5;
}
```

**`example.textproto`**

```textproto
app {
  name: "kitty cat"
  executable: "/usr/bin/cat",
  args: ["/home/steph/config.textproto"],
  env: { key: "keyN", value: "valueN", }
}
app {
  name: "Snorlax"
  executable: "/usr/bin/sleep",
  args: ["120"],
  env: {}
}
```

## `simplelaunch` and POSIX signals

`simplelaunch` will catch `SIGINT`, `SIGQUIT`, and `SIGTERM`; then will, before exiting itself, attempt to stop all unstopped children with the following signals (in order):

- SIGINT
- SIGTERM
- SIGABRT
- SIGKILL

If a child process accepts these signals but does not terminate, it will be reparented to the init process (pid 1) once `simplelaunch` has exited.

`simplelaunch` will track `SIGCHLD` and the reason for exiting, so that information may be used by callers of the REST API.
