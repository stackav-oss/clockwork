# Using the deterministic runner

The deterministic runner can be used when invoking a Clockwork executable by passing in command line arguments.

> [!WARNING]
> The deterministic runner ONLY WORKS WITH A SINGLE PROCESS SYSTEM DEFINITION.

Below are the deterministic runner specific command line options:

- deterministic-runner: switch that specifies the deterministic runner should be used.
- input-log-uri: URI of the input log file. This is the log file which messages will be published from.
- output-log-uri: URI of the output log file. This is the log file which configured channels will be written to.
- channel-publisher-config: file path to the log publisher configuration Tachyon file.
- log-writer-config: file path to the log writer configuration Tachyon file.
- sim-start-time-ns: start time of the simulation in nanoseconds
- sim-end-time-ns: end time of the simulation in nanoseconds

> [!NOTE]
> The input log URI cannot be specified if there is no log publisher config specified.
> Similarly an output log URI cannot be specified if there is no log writer config specified.

If an input log URI is specified, the start and end time will default to that of the log file unless they are explicitly specified.

If an input log URI is not specified, then both a start and end time must be.
