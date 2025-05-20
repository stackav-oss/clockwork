# Clockwork execution conditions

This document describes Clockwork execution conditions for controlling when Cog functions execute.

## Dial members

Each Cog Dial will have a sub-structure in the member `conditions` which contains a public member for every execution condition it defines, with the same name as the condition.
The condition member will be an instance of `clockwork::ExecCondition` or a subclass.
The base class provides the ability to check if the condition was active at the beginning of the current Cog function execution using either the `is_active()` method or `static_cast<bool>` of the instance.
Different types of execution conditions may add more functionality, as follows.

### `time_since_last_exec`

A `time_since_last_exec` condition will create an instance of `clockwork::TimeSinceLastExecCondition`, which adds to the base information a `get_time_since_last_exec()` method returning the time since the last execution of the Cog function.
The time is always measured from start time of one execution to start time of the next, not from end time to start time.
Cog functions may also query the threshold for the condition, specified in the domain-specific language (DSL) source, via the member function `get_threshold()`.

#### Example `time_since_last_exec` DSL

```clockwork
cog Example
{
    execution
    {
        condition timeout: time_since_last_exec(120ms);
    }
}
```

#### Example of using generated Dial facilities for `time_since_last_exec` condition

```cpp
struct ExampleDial
{
  struct Conditions
  {
      // The precise type here is not guaranteed, but there will be a "timeout" member.
      stack::clockwork::TimeSinceLastExecCondition<120'000'000U> timeout;
  } conditions;
};

// Example user code:
if (dial.conditions.timeout.is_active())
{
  // You wouldn't need this because Clockwork already records timing diagnostics, but just to illustrate the API:
  debug_data.timeout_scheduling_latency =
    dial.conditions.timeout.get_time_since_last_exec() - dial.conditions.timeout.get_threshold();
}
```

## Rate limiting

If you have a Cog you know to be bursty, you can avoid overwhelming downstream Cogs by rate limiting its execution.
You can add a rate limit in the `execution` block by including a `rate limit` statement.
Consider the following example.
This Cog will execute whenever it has a new input or whenever the periodic condition times out unless it has published more than five messages to the output `bar` in the last ten milliseconds.
You can think of the rate limiter as an additional execution condition that gets logically added with the result of the expression you provided to `execute when`.
You can specify a rate limit for each of your Cog's outputs, and it will only execute if none of the limits have been exceeded.

> [!IMPORTANT]
> Adding rate limits to your cog will prevent it from executing whenever they are exceeded.
> This is particularly important to keep in mind if your Cog uses the `time_since_last_exec` condition, as the gaps between executions may be considerably longer than the specified interval while being throttled.

```clockwork
cog Example
{
  inputs
  {
    foo: Tappy<FooSchema>;
  }
  outputs
  {
    bar: Tappy<BarSchema>;
  }
  execution
  {
    rate limit bar: 5 every 10ms;
    condition periodic: time_since_last_exec(1ms);
    condition new_foo: new_message(foo);
    execute when: new_foo or periodic;
  }
}
```
