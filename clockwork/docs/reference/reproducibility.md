

# Application layer offline reproducibility

## Overview/Purpose

Clockwork provides a means for reproducible simulation and testing via the Deterministic Runner.
This allows for test writers to be guaranteed that all deviations from the expected test outcome can be attributed to the system under test rather than variations in how the Clockwork infrastructure performed the execution of the test.

## In-Scope

- Providing a means to run single-process single threaded deterministically ordered execution of Clockwork cogs

## Out of Scope

- Online determinism
- Running multiple Clockwork processes deterministically.
- Preventing non-deterministic behavior of the cogs that compose the system.
- Exact reproduction of online events in offline simulation

## Current Solution

Clockwork provides what is termed the “Deterministic Runner”.
This runner is single threaded and executes cogs based on the order in which they become ready to execute.
The deterministic runner also is capable of injecting captured logs into the system.
This also does not affect determinism.
As long as the log remains identical, the message will be injected at the appropriate simulation time upon each execution.

The lack of multiple threads or processes in the underlying deterministic runner, precludes any non-deterministic behavior.
However, if underlying cogs behave in a non-deterministic fashion, especially in respect to whether or not outputs are generated, non-deterministic results will occur.
