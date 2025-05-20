# `log_cerr` usage

Logging to `std::cerr` is supported through a series of `log_cerr_*` functions to separate log messages by `LogLevel`.
Developers should ensure that their components are producing the correct logging information at an appropriate level.
Supported levels and general guidance are given below:

- debug: granular information that can be useful for diagnosing an issue for the purpose of making sure everything is operating correctly.
- info: standard log level that indicates something has happened and general developers may wish to be aware of the event, but can safely ignore it otherwise.
- warn: indicates the detection of unexpected behavior that may affect component functionality, but it can continue to operate.
- error: indicates that one or more functional components are unable to function.
- fatal: indicates a system-wide error that prevents the entire system from functioning.

## Limiting log messages

Verbose logging can sometimes obscure important (eg, error) events that the developer is actually interested in.
Log messages can be pre-filtered by setting the environment variable `STACK_LOG_CERR_THRESHOLD` to one of the log levels above; eg, `export STACK_LOG_CERR_THRESHOLD=warn`.
By default, the log threshold is set to `info`.

## Enabling colorful logs

By default, colorful logs are disabled.
To configure colorful logs, set the environment variable `STACK_LOG_CERR_COLOR_MODE` to `always`, `never`, or `auto`.
Setting to `auto` will enable colorful logs for terminal output only.
