# Clockwork Dynamic Scheduling

This document describes the high-level scheduling architecture of Clockwork's online dynamic Cog scheduler and execution engine.
At the time of this writing, Clockwork has two different execution engines: the online dynamic executor and the offline deterministic executor.
The architectural design intent is to eventually also support an online statically-scheduled executor, but that is not yet implemented.
This document focuses only on the online dynamic executor.

## Overview

Clockwork's online dynamic scheduling architecture is designed to enable flexible and efficient execution of Cogs in soft real-time environments.
It relies largely on the underlying operating system's scheduler; at the time of this writing only Linux is supported, so we rely on Linux's scheduler.
Any given deployment may use any of the Linux scheduling policies, and selection and tuning of the underlying scheduler is outside the scope of this document.

### Processes and threads

Clockwork system composition allocates Cogs to processes and supports multiple Cogs per process and multiple processes per system.
Each process also has multiple execution threads, but Cogs are not allocated to specific threads; Cogs within a process all share the same pool of threads.

Clockwork maintains one dedicated thread per process for Clockwork I/O event handling, which primarily runs an `epoll` loop.
This thread adds Cog instances to the execution queue when they are ready to run, and also handles underlying I/O tasks.

### Cog execution queue

Each process maintains a single execution queue shared by all Cogs and worker threads in that process.
The queue is a simple FIFO (First-In, First-Out) queue with no prioritization options other than FIFO.
Clockwork does not support direct specification of prioritization because the design intent is primarily to execute using the static (ahead of time) scheduler, which will make prioritization decisions at compile time based on critical path analysis.
It is possible to achieve some prioritization with the dynamic scheduler at a process level by setting Linux process priorities and segregating Cogs into different processes for different priority levels; this is outside the scope of this document.

Each Cog instance may be in the execution queue only once; this is maintained as a queue invariant.
Cogs are added to the queue when all of their execution conditions are met, meaning they are eligible to run.
After execution, they may be added back to the end of the queue if they are eligible to run again, or else will be removed from the queue.
The I/O thread will re-add them to the queue when they become eligible again.

Being in the queue does not guarantee that the Cog instance is executable; it means all execution conditions are met, but if the Cog has shared state, the Cog must also be able to acquire the necessary locks to access that state before it can run.
Therefore, the dynamic scheduler may skip over Cogs in the queue that cannot currently run due to lock contention.
Those Cogs will remain at the front of the queue until they can obtain their locks, which avoids starvation.

### CPU Core Pinning

Clockwork supports pinning Cog processes to specific CPU cores during the system composition stage.
This is applied at the process level, not the Cog level, and affects all Cogs in that process.

### Execution metrics and monitoring

The dynamic executor collects extensive execution statistics, such as the duration of each Cog execution, the amount of time each Cog instance spends waiting in the execution queue, and the number of times each Cog instance is skipped due to lock contention.
These metrics are logged for offline analysis and can also be used to trigger faults at runtime if needed.
